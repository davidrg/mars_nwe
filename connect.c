/* connect.c  01-Nov-97 */
/* (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "net.h"
#include "unxfile.h"

#include <dirent.h>
#include <utime.h>

/* #define TEST_FNAME   "PRINT.000"
*/

int use_mmap=USE_MMAP;
int tells_server_version=1;   /* defaults to 3.11 */
int server_version_flags=0;
int max_burst_send_size=0x2000;
int max_burst_recv_size=0x2000;


#ifdef TEST_FNAME
  static int test_handle=-1;
#endif

static int  default_uid=-1;
static int  default_gid=-1;
static int  default_umode_dir=0775;
static int  default_umode_file=0664;

#include "nwfname.h"
#include "nwvolume.h"
#include "nwfile.h"
#include "connect.h"


typedef struct {
   ino_t  inode;        /* Unix Inode dieses Verzeichnisses */
   time_t timestamp;    /* Zeitmarke          */
   uint8  *path;        /* path ab Volume     */
   uint8  volume;       /* Welches Volume     */
   uint8  is_temp;      /* 0:perm. 1:temp 2: spez. temp */
   uint8  drive;        /* driveletter        */
   uint8  task;         /* actual task        */
} NW_DIR;


NW_DIR    dirs[MAX_NW_DIRS];
int       used_dirs=0;
int       act_uid=-1;
int       act_gid=-1;
int       act_obj_id=0L;  /* not login                  */
int       entry8_flags=0; /* special flags, see examples nw.ini, entry 8 */
int       act_umode_dir=0;
int       act_umode_file=0;

static gid_t *act_grouplist=NULL;  /* first element is counter !! */

static int       connect_is_init = 0;

#define MAX_DIRHANDLES    80
#define EXPIRE_COST	  86400 /* Is one day enough? :) */

typedef struct {
  DIR    *f;
  char   unixname[256]; /* kompletter unixname       */
  ino_t  inode;         /* Unix Inode                */
  time_t timestamp;     /* fr letzte Allocierung    */
  char   *kpath;        /* Ein Zeichen nach unixname */
  int    vol_options;   /* searchoptions             */
  int    volume;        /* Volume Number	     */

  int    sequence;      /* Search sequence           */
  off_t  dirpos;        /* Current pos in unix dir   */
} DIR_HANDLE;

static DIR_HANDLE   dir_handles[MAX_DIRHANDLES];

static int anz_dirhandles=0;

static char *build_unix_name(NW_PATH *nwpath, int modus)
/*
 * returns complete UNIX path
 * modus & 1 : ignore fn, (only path)
 * modus & 2 : no  '/' at end
 */
{
  static char unixname[300];    /* must be big enouugh */
  int volume = nwpath->volume;
  char *p, *pp;
  if (volume < 0 || volume >= used_nw_volumes
                 || !nw_volumes[volume].unixnamlen) {
    errorp(0, "build_unix_name", "volume=%d not ok\n", volume);
    strcpy(unixname, "Z/Z/Z/Z"); /*  */
    return(unixname);
  }
  strcpy(unixname, (char*)nw_volumes[volume].unixname); /* first UNIXNAME VOLUME */

  p  = pp = unixname+strlen(unixname);
  strcpy(p,  (char*)nwpath->path);  /* now the path */
  p += strlen((char*)nwpath->path);
  if ( (!(modus & 1)) && nwpath->fn[0])
    strcpy(p, (char*)nwpath->fn);    /* and now fn  */
  else if ((modus & 2) && (*(p-1) == '/')) {
    if (p > unixname+1) *(--p) = '\0';
    else {
      *p++ = '.';
      *p   = '\0';
    }
  }
  dos2unixcharset(unixname);
  return(unixname);
}


static int new_dir_handle(ino_t inode, NW_PATH *nwpath)
/*
 * RETURN=errorcode (<0) or dir_handle
 */
{
  int rethandle;
  DIR_HANDLE  *dh   = NULL;
  time_t  akttime   = time(NULL);
  time_t  last_time = akttime;
  int  thandle      = 1;
  int  nhandle      = 0;
  for (rethandle=0; rethandle < anz_dirhandles; rethandle++){
    dh=&(dir_handles[rethandle]);
    if (dh->f) { /* only the actual dir-handle should remain open */
      closedir(dh->f);
      dh->f=NULL;
    }
    if (!dh->inode) {
      if (!nhandle)
        nhandle = rethandle+1;
    } else if (dh->inode == inode && dh->volume == nwpath->volume){
      nhandle   = rethandle+1;
      break;
    } else if (dh->timestamp < last_time){
      thandle   = rethandle+1;
      last_time = dh->timestamp;
    }
  }

  if (!nhandle){
    if (anz_dirhandles < MAX_DIRHANDLES) {
      dh=&(dir_handles[anz_dirhandles]);
      dh->f=NULL;
      rethandle = ++anz_dirhandles;
    } else {
      dh=&(dir_handles[thandle-1]);
      rethandle = thandle;
    }
  } else
    rethandle=nhandle;

  /* init dir_handle */
  dh=&(dir_handles[rethandle-1]);
  strcpy(dh->unixname, build_unix_name(nwpath, 0));
  dh->kpath         = dh->unixname + strlen(dh->unixname);
  if (dh->f)
    closedir(dh->f);
  if ((dh->f        = opendir(dh->unixname)) != (DIR*) NULL){
    dh->volume      = nwpath->volume;
    dh->vol_options = nw_volumes[dh->volume].options;
    dh->inode       = inode;
    dh->timestamp   = akttime;
    dh->sequence    = 0;
    dh->dirpos      = (off_t)0;
    if (dh->vol_options & VOL_OPTION_REMOUNT) {
      closedir(dh->f);
      dh->f = NULL;
    }
  } else {
    dh->inode       = 0;
    dh->unixname[0] = '\0';
    dh->vol_options = 0;
    dh->kpath       = dh->unixname;
    rethandle       = /* -0x9c */ -0xff;
  }
  return(rethandle);
}

static int free_dir_handle(int dhandle)
{
  if (dhandle > 0 && --dhandle < anz_dirhandles) {
    DIR_HANDLE  *dh=&(dir_handles[dhandle]);
    if (dh->f != (DIR*) NULL) {
      closedir(dh->f);
      dh->f = (DIR*)NULL;
    }
    dh->inode = 0;
    while (anz_dirhandles && !dir_handles[anz_dirhandles-1].inode)
      anz_dirhandles--;
    return(0);
  }
  return(-0x88); /* wrong dir_handle */
}

void set_default_guid(void)
{
  seteuid(0);
  setgroups(0, NULL);
  if (setegid(default_gid) < 0 || seteuid(default_uid) < 0) {
    errorp(1, "set_default_guid, !! SecurityAbort !!",
      "Cannot set default gid=%d and uid=%d" , default_gid, default_uid);
    exit(1);
  }
  act_gid = default_gid;
  act_uid = default_uid;
  act_umode_dir  = default_umode_dir;
  act_umode_file = default_umode_file;
  xfree(act_grouplist);
}

void set_guid(int gid, int uid)
{
  if ( gid < 0 || uid < 0
     || seteuid(0)
     || setegid(gid)
     || seteuid(uid) ) {
    set_default_guid();
  } else if (act_gid != gid || act_uid != uid) {
    struct passwd *pw = getpwuid(uid);
    if (NULL != pw) {
      seteuid(0);
      initgroups(pw->pw_name, gid);
    }
    act_gid = gid;
    act_uid = uid;

    act_umode_dir  = default_umode_dir;
    act_umode_file = default_umode_file;

    xfree(act_grouplist);
    if (seteuid(uid))
      set_default_guid();
    else {
      int k=getgroups(0, NULL);
      if (k > 0) {
        act_grouplist=(gid_t*)xmalloc((k+1) * sizeof(gid_t));
        getgroups(k, act_grouplist+1);
        *act_grouplist=(gid_t)k;
      }
    }
  }
  XDPRINTF((5,0,"SET GID=%d, UID=%d %s", gid, uid,
        (gid==act_gid && uid == act_uid) ? "OK" : "failed"));
}

void reset_guid(void)
{
  set_guid(act_gid, act_uid);
}

void reseteuid(void)
{
  if (seteuid(act_uid))
    reset_guid();
}

void set_act_obj_id(uint32 obj_id)
{
  act_obj_id=obj_id;
  XDPRINTF((5, 0, "actual obj_id is set to 0x%x", obj_id));
}

int in_act_groups(gid_t gid)
/* returns 1 if gid is member of act_grouplist else 0 */
{
  int    k;
  gid_t *g;
  if (!act_grouplist) return(0);
  k=(int)*act_grouplist;
  g = act_grouplist;
  while (k--) {
    if (*(++g) == gid) return(1);
  }
  return(0);
}

int get_real_access(struct stat *stb)
/* returns F_OK, R_OK, W_OK, X_OK  */
/* ORED with 0x10 if owner access  */
/* ORED with 0x20 if group access  */
{
  int mode = 0;
  if (!act_uid) return(0x10 | R_OK | W_OK | X_OK) ;
  else {
    if (act_uid == stb->st_uid) {
      mode    |= 0x10;
      if (stb->st_mode & S_IXUSR)
        mode  |= X_OK;
      if (stb->st_mode & S_IRUSR)
        mode  |= R_OK;
      if (stb->st_mode & S_IWUSR)
        mode  |= W_OK;
    } else if ( (act_gid == stb->st_gid)
             || in_act_groups(stb->st_gid) ) {
      mode    |= 0x20;
      if (stb->st_mode & S_IXGRP)
        mode  |= X_OK;
      if (stb->st_mode & S_IRGRP)
        mode  |= R_OK;
      if (stb->st_mode & S_IWGRP)
        mode  |= W_OK;
    } else {
      if (stb->st_mode & S_IXOTH)
        mode  |= X_OK;
      if (stb->st_mode & S_IROTH)
        mode  |= R_OK;
      if (stb->st_mode & S_IWOTH)
        mode  |= W_OK;
    }
  }
  return(mode);
}

uint32 get_file_owner(struct stat *stb)
/* returns nwuser creator of file */
{
  uint32 owner=1L;  /* default Supervisor */
  if (act_obj_id && stb && (stb->st_uid == act_uid))
    owner=act_obj_id;
  XDPRINTF((5, 0, "get_file_owner owner=0x%x", owner));
  return(owner);
}

static char *conn_get_nwpath_name(NW_PATH *p)
/* for debugging */
{
static char nwpathname[300];
  char volname[100];
  if (p->volume < 0 || p->volume >= used_nw_volumes) {
    sprintf(volname, "<%d=NOT-OK>", (int)p->volume);
  } else strcpy(volname, (char*)nw_volumes[p->volume].sysname);
  sprintf(nwpathname, "%s:%s%s", volname, p->path, p->fn);
  return(nwpathname);
}

static int x_str_match(uint8 *s, uint8 *p, int soptions)
{
  uint8    pc, sc;
  uint     state = 0;
  uint8    anf, ende;
  int      not = 0;
  uint     found = 0;
  while ( (pc = *p++) != 0) {

    if (state != 100) {
      if (pc == 255 &&  (*p == '*' || *p == '?'
                      || *p==0xaa  || *p==0xae || *p==0xbf || *p=='.')) {
        pc=*p++;
      }

      switch  (pc) {
        case 0xaa:   pc='*';  break;
        case 0xae:   pc='.';  break;
        case 0xbf:   pc='?';  break;
      }
    }

    switch (state){
      case 0 :
      switch  (pc) {
        case '\\':  /* any following char */
                    if (*p++ != *s++) return(0);
                    break;

        case '?' :  if (!*s ||  (sc = *s++) == '.')
                      state = 10;
                    break;

        case  '.' : if (*s && pc != *s++) return(0);
                    break;

	case '*' : if (!*p) return(1); /* last star */
	           while (*s) {
	             if (x_str_match(s, p, soptions) == 1) return(1);
                     else if (*s == '.') return(0);
	             ++s;
	           }
                   state = 30;
                   break;

        case '[' :  if ( (*p == '!') || (*p == '^') ){
                       ++p;
                       not = 1;
                     }
                     state = 100;
                     continue;

        default  :  /* 'normal' chars */
                    if (soptions & VOL_OPTION_IGNCASE) {
                       if (!dfn_imatch(*s, pc))
                        return(0);
                    } else if (pc != *s) return(0);
                    s++;
                    break;

      }  /* switch */
      break;

      case 10 :  if (pc != '*' && pc != '?' ) {
                   if (pc == '.')
                     state = 0;
                   else return(0);
                 }
                 break;

      case 30:   if (pc != '.') return(0);
                 state = 31;
                 break;

      case 31:   if (pc != '*' ) return(0);
                 break;

      case   100  :   /*  Bereich von Zeichen  */
        sc = *s++;
        found = not;
        if (!sc) return(0);
        do {
          if (pc == '\\') pc = *(p++);
          if (!pc) return(0);
          anf = pc;
          if (*p == '-' && *(p+1) != ']'){
            ende = *(++p);
            p++;
          }
          else ende = anf;
          if (found == not) { /* only if not found */
            if (anf == sc || (anf <= sc && sc <= ende))
               found = !not;
          }
        } while ((pc = *(p++)) != ']');
        if (! found ) return(0);
        not   = 0;
        found = 0;
        state = 0;
        break;

      default :  break;
    }  /* switch */
  } /* while */
  if (*s=='.' && *(s+1)=='\0') return(1);  /* I hope this is right */
  return ( (*s) ? 0 : 1);
}

int fn_dos_match(uint8 *s, uint8 *p, int options)
{
  uint8 *ss=s;
  int   len=0;
  int   pf=0;
  for (; *ss; ss++){
    if (*ss == '.') {
      if (pf++) return(0); /* no 2. point */
      len=0;
    } else {
      ++len;
      if ((pf && len > 3) || len > 8) return(0);

      if (!(options & VOL_OPTION_IGNCASE)){
        if (options & VOL_OPTION_DOWNSHIFT){   /* only downshift chars */
          if (*ss >= 'A' && *ss <= 'Z') return(0);
        } else {            /* only upshift chars   */
          if (*ss >= 'a' && *ss <= 'z') return(0);
        }
      }

    }
  }
  return(x_str_match(s, p, options));
}

typedef struct {
  int        attrib;
  struct stat statb;
  uint8      *ubuf;   /* userbuff */
} FUNC_SEARCH;

static int func_search_entry(NW_PATH *nwpath, int attrib,
        int (*fs_func)(NW_PATH *nwpath, FUNC_SEARCH *fs), FUNC_SEARCH *fs)

/* returns  > 0  if OK  < 1 if not ok or not found */
{
  struct dirent* dirbuff;
  DIR            *f;
  int            result=0;
  int            okflag=0;
  char           xkpath[256];
  uint8          entry[256];
  int            volume = nwpath->volume;
  int            soptions;
  FUNC_SEARCH    fs_local;
  if (!fs) {
    fs        = &fs_local;
    fs->ubuf  = NULL;
  }
  fs->attrib  = attrib;
  if (volume < 0 || volume >= used_nw_volumes) return(-1); /* something wrong */
  else  soptions = nw_volumes[volume].options;
  strcpy((char*)entry,  (char*)nwpath->fn);

  nwpath->fn[0] = '\0';
  strcpy(xkpath, build_unix_name(nwpath, 1|2));

  XDPRINTF((5,0,"func_search_entry attrib=0x%x path:%s:, xkpath:%s:, entry:%s:",
        attrib, nwpath->path, xkpath, entry));
  if ((f=opendir(xkpath)) != (DIR*)NULL) {
    char *kpath=xkpath+strlen(xkpath);
    *kpath++ = '/';
    while ((dirbuff = readdir(f)) != (struct dirent*)NULL){
      okflag = 0;
      if (dirbuff->d_ino) {
        uint8 *name=(uint8*)(dirbuff->d_name);
        uint8 dname[256];
        xstrcpy(dname, name);
        unix2doscharset(dname);
        okflag = (name[0] != '.' &&
                 (  (entry[0] == '*' && entry[1] == '\0')
                 || (!strcmp((char*)dname, (char*)entry))
                 || fn_dos_match(dname, entry, soptions)));
        if (okflag) {
          *kpath = '\0';
          strcpy(kpath, (char*)name);
          if (!s_stat(xkpath, &(fs->statb), NULL)) {
            okflag = (  ( ( (fs->statb.st_mode & S_IFMT) == S_IFDIR) &&  (attrib & 0x10))
                  ||    ( ( (fs->statb.st_mode & S_IFMT) != S_IFDIR) && !(attrib & 0x10)));
            if (okflag){
              strcpy((char*)nwpath->fn, (char*)dname);
              XDPRINTF((5,0,"FOUND=:%s: attrib=0x%x", nwpath->fn, fs->statb.st_mode));
              result = (*fs_func)(nwpath, fs);
              if (result < 0) break;
              else result=1;
            }
          } else okflag = 0;
        }
        XDPRINTF((6,0, "NAME=:%s: OKFLAG %d", name, okflag));
      }  /* if */
    } /* while */
    closedir(f);
  } /* if */
  return(result);
}

static int get_dir_entry(NW_PATH *nwpath,
                          int    *sequence,
                          int    attrib,
                          struct stat *statb)

/* returns 1 if OK and 0 if not OK */
{
  struct dirent* dirbuff;
  DIR            *f;
  int            okflag=0;
  char           xkpath[256];
  uint8          entry[256];
  int            volume = nwpath->volume;
  int            soptions;
  int            akt_sequence=0;
  if (volume < 0 || volume >= used_nw_volumes) return(0); /* something wrong */
  else  soptions = nw_volumes[volume].options;
  strcpy((char*)entry,  (char*)nwpath->fn);
  nwpath->fn[0] = '\0';
  strcpy(xkpath, build_unix_name(nwpath, 1|2));
  XDPRINTF((5,0,"get_dir_entry attrib=0x%x path:%s:, xkpath:%s:, entry:%s:",
                          attrib, nwpath->path, xkpath, entry));

  if ((f=opendir(xkpath)) != (DIR*)NULL) {
    char *kpath=xkpath+strlen(xkpath);
    *kpath++ = '/';
    if (*sequence == MAX_U16) *sequence = 0;

    while (akt_sequence++ < *sequence) {
      if (NULL == readdir(f)) {
        closedir(f);
        return(0);
      }
    }

    while ((dirbuff = readdir(f)) != (struct dirent*)NULL){
      okflag = 0;
      (*sequence)++;
      if (dirbuff->d_ino) {
        uint8 *name=(uint8*)(dirbuff->d_name);
        uint8 dname[256];
        xstrcpy(dname, name);
        unix2doscharset(dname);
        okflag = (name[0] != '.' &&
                 (  (entry[0] == '*' && entry[1] == '\0')
                 || (!strcmp((char*)dname, (char*)entry))
                 || fn_dos_match(dname, entry, soptions)));
        if (okflag) {
          *kpath = '\0';
          strcpy(kpath, (char*)name);
          if (!s_stat(xkpath, statb, NULL)) {
            okflag = (  ( ( (statb->st_mode & S_IFMT) == S_IFDIR) &&  (attrib & 0x10))
                  ||    ( ( (statb->st_mode & S_IFMT) != S_IFDIR) && !(attrib & 0x10)));
            if (okflag){
              strcpy((char*)nwpath->fn, (char*)dname);
              XDPRINTF((5,0,"FOUND=:%s: attrib=0x%x", nwpath->fn, statb->st_mode));
              break; /* ready */
            }
          } else okflag = 0;
        }
        XDPRINTF((6,0, "NAME=:%s: OKFLAG %d", name, okflag));
      }  /* if */
    } /* while */
    closedir(f);
  } /* if */
  return(okflag);
}

static DIR *give_dh_f(DIR_HANDLE    *dh)
{
  if (!dh->f) {
    *(dh->kpath) = '\0';
    dh->f  = opendir(dh->unixname);
  }
  dh->timestamp=time(NULL); /* tnx to Andrew Sapozhnikov */
  return(dh->f);
}

static void release_dh_f(DIR_HANDLE *dh, int expire)
{
  if (dh->f && (dh->vol_options & VOL_OPTION_REMOUNT) ) {
    closedir(dh->f);
    dh->f = NULL;
  }
  if (expire)
    dh->timestamp-=EXPIRE_COST; /* tnx to Andrew Sapozhnikov */
}

static int get_dh_entry(DIR_HANDLE *dh,
                        uint8  *search,
                        int    *sequence,
                        int    attrib,
                        struct stat *statb)

/* returns 1 if OK and 0 if not OK */
{
  DIR            *f     = give_dh_f(dh);
  int            okflag = 0;

  if (f != (DIR*)NULL) {
    struct  dirent *dirbuff;
    uint8   entry[256];
    strmaxcpy(entry, search, 255);
    if ( (uint16)*sequence == MAX_U16)  *sequence = 0;

    if (*sequence < dh->sequence || ((long)dh->dirpos) < 0L) {
      dh->dirpos   = (off_t)0;
      dh->sequence = 0;
    }
    seekdir(f, dh->dirpos);

    if (dh->sequence != *sequence) {
      while (dh->sequence < *sequence) {
        if (NULL == readdir(f)) {
          dh->dirpos = telldir(f);
          release_dh_f(dh, 1);
          return(0);
        }
        dh->sequence++;
      }
      dh->dirpos = telldir(f);
    }
    XDPRINTF((5,0,"get_dh_entry seq=0x%x, attrib=0x%x path:%s:, entry:%s:",
                *sequence, attrib, dh->unixname, entry));

    while ((dirbuff = readdir(f)) != (struct dirent*)NULL){
      okflag = 0;
      dh->sequence++;
      if (dirbuff->d_ino) {
        uint8 *name=(uint8*)(dirbuff->d_name);
        uint8 dname[256];
        xstrcpy(dname, name);
        unix2doscharset(dname);
        okflag = (name[0] != '.' && (
                        (!strcmp((char*)dname, (char*)entry)) ||
                        (entry[0] == '*' && entry[1] == '\0')
                     || fn_dos_match(dname, entry, dh->vol_options)));

        if (okflag) {
          strcpy(dh->kpath, (char*)name);
          XDPRINTF((5,0,"get_dh_entry Name=%s unixname=%s",
                                  name, dh->unixname));

          if (!s_stat(dh->unixname, statb, NULL)) {
            okflag = ( (( (statb->st_mode & S_IFMT) == S_IFDIR) &&  (attrib & 0x10))
                     || (((statb->st_mode & S_IFMT) != S_IFDIR) && !(attrib & 0x10)));
            if (okflag){
              strcpy((char*)search, (char*)dname);
              break; /* ready */
            }
          } else okflag = 0;
        }
      }  /* if */
    } /* while */
    dh->kpath[0] = '\0';
    *sequence  = dh->sequence;
    dh->dirpos = telldir(f);
    release_dh_f(dh, (dirbuff==NULL));
  } /* if */
  return(okflag);
}

static void conn_build_path_fn( uint8 *vol,
                         uint8 *path,
                         uint8 *fn,
                         int   *has_wild,
                         uint8 *data,
                         int   len)

/* is called from build_path  */
{
   uint8  *p  = NULL;
   uint8  *p1 = path;
   *vol       = '\0';
   *has_wild  = 0;     /* no wild char */
   while (len-- && *data){
     if (*data == 0xae) *p1++ = '.';
     else if (*data == 0xaa|| *data == '*' ) {
       *p1++ = '*';
       (*has_wild)++;
     } else if (*data == 0xbf|| *data == '?' ) {
       *p1++ = '?';
       (*has_wild)++;
     } else if (*data == '/' || *data == '\\') {
       *p1++ = '/';
       p = p1;
     } else if (*data == ':') { /* extract volume */
       int len = (int) (p1 - path);
       memcpy(vol, path, len);
       vol[len]   = '\0';
       up_fn(vol);
       p1 = path;
     } else *p1++ = *data;
     data++;
   }
   *p1 = '\0';
   if (fn != NULL) {  /* if with filename     */
     if (p != NULL){  /* exist directory-path  */
       strcpy((char*)fn, (char*)p);
       *p = '\0';
     } else {         /* only filename */
       strcpy((char*)fn, (char*)path);
       *path= '\0';
     }
   }
}

static int build_path( NW_PATH *path,
                       uint8   *data,
                       int     len,
                       int     only_dir)
/*
 * fills path structure with the right values
 * if only_dir > 0, then the full path will be interpreted
 * as directory, in the other way, the last segment of path
 * will be interpreted as fn.
 * returns -0x98, if volume is wrong
 */
{
  uint8 vol[256];
  conn_build_path_fn(vol, path->path,
                     (only_dir) ? (uint8)NULL
                                : path->fn,
                     &(path->has_wild),
                     data, len);

  path->volume = -1;
  if (only_dir) path->fn[0] = '\0';

  if (vol[0]) {  /* there is a volume in path */
    int j = used_nw_volumes;
    while (j--) {
      if (!strcmp((char*)nw_volumes[j].sysname, (char*)vol)) {
        path->volume = j;
        if (nw_volumes[j].options & VOL_OPTION_DOWNSHIFT) {
          down_fn(path->path);
          down_fn(path->fn);
        } else {
          up_fn(path->path);
          up_fn(path->fn);
        }
        break;
      }
    }
    if (path->volume < 0) return(-0x98);
  }
  return(0);
}

static int nw_path_ok(NW_PATH *nwpath)
/* returns UNIX inode of path  */
{
  int j = 0;
  NW_DIR *d=&(dirs[0]);
  struct stat stbuff;
  int    result=0;

  if ((!act_obj_id) && !(entry8_flags & 1)) {
    if (nwpath->volume)
      result = -0x9c;   /* wrong path, only volume 0 is OK */
    else {
      char *p=nwpath->path;
      char pp[10];
      while (*p=='/') ++p;
      strmaxcpy(pp, p, 6);
      up_fn(pp);
      p=pp+5;
      if (memcmp(pp, "LOGIN", 5) || (*p!='\0' && *p!='/') )
        result=-0x9c;
    }
  }

  if (!result) {
    while (j++ < (int)used_dirs){
      if (d->inode && d->volume == nwpath->volume
                   && !strcmp((char*)nwpath->path, (char*)d->path)){
        return(d->inode);
      }
      d++;
    } /* while */
    if (!s_stat(build_unix_name(nwpath, 1 | 2 ), &stbuff, NULL)
      && S_ISDIR(stbuff.st_mode))
      return(stbuff.st_ino);
    result = -0x9c;   /* wrong path */
  }
  XDPRINTF((4,0x10, "NW_PATH_OK failed:`%s`", conn_get_nwpath_name(nwpath)));
  return(result);
}

static int build_verz_name(NW_PATH *nwpath,    /* gets complete path     */
                           int     dir_handle) /* search with dirhandle  */

/* return -completition code or inode */
{
   uint8      searchpath[256];
   uint8      *p=searchpath;
   uint8      *ppp=nwpath->path;
   int        completition=0;

   strcpy((char*)searchpath, (char*)ppp);  /* save path */

   if (nwpath->volume > -1) { /* absolute path */
     *ppp= '\0';
   } else  {  /* volume not kwown yet, I must get it about dir_handle */
     if (dir_handle > 0 &&
       --dir_handle < (int)used_dirs && dirs[dir_handle].inode){
       nwpath->volume = dirs[dir_handle].volume;
       if (searchpath[0] == '/') { /* absolute path */
         p++;
         *ppp = '\0';
       } else { /* get path from dir_handle */
         NW_VOL *v = &nw_volumes[nwpath->volume];
         strcpy((char*)ppp, (char*)dirs[dir_handle].path);
         if (v->options & VOL_OPTION_IGNCASE)
            ppp += strlen(ppp);
       }
     } else return(-0x9b); /* wrong dir handle */
   }
   if (*p) {
     uint8  *panf = nwpath->path;
     uint8  *p1   = panf+strlen((char*)panf);
     uint8  *a    = p;
     uint8  w;
     int    state = 0;
     while ((!completition) && (w = *p++) > 0){
       if (!state){
         XDPRINTF((5,0,"in build_verz_name path=:%s:", nwpath->path));
         if (w == '.')      state = 20;
         else if (w == '/') state = 30;
         else state++;
       } else if (state < 9){
         if (w == '.')      state = 10;
         else if (w == '/') state = 30;
         else state++;
         if (state == 9) completition= -0x9c; /* something wrong */
       } else if (state < 14){
         if (w == '.') return(-0x9c);
         else if (w == '/') state = 30;
         else state++;
         if (state == 14) completition= -0x9c; /* something wrong  */
       } else if (state == 20){
         if (ppp > nwpath->path)
            ppp=nwpath->path;
         if (w == '/') state = 30;
         else if (w != '.') completition= -0x9c; /* something wrong  */
       }
       if (state == 30 || !*p) { /* now action */
         uint8 *xpath=a;
         int   len = (int)(p-a);
         if (len && state == 30) --len; /* '/' don not need it here */
         a = p;
         if (len) {
           if (*xpath == '.') {
             uint8 *xp=xpath+1;
             if (*xp == '.') {
               while (*xp++ == '.' && completition > -1) {
                 p1--; /* steht nun auf letztem Zeichen '/' od. ':' */
                 if (p1 < panf) completition = -0x9c ;
                  /* wrong path, don't can go back any more */
                 else {
                   while (p1 > panf && *(--p1) != '/');;
                   if (p1 == panf) *p1='\0';
                   else *(++p1) = '\0';
                 }
               }
             }
           } else {
             memcpy(p1, xpath, len);
             p1   += len;
             *p1++ = '/';
             *p1   = '\0';
           }
         } /* if len */
         state = 0;
       }
     }
   }
   if (!completition) {
     if (nwpath->volume > -1 && nwpath->volume < used_nw_volumes){
       NW_VOL *v = &nw_volumes[nwpath->volume];
       if (v->options & VOL_OPTION_DOWNSHIFT) {
         down_fn(ppp);
         down_fn(nwpath->fn);
       } else {
         up_fn(ppp);
         up_fn(nwpath->fn);
       }
       if (v->options & VOL_OPTION_IGNCASE) {
         uint8 unixname[1024]; /* should be enough */
         uint8 *pp=unixname+v->unixnamlen;
         int   offset  = ppp - nwpath->path;
         int   pathlen = strlen(nwpath->path);
         int   fnlen   = strlen(nwpath->fn);
         memcpy(unixname, v->unixname, v->unixnamlen);
         strcpy(pp, nwpath->path);
         if (fnlen)
           strcpy(pp+pathlen, nwpath->fn);
         dos2unixcharset(pp);
         pp      += offset;
         pathlen -= offset;
         mangle_dos_name(v, unixname, pp);
         unix2doscharset(pp);
         XDPRINTF((5, 0, "Mangled DOS/unixname=%s", unixname));
           memcpy(ppp, pp, pathlen);
         if (fnlen)
           memcpy(nwpath->fn, pp+pathlen, fnlen);
       }
     } else return(-0x98); /* wrong volume */
     completition = nw_path_ok(nwpath);
   }
   return(completition);
}

int conn_get_kpl_path(NW_PATH *nwpath, int dirhandle,
                          uint8 *data, int len, int only_dir)
/*
 * if ok then the inode of dir will be returned
 * else a negativ errcode will be returned
 */
{
   int completition = build_path(nwpath, data, len, only_dir);
   XDPRINTF((5, 0, "compl=0x%x, conn_get_kpl_path %s",
      completition, conn_get_nwpath_name(nwpath)));
   if (!completition) completition = build_verz_name(nwpath, dirhandle);
   return(completition);
}

int conn_get_full_path(int dirhandle, uint8 *data, int len, 
                       uint8 *fullpath)
/* returns path in form VOLUME:PATH */
{  
  NW_PATH nwpath;
  int result = build_path(&nwpath, data, len, 0);
  fullpath[0]='\0';
  if (!result)
     result = build_verz_name(&nwpath, dirhandle);
  if (result > -1) {
    uint8 *p=(*nwpath.path=='/') ? nwpath.path+1 : nwpath.path;
    int len=sprintf(fullpath, "%s:%s",
       nw_volumes[nwpath.volume].sysname, p);
    if (nwpath.fn[0]) {
      if (*p) fullpath[len++]='/';
      strcpy(fullpath+len, nwpath.fn);
    }
    result=len+strlen(nwpath.fn);
  }
  XDPRINTF((1, 0, "conn_get_full_path: result=%d,(0x%x),`%s`", result, result, fullpath));
  return(result);
}

int conn_get_kpl_unxname(char *unixname,
                         int dirhandle,
                         uint8 *data, int len)
/*
 * gives you the unixname of dirhandle + path
 * returns volumenumber, or < 0 if error
 */
{
  NW_PATH nwpath;
  int completition = build_path(&nwpath, data, len, 0);
  if (!completition)
     completition = build_verz_name(&nwpath, dirhandle);
  if (completition > -1) {
    if (unixname)
      strcpy(unixname, build_unix_name(&nwpath, 0));
    completition=nwpath.volume;
  }
  XDPRINTF((5, 0, "conn_get_kpl_unxname: completition=0x%x", completition));
  return(completition);
}

void un_date_2_nw(time_t time, uint8 *d, int high_low)
{
  struct tm  *s_tm=localtime(&time);
  uint16  xdate=s_tm->tm_year - 80;
  xdate <<= 4;
  xdate |= s_tm->tm_mon+1;
  xdate <<= 5;
  xdate |= s_tm->tm_mday;
  if (high_low) {
    U16_TO_BE16(xdate, d);
  } else {
    U16_TO_16(xdate, d);
  }
}

void un_time_2_nw(time_t time, uint8 *d, int high_low)
{
  struct tm  *s_tm=localtime(&time);
  uint16  xdate=s_tm->tm_hour;
  xdate <<= 6;
  xdate |= s_tm->tm_min;
  xdate <<= 5;
  xdate |= (s_tm->tm_sec / 2);
  if (high_low) {
    U16_TO_BE16(xdate, d);
  } else {
    U16_TO_16(xdate, d);
  }
}

time_t nw_2_un_time(uint8 *d, uint8 *t)
{
  uint16 xdate = GET_BE16(d);
  uint16 xtime = (t != (uint8*) NULL) ? GET_BE16(t) : 0;

  int year     = (xdate >> 9) + 80;
  int month    = (xdate >> 5) & 0x0F;
  int day      = xdate & 0x1f;
  int hour     = xtime >> 11;
  int minu     = (xtime >> 5) & 0x3f;
  int sec      = (xtime & 0x1f) << 1;  /* Tnx to Csoma Csaba */
  struct tm    s_tm;
  s_tm.tm_year   = year;
  s_tm.tm_mon    = month-1;
  s_tm.tm_mday   = day;
  s_tm.tm_hour   = hour;
  s_tm.tm_min    = minu;
  s_tm.tm_sec    = sec;
  s_tm.tm_isdst  = -1;
  return(mktime(&s_tm));
}

int un_nw_attrib(struct stat *stb, int attrib, int mode)
/* mode: 0 = un2nw , 1 = nw2un */
{
  /* Attribute */
  /* 0x01   readonly     */
  /* 0x02   hidden       */
  /* 0x04   System       */
  /* 0x10   IS_DIR       */
  /* 0x20   Archive Flag */
  /* 0x80   Sharable     */  /* TLINK (TCC 2.0) don't like it ???? */

  int is_dir=S_ISDIR(stb->st_mode);
  
  if (!mode) {
    /* UNIX access -> NW access */
    if (!is_dir) {
      attrib = FILE_ATTR_A;
    } else {
      attrib = FILE_ATTR_DIR;
    }
    if (act_uid) {
      /* if not root */
      int acc=get_real_access(stb);
      if (!(acc & W_OK)) {
        attrib |= FILE_ATTR_R;    /* RO    */
      }
      if (!(acc & R_OK)) {
        attrib |= FILE_ATTR_H;   /* We say hidden here */
      }
    }
    /* only shared if gid == gid &&  x Flag */
    if (!is_dir) {
      if ((act_gid == stb->st_gid) && (stb->st_mode & S_IXGRP))
         attrib |= FILE_ATTR_SHARE;  /* shared flag */
    }
    return(attrib);
  } else {
    /* NW access -> UNIX access */
    int mode = S_IRUSR | S_IRGRP;
#if 0
    /* this is sometimes very BAD */
    if (attrib & FILE_ATTR_H)   /* hidden */
      stb->st_mode &= ~mode;
    else
#endif
      stb->st_mode |= mode;

    mode = S_IWUSR | S_IWGRP;
    if (attrib & FILE_ATTR_R)   /* R/O */
      stb->st_mode &= ~mode;
    else
      stb->st_mode |= mode;

    if (!is_dir) {
      if (attrib & FILE_ATTR_SHARE)   /* Shared */
        stb->st_mode |= S_IXGRP;
      else
        stb->st_mode &= ~S_IXGRP;
    }
    
    return(stb->st_mode);
  }
}

int un_nw_rights(struct stat *stb)
/* returns eff rights of file/dir */
/* needs some more work          */
{
  int rights=0xfb; /* first all rights, but not TRUSTEE_O */
  if (act_uid) {
    /* if not root */
    int is_dir = S_ISDIR(stb->st_mode);
    int acc=get_real_access(stb);
    int norights= TRUSTEE_A;  /* no access control rights */
    if (!(acc & W_OK)) {
      if (is_dir) {
        norights |= TRUSTEE_C;
        norights |= TRUSTEE_E;
      } else {
        norights |= TRUSTEE_W;  /* RO */
      }
    }
    if (!(acc & R_OK)) {
      if (is_dir) {
        norights |= TRUSTEE_F;  /* SCAN */
      } else {
        norights |= TRUSTEE_R;  /* Not for Reading */
      }
    }
    rights &= ~norights;
  } else rights|= TRUSTEE_S;  /* Supervisor */
  return(rights);
}

static int get_file_attrib(NW_FILE_INFO *f, struct stat *stb,
                           NW_PATH *nwpath)
{
  int voloptions=get_volume_options(nwpath->volume);
  strncpy((char*)f->name, (char*)nwpath->fn, sizeof(f->name));
  f->attrib=0;  /* d->name could be too long */
  up_fn(f->name);
  if (voloptions & VOL_OPTION_IS_PIPE)
    f->attrib      = FILE_ATTR_SHARE;
  else
    f->attrib      = (uint8) un_nw_attrib(stb, 0, 0);
  XDPRINTF((5,0, "get_file_attrib = 0x%x of ,%s, uid=%d, gid=%d",
         (int)f->attrib, conn_get_nwpath_name(nwpath),
         stb->st_uid, stb->st_gid));
  f->ext_attrib  = 0;
  un_date_2_nw(stb->st_mtime, f->create_date, 1);
  un_date_2_nw(stb->st_atime, f->acces_date,  1);
  un_date_2_nw(stb->st_mtime, f->modify_date, 1);
  un_time_2_nw(stb->st_mtime, f->modify_time, 1);
  U32_TO_BE32(stb->st_size,   f->size);
  return(1);
}

static int get_dir_attrib(NW_DIR_INFO *d, struct stat *stb,
                         NW_PATH *nwpath)
{
  XDPRINTF((5,0, "get_dir_attrib of %s", conn_get_nwpath_name(nwpath)));
  strncpy((char*)d->name, (char*)nwpath->fn, sizeof(d->name));
  d->attrib=0;  /* d->name could be too long */
  up_fn(d->name);
  d->attrib     = (uint8) un_nw_attrib(stb, 0, 0);
#if 0  /* changed: 02-Nov-96 */
  d->ext_attrib = 0xff; /* effektive rights ?? */
#else
  d->ext_attrib = (uint8) un_nw_rights(stb); /* effektive rights ?? */
#endif
  un_date_2_nw(stb->st_mtime, d->create_date, 1);
  un_time_2_nw(stb->st_mtime, d->create_time, 1);
  U32_TO_BE32(get_file_owner(stb), d->owner_id);
  d->access_right_mask = 0;
  d->reserved          = 0;
  U16_TO_BE16(0, d->next_search);
  return(1);
}

static int do_delete_file(NW_PATH *nwpath, FUNC_SEARCH *fs)
{
  char           unname[256];
  strcpy(unname, build_unix_name(nwpath, 0));
  XDPRINTF((5,0,"DELETE FILE unname:%s:", unname));
  return(nw_unlink(nwpath->volume, unname));
}

int nw_delete_datei(int dir_handle, uint8 *data, int len)
{
  NW_PATH nwpath;
  int completition = conn_get_kpl_path(&nwpath, dir_handle, data, len, 0);
  if (completition >  -1) {
    completition = func_search_entry(&nwpath, 0x6, do_delete_file, NULL);
    if (completition < 0) return(completition);
    else if (!completition) return(-0xff);
  }
  return(completition);
}

static int do_set_file_info(NW_PATH *nwpath, FUNC_SEARCH *fs)
{
  char unname[256];
  int  result=0;
  NW_FILE_INFO *f=(NW_FILE_INFO*)fs->ubuf;
  int voloption;
  strcpy(unname, build_unix_name(nwpath, 0));
  if ((voloption = get_volume_options(nwpath->volume)) & VOL_OPTION_IS_PIPE){
    ;;   /* don't change 'pipe commands' */
  } else if (voloption & VOL_OPTION_READONLY) {
    result=(-0x8c); /* no modify rights */
  } else {
    struct utimbuf ut;
    struct stat statb;
#if PERSISTENT_SYMLINKS
    S_STATB stb;
#endif
    ut.actime = ut.modtime = nw_2_un_time(f->modify_date, f->modify_time);
    if ( 0 == (result=s_stat(unname,  &statb, &stb)) &&
         0 == (result=s_utime(unname, &ut, &stb))){
      result = s_chmod(unname,
               un_nw_attrib(&statb, (int)f->attrib, 1), &stb);
    }
    if (result)
      result= (-0x8c); /* no modify rights */
  }
  XDPRINTF((5,0,"set_file_info result=0x%x, unname:%s:", unname, -result));
  return(result);
}

int nw_set_file_information(int dir_handle, uint8 *data, int len,
                             int searchattrib, NW_FILE_INFO *f)
{
  NW_PATH nwpath;
  int completition = conn_get_kpl_path(&nwpath, dir_handle, data, len, 0);
  if (completition >  -1) {
    FUNC_SEARCH  fs;
    fs.ubuf = (uint8*)f;
    completition = func_search_entry(&nwpath, searchattrib,
                 do_set_file_info, &fs);
    if (completition < 0) return(completition);
    else if (!completition) return(-0xff);
  }
  return(completition);
}

int nw_chmod_datei(int dir_handle, uint8 *data, int len,
                          int attrib, int access)

{
  char          unname[256];
  struct stat   stbuff;
  int           completition=-0x9c;
  NW_PATH       nwpath;
#if PERSISTENT_SYMLINKS
  S_STATB       stb;
#endif
  build_path(&nwpath, data, len, 0);
  if (nwpath.fn[0] != '.') { /* Files with . at the beginning are not ok */
    completition = build_verz_name(&nwpath, dir_handle);
  }
  if (completition < 0) return(completition);
  strcpy(unname, build_unix_name(&nwpath, 2));
  XDPRINTF((5,0,"set file attrib 0x%x, unname:%s:", access,  unname));

  if (!s_stat(unname, &stbuff, &stb)){
    int result = s_chmod(unname, un_nw_attrib(&stbuff, access, 1), &stb);
    return( (result != 0) ? -0x8c : 0);  /* no modify rights */
  }

  return(-0x9c); /* wrong path */
}

int nw_mk_rd_dir(int dir_handle, uint8 *data, int len, int mode)
{
  NW_PATH nwpath;
  int completition = conn_get_kpl_path(&nwpath, dir_handle, data, len, !mode);

  if (completition > -1) {
    char unname[256];
    strcpy(unname, build_unix_name(&nwpath, 2));
    if (get_volume_options(nwpath.volume) & VOL_OPTION_READONLY)
       return(mode ? -0x84 : -0x8a);
    if (mode) {
      XDPRINTF((5,0,"MKDIR dirname:%s:", unname));
      if (!mkdir(unname, 0777)) {
        if (act_umode_dir)
          chmod(unname, act_umode_dir);
        return(0);
      }
      if (errno == EEXIST)
        completition = -0xff; 
      else
        completition = -0x84; /* No Create Priv.*/  /* -0x9f Direktory Aktive */
    } else { /* rmdir */
      int  j = -1;
      while (++j < (int)anz_dirhandles){
        DIR_HANDLE  *dh=&(dir_handles[j]);
        if (dh->inode == completition && dh->f != (DIR*) NULL) {
          closedir(dh->f);
          dh->f = (DIR*)NULL;
        }
      }
      XDPRINTF((5,0,"RMDIR dirname:%s:", unname));

      if (!rmdir(unname)) {
        NW_DIR *d=&(dirs[0]);
        j = 0;
        while (j++ < (int)used_dirs){
          if (d->inode == completition) d->inode = 0;
          d++;
        }
        j = -1;
        while (++j < (int)anz_dirhandles){
          DIR_HANDLE  *dh=&(dir_handles[j]);
          if (dh->inode == completition) free_dir_handle(j+1);
        }
        completition = 0;
      } else if (errno == EEXIST)
         completition = -0xa0;    /* dir not empty */
       else completition = -0x8a; /* No privilegs  */
    }
  }
  return(completition);
}

int mv_file(int qdirhandle, uint8 *q, int qlen,
            int zdirhandle, uint8 *z, int zlen)
{
  NW_PATH quellpath;
  NW_PATH zielpath;
  int completition=conn_get_kpl_path(&quellpath, qdirhandle, q, qlen, 0);
  if (completition > -1) {
    completition=conn_get_kpl_path(&zielpath,    zdirhandle, z, zlen, 0);
    if (completition > -1) {
      int optq = get_volume_options(quellpath.volume);
      int optz = get_volume_options(zielpath.volume);
      if      ((optq & VOL_OPTION_IS_PIPE) ||
               (optz & VOL_OPTION_IS_PIPE)) completition = -0x8b;
      else if ((optq & VOL_OPTION_READONLY) ||
               (optz & VOL_OPTION_READONLY)) completition = -0x8b;
    }
    if (completition > -1){
      char unquelle[256];
      char unziel[256];
      strcpy(unquelle, build_unix_name(&quellpath,0));
      strcpy(unziel,   build_unix_name(&zielpath,0));
      if (entry8_flags & 0x4)  /* new: 20-Nov-96 */
        completition = unx_mvfile_or_dir(unquelle, unziel);
      else
        completition = unx_mvfile(unquelle, unziel);
      switch (completition) {
        case   0      : break;
        case   EEXIST : completition = -0x92; break; /* allready exist */
        case   EXDEV  : completition = -0x9a; break; /* cross device   */
        case   EROFS  : completition = -0x8b; break; /* no rights      */
        default       : completition = -0xff;
      }
    }
  }
  return(completition);
}

int mv_dir(int dir_handle, uint8 *q, int qlen,
                           uint8 *z, int zlen)
{
  NW_PATH quellpath;
  NW_PATH zielpath;
  int completition=conn_get_kpl_path(&quellpath, dir_handle, q, qlen, 0);
  if (completition > -1){
#if 1
    /* I do not know anymore why I did these ??? */
    memcpy(&zielpath, &quellpath, sizeof(NW_PATH));
    strmaxcpy(zielpath.fn, z, zlen);

    /* ----------- now I know again why I did these ----- */
    /* for example the novell rendir.exe does something like this
     * 0x0,0xd,0xf,0x0,0x7,'T','M','P',':','\','I','I',0x2,'K','K'
     * no dirhandle, qpath = fullpath, zpath = only name
     */
#else
    /* and NOT these, perhaps this will also be ok ?! -- TODO -- */
    completition=conn_get_kpl_path(&zielpath, dir_handle, z, zlen, 0);
#endif
    if (completition > -1) {
      int optq = get_volume_options(quellpath.volume);
      int optz = get_volume_options(zielpath.volume);
      if      ((optq & VOL_OPTION_IS_PIPE) ||
               (optz & VOL_OPTION_IS_PIPE)) completition = -0x8b;
      else if ((optq & VOL_OPTION_READONLY) ||
               (optz & VOL_OPTION_READONLY)) completition = -0x8b;
      /* patch from Sven Norinder <snorinder@sgens.ericsson.se> :09-Nov-96 */
      else if (optz & VOL_OPTION_DOWNSHIFT)
        down_fn(zielpath.fn);
      else
        up_fn(zielpath.fn);
    }
    if (completition > -1){
      int result;
      char unquelle[256];
      char unziel[256];
      strcpy(unquelle, build_unix_name(&quellpath, 0));
      strcpy(unziel,   build_unix_name(&zielpath,  0));

      result = unx_mvdir((uint8 *)unquelle, (uint8 *)unziel);
      XDPRINTF((2,0, "rendir result=%d, '%s'->'%s'",
                 result, unquelle, unziel));
      if (!result)
        completition = 0;
      else {
        if (result == EEXIST)
          completition=-0x92;    /* allready exist */
        else if (result == EXDEV)
          completition=-0x9a;    /* cross devices */
        else completition=-0x9c; /* wrong path */
      }
    }
  }
  return(completition);
}

static int change_dir_entry( NW_DIR *dir,     int volume,
                             uint8 *path,     ino_t inode,
                             int driveletter, int is_temp,
                             int new_entry,   int task)
{
  if (new_entry || (dir->inode && dir->is_temp != 2)) {
    int len;
    if (*path=='/' && *(path+1) != '\0') path++;
    len=strlen(path);
    if (dir->path) xfree(dir->path);
    dir->path=xmalloc(len+2);
    if (len) {
      memcpy(dir->path, path, len);
      if (dir->path[len-1] != '/') {
        *(dir->path + len) = '/';
        ++len;
      }
    } else {
      *(dir->path) = '/';
      ++len;
    }
    *(dir->path+len)  = '\0';
    dir->inode        = inode;
    dir->volume       = (uint8) volume;
    dir->timestamp    = time(NULL);
    if (driveletter > -1)  {
      dir->drive   = (uint8) driveletter;
      dir->task    = (uint8)task;
    } else {
      if (task     < (int)dir->task) dir->task = (uint8) task;
    }
    if (is_temp     > -1) dir->is_temp = (uint8) is_temp;
    return(0);
  } else {
    if (!dir->inode) return(-0x9b); /* wrong handle */
    else return(-0xfa); /* temp remap Error */
  }
}

void nw_exit_connect(void)
{
  if (connect_is_init) {
    init_file_module(-1);
  }
}

int nw_init_connect(void)
/* May be called when ever you want */
{
  uint8    *login   = (uint8*) "LOGIN/";
  NW_PATH  nwlogin;
  FILE *f= open_nw_ini();

  if (f != (FILE*) NULL){
    uint8  buff[256];
    struct stat stbuff;
    int            what;
    int k        = MAX_NW_DIRS;
    NW_DIR *d    = &(dirs[0]);
    strcpy((char*)nwlogin.path, (char*)login);
    nwlogin.fn[0]   = '\0';
    nwlogin.volume  = 0;

    while (k--) {
      if (connect_is_init)
        xfree(d->path);
      else
        d->path = NULL;
      d->volume    = 0;
      d->inode     = 0;
      d->is_temp   = 0;
      d->drive     = 0;
      d++;
    }

    init_file_module(-1);

    if (connect_is_init) {
      k = 0;
      while (k++ < anz_dirhandles) free_dir_handle(k);
    } else
      connect_is_init++;

    while (0 != (what = get_ini_entry(f, 0, buff, sizeof(buff)))) {
      if (what == 6) { /* version */
        if (2 != sscanf((char*)buff, "%d %x",
               &tells_server_version,
               &server_version_flags))
          server_version_flags=0;
      } else if (what == 8) { /* entry8_flags */
        entry8_flags = hextoi((char*)buff);
      } else if (what == 9) { /* umode */
        int  umode_dir, umode_file;
        if (2 == sscanf((char*)buff, "%o %o", &umode_dir, &umode_file)) {
          default_umode_dir  = umode_dir;
          default_umode_file = umode_file;
        }
      } else if (what == 10) { /* GID */
        default_gid = atoi((char*)buff);
      } else if (what == 11) { /* UID */
        default_uid = atoi((char*)buff);
      } else if (what == 30) { /* Burstmodus */
        int  recvsize, sendsize;
        int i= sscanf((char*)buff, "%i %i", &recvsize, &sendsize);
        if (i>0){
          max_burst_recv_size=recvsize;
          if (i>1)
            max_burst_send_size=sendsize;
        }
      } else if (50 == what) {
        init_nwfname(buff);
      } else if (68 == what) {  /* USE_MMAP */
        use_mmap=atoi(buff);
      } else if (what == 103) { /* Debug */
        get_debug_level(buff);
      }
    } /* while */
    nw_init_volumes(f);
    fclose(f);

    if (used_nw_volumes < 1) {
      errorp(1, "No Volumes defined. Look at ini file entry 1, Abort !!", NULL);
      return(-1);
    }
    if (get_volume_options(0) & VOL_OPTION_DOWNSHIFT)
      down_fn(nwlogin.path);
    if (stat(build_unix_name(&nwlogin, 0), &stbuff)) {
      errorp(1, "Stat error LOGIN Directory, Abort !!",
            "UnixPath=`%s`", build_unix_name(&nwlogin, 0));
      return(-1);
    }
    (void)change_dir_entry(&(dirs[0]), 0, nwlogin.path, stbuff.st_ino,
                       0, 0, 1, 0);
    /* first Handle must be known und must not be temp */
    /* and has no Drive-Character                      */
    used_dirs      = 1;
    anz_dirhandles = 0;
    return(0);
  } else return(-1);
}

int nw_free_handles(int task)
/*
 * if task== -1 then all is initialized
 * else the temp handles of the actual task
 * are deleted. I hope this is right. !??
 */
{
  if (task == -1)
    return(nw_init_connect());
  else {
    NW_DIR *d = &(dirs[0]);
    int     k = used_dirs;
    while (k--) {
      if (d->is_temp && d->task == task) {
      /* only actual task */
        xfree(d->path);
        d->volume    = 0;
        d->inode     = 0;
        d->is_temp   = 0;
        d->drive     = 0;
      }
      d++;
    }
    for (k=0; k < anz_dirhandles; k++){
      DIR_HANDLE *dh=&(dir_handles[k]);
      if (dh->f) {
        closedir(dh->f);
        dh->f=NULL;
      }
    }
    init_file_module(task);
  }
  return(0);
}

int xinsert_new_dir(int volume, uint8 *path, int inode, int drive, int is_temp, int task)
{
  int j              = 0;
  int    freehandle  = 0;
#if 0
  time_t lowtime     = time(NULL);
  int    timedhandle = 0;
#endif
  /* first look, whether drive is allready in use */
  for (j = (used_dirs) ? 1 : 0; j < (int)used_dirs; j++) {
    NW_DIR *d = &(dirs[j]);
    if (!d->inode)
       freehandle = j+1;
#if 0
    } else if (d->is_temp && d->timestamp < lowtime) {
      timedhandle = j+1;
      lowtime     = d->timestamp;
    }
#endif
  }
  if (!freehandle && used_dirs < MAX_NW_DIRS) freehandle = ++used_dirs;
#if 0
  if (!freehandle) freehandle = timedhandle;
#endif
  if (freehandle){
    (void)change_dir_entry(&(dirs[freehandle-1]),
          volume, path, inode,
          drive, is_temp, 1, task);
    while (used_dirs > freehandle && !dirs[used_dirs-1].inode) used_dirs--;
    return(freehandle);
  } else return(-0x9d);  /* no dir Handles */
}

int insert_new_dir(NW_PATH *nwpath, int inode, int drive, int is_temp, int task)
{
 return(xinsert_new_dir(nwpath->volume, nwpath->path,
                       inode, drive, is_temp, task));
}


int nw_search(uint8 *info, uint32 *fileowner,
              int dirhandle, int searchsequence,
              int search_attrib, uint8 *data, int len)

{
   NW_PATH nwpath;
   int     completition = conn_get_kpl_path(&nwpath, dirhandle, data, len, 0);
   XDPRINTF((5,0,"nw_search path:%s:, fn:%s:, completition:0x%x",
     nwpath.path, nwpath.fn, completition));
   if (completition > -1) {
      struct stat stbuff;
      if (get_dir_entry(&nwpath,
                        &searchsequence,
                        search_attrib,
                        &stbuff)){
         if ( S_ISDIR(stbuff.st_mode) ) {
           get_dir_attrib((NW_DIR_INFO*)info, &stbuff,
                   &nwpath);
         } else {
           get_file_attrib((NW_FILE_INFO*)info, &stbuff,
                &nwpath);
         }
         if (fileowner) *fileowner = get_file_owner(&stbuff);
         return(searchsequence);
      } else return(-0xff); /* not found */
   } else return(completition); /* wrong path */
}

int nw_dir_get_vol_path(int dirhandle, uint8 *path)
/* returns volume or error ( < 0) */
{
  int result;
  NW_DIR *dir    = (dirhandle > 0 && dirhandle <= used_dirs)
                     ? &(dirs[dirhandle-1])
                     : NULL;
  if (dir && dir->inode) {
    int llen       = strlen(dir->path);
    uint8  *p      = path+llen;
    result         = dir->volume;
    memcpy(path, dir->path, llen+1);
    if (llen && *(p-1) == '/')
        *(p-1) = '\0';
    if (result < 0) result = -0x98; /* wrong volume */
  } else result = -0x9b;
  return(result);
}

int nw_dir_search(uint8 *info,
              int dirhandle, int searchsequence,
              int search_attrib, uint8 *data, int len)

{
   NW_PATH nwpath;
   int   completition=-0x9c;
   build_path(&nwpath, data, len, 0);
   if (dirhandle > 0 && --dirhandle < anz_dirhandles){
     DIR_HANDLE *dh = &(dir_handles[dirhandle]);
     struct stat stbuff;
     if (dh->vol_options & VOL_OPTION_DOWNSHIFT) {
       down_fn(nwpath.fn);
     } else {
       up_fn(nwpath.fn);
     }
     if (get_dh_entry(dh,
                      nwpath.fn,
                      &searchsequence,
                      search_attrib,
                      &stbuff)){
       if ( S_ISDIR(stbuff.st_mode) ) {
         get_dir_attrib((NW_DIR_INFO*)info,   &stbuff,
              &nwpath);
       } else {
         get_file_attrib((NW_FILE_INFO*)info, &stbuff,
              &nwpath);
       }
       return(searchsequence);
     } else return(-0xff);     /* not found */
   } else return(completition); /* wrong path */
}

int nw_alloc_dir_handle( int    dir_handle,  /* source directory handle   */
                         uint8  *data,       /* source path               */
                         int    len,         /* pathlen                   */
                         int    driveletter, /* A .. Z normal             */
                         int    is_temphandle, /* temp handle         = 1 */
                                               /* special temp handle = 2 */
                         int    task)          /* Prozess Task            */
{
   NW_PATH nwpath;
   int inode=conn_get_kpl_path(&nwpath, dir_handle, data, len, 1);
   if (inode > -1)
     inode = insert_new_dir(&nwpath, inode, driveletter, is_temphandle, task);
   XDPRINTF((2,0,"Allocate %shandle:%s, Qhandle=%d, drive=%d, Task=%d, result=0x%x",
       (is_temphandle) ? "Temp" : "Perm", conn_get_nwpath_name(&nwpath),
               dir_handle, driveletter, task, inode));
   return(inode);
}

int nw_open_dir_handle( int        dir_handle,
                        uint8      *data,     /* extra path  */
                        int        len,       /* len of DATA */

                        int        *volume,   /* Volume      */
                        int        *dir_id,
                        int        *searchsequence)

/*
 * Routine returns handle to use in searchroutines.
 * RETURN=errcode ( <0 ) or eff. ACCES Rights
*/

{
   NW_PATH nwpath;
   int completition = conn_get_kpl_path(&nwpath, dir_handle, data, len, 1);
   if (completition > -1) {
     XDPRINTF((5,0,"NW_OPEN_DIR: completition = 0x%x; nwpath= %s",
           (int)completition, conn_get_nwpath_name(&nwpath) ));

     completition = new_dir_handle((ino_t)completition, &nwpath);
     if (completition > -1) {
       struct stat stb;
       DIR_HANDLE *dh  = &(dir_handles[completition-1]);
       s_stat(dh->unixname, &stb, NULL);
       *volume         = dh->volume;
       *dir_id         = completition;
       *searchsequence = MAX_U16;
       completition    = (uint8)un_nw_rights(&stb);  /* eff. rights */
     }
     XDPRINTF((5,0,"NW_OPEN_DIR_2: completition = 0x%x",
                    completition));
   } else {
     XDPRINTF((4,0,"NW_OPEN_DIR failed: completition = -0x%x", -completition));
   }
   return(completition);
}

int nw_free_dir_handle(int dir_handle, int task)
{
  if (dir_handle && --dir_handle < (int)used_dirs) {
    NW_DIR *d=&(dirs[dir_handle]);
    XDPRINTF((2,0,"free dhandle:%d, task=%d, d->inode=0x%x, used_dirs=%d",
            dir_handle+1, task, d->inode, used_dirs));
    if (!d->inode
#if 0
    || d->task != task
#endif
    ) return(-0x9b); /* wrong handle */
    else {
      d->inode = 0;
      xfree(d->path);
    }
    return(0);
  } else return(-0x9b); /* wrong handle */
}

int alter_dir_handle(int targetdir, int volume, uint8 *path,
                     int inode, int task)
/* targetdir will be changed */
{
  if (targetdir > 0 && --targetdir < used_dirs
    && dirs[targetdir].is_temp != 2) {
    /* do not change special temphandles */
    XDPRINTF((5,0,"Change dhandle:%d(%d) -> '%d:%s'", targetdir+1, task,
              volume, path));
    return(change_dir_entry(&dirs[targetdir],
                    volume, path, inode,
                    -1, -1, 0, task));
    /* here the existing handle is only modified */
  } else return(-0x9b); /* BAD DIR Handle */
}


int nw_set_dir_handle(int targetdir, int dir_handle,
                          uint8 *data, int len, int task)
/* targetdirs gets path of dirhandle + data */
{
  NW_PATH nwpath;
  int inode = conn_get_kpl_path(&nwpath, dir_handle, data, len, 1);
  if (inode > -1)
    inode = alter_dir_handle(targetdir, nwpath.volume, nwpath.path,
                              inode, task);
  return(inode);  /* invalid PATH */
}

int nw_get_directory_path(int dir_handle, uint8 *name)
{
  int     result   = -0x9b;
  name[0] = '\0';
  if (dir_handle > 0 && --dir_handle < (int)used_dirs
                     && dirs[dir_handle].inode) {
    int volume = dirs[dir_handle].volume;
    if (volume > -1 && volume < used_nw_volumes){
      result=sprintf((char*)name, "%s:%s", nw_volumes[volume].sysname, dirs[dir_handle].path);
      if (name[result-1] == '/') name[--result] = '\0';
      up_fn(name);
    } else result = -0x98;
  }
  XDPRINTF((5,0,"nw_get_directory_path:%s: Handle=%d, result=0x%x", name, dir_handle+1, result));
  return(result);
}

int nw_get_vol_number(int dir_handle)
/* Get Volume Nummmer with Handle */
{
  int     result   = -0x9b; /* wrong handle */
  if (dir_handle > 0 && --dir_handle < (int)used_dirs
          && dirs[dir_handle].inode  )  {
    result = dirs[dir_handle].volume;
    if (result < 0 || result >= used_nw_volumes) result = -0x98; /* wrong volume */
  }
  XDPRINTF((5,0,"nw_get_vol_number:0x%x: von Handle=%d", result, dir_handle+1));
  return(result);
}

int nw_get_eff_dir_rights(int dir_handle, uint8 *data, int len, int modus)
/* modus 0=only_dir, 1=dirs and files */
{
  char          unname[256];
  struct stat   stbuff;
  NW_PATH       nwpath;
  int completition = conn_get_kpl_path(&nwpath, dir_handle, data, len,
                     (modus) ? 0 : 1);
  if (completition < 0) return(completition);
  strcpy(unname, build_unix_name(&nwpath, 0));
  if (s_stat(unname, &stbuff, NULL) ||
    (!modus && !S_ISDIR(stbuff.st_mode)) ) {
    completition = -0x9c;
  } else completition=un_nw_rights(&stbuff); /* eff. rights */
  return(completition);
}

int nw_creat_open_file(int dir_handle, uint8 *data, int len,
            NW_FILE_INFO *info, int attrib, int access,
            int creatmode, int task)
/*
 * creatmode: 0 = open | 1 = creat | 2 = creatnew  & 4 == save handle
 * attrib ??
 * access: 0x1=read, 0x2=write
 */
{
  NW_PATH nwpath;
  int completition = conn_get_kpl_path(&nwpath, dir_handle, data, len, 0);
  if (completition > -1) {
     struct stat stbuff;
     completition=file_creat_open(nwpath.volume, (uint8*)build_unix_name(&nwpath, 0),
                      &stbuff, attrib, access, creatmode, task);

     if (completition > -1)
       get_file_attrib(info, &stbuff, &nwpath);
  }
  return(completition);
}

static int s_nw_scan_dir_info(int dir_handle,
                       uint8 *data, int len, uint8 *subnr,
                       uint8 *subname, uint8 *subdatetime,
                       uint8 *owner, uint8 *wild)
{
  int volume;
  int searchsequence;
  int dir_id;
  int rights = nw_open_dir_handle(dir_handle, data, len,
                           &volume, &dir_id, &searchsequence);

  if (rights > -1) {
    DIR_HANDLE *dh = &(dir_handles[dir_id-1]);
    struct stat stbuff;
    uint16 dirsequenz     = GET_BE16(subnr);
    uint16 aktsequenz     = 0;
    uint8  dirname[256];
    if (!dirsequenz) dirsequenz++;

    if (dh->vol_options & VOL_OPTION_DOWNSHIFT) {
      down_fn(wild);
    } else {
      up_fn(wild);
    }

    strcpy((char*)dirname, (char*)wild);
    XDPRINTF((5,0,"SCAN_DIR: rights = 0x%x, subnr = %d",
                (int)rights, (int)GET_BE16(subnr)));

    if (*dirname) {

      while ( get_dh_entry( dh,
                            dirname,
                            &searchsequence,
                            0x10,
                            &stbuff) ) {

        XDPRINTF((5,0,"SCAN_DIR: von %s, found %s:", dh->unixname, dirname));
        if (++aktsequenz == dirsequenz) { /* actual found */
          U16_TO_BE16(aktsequenz, subnr);
          up_fn(dirname);
          strncpy((char*)subname, (char*)dirname, 16);
          U32_TO_BE32(get_file_owner(&stbuff),  owner);
          un_date_2_nw(stbuff.st_mtime, subdatetime,   1);
          un_time_2_nw(stbuff.st_mtime, subdatetime+2, 1);
          return(un_nw_rights(&stbuff));
        }
        strcpy((char*)dirname, (char*)wild);
      } /* while */
    } else {
      *(dh->kpath)   = '.';
      *(dh->kpath+1) = '\0';
      if (!s_stat(dh->unixname, &stbuff, NULL)) {
        U16_TO_BE16(1, subnr);
        memset(subname, 0, 16);
        U32_TO_BE32(get_file_owner(&stbuff),  owner);
        un_date_2_nw(stbuff.st_mtime, subdatetime,   1);
        un_time_2_nw(stbuff.st_mtime, subdatetime+2, 1);
        return(un_nw_rights(&stbuff));
      }
    }
    /* return(-0x9c);  NO MORE INFO */
    return(-0xff);
  }
  return(rights);
}

int nw_scan_dir_info(int dir_handle, uint8 *data, int len, uint8 *subnr,
                       uint8 *subname, uint8 *subdatetime, uint8 *owner)
{
  int   k     = len;
  uint8 *p    = data+len;
  uint8 dirname[256];
  while (k) {
    uint8 c = *--p;
    if (c == '/' || c == '\\' || c == ':') {
      p++;
      break;
    }
    --k;
  }
  if (len && k < len) {
    strmaxcpy(dirname, p, len-k);
    len = k;
  } else dirname[0] = '\0';
  XDPRINTF((7, 0, "nw_scan_dir_info, dirname=`%s`, len=%d, k=%d",
    dirname, len , k));
  return(s_nw_scan_dir_info(dir_handle, data, len, subnr,
                     subname, subdatetime, owner, dirname));
}



void get_dos_file_attrib(NW_DOS_FILE_INFO *f,
                               struct stat *stb,
                               int         volume,
                               uint8        *path)
{
  uint8  spath[14];
  uint32 nw_owner=get_file_owner(stb);
  int voloptions=get_volume_options(volume);
  f->namlen=min(strlen((char*)path), 12);
  strmaxcpy(spath, path, 12);
  up_fn(spath);
  strncpy((char*)f->name, (char*)spath, f->namlen);
  /* Attribute */
  /* 0x20   Archive Flag */
  /* 0x80   Sharable     */
  if (voloptions & VOL_OPTION_IS_PIPE)
    f->attributes[0] = FILE_ATTR_SHARE;
  else
    f->attributes[0] = (uint8) un_nw_attrib(stb, 0, 0);
  un_date_2_nw(stb->st_mtime, f->created.date, 0);
  un_time_2_nw(stb->st_mtime, f->created.time, 0);
  U32_TO_BE32(nw_owner, f->created.id);

  un_date_2_nw(stb->st_mtime, f->updated.date, 0);
  un_time_2_nw(stb->st_mtime, f->updated.time, 0);
  U32_TO_BE32(nw_owner, f->updated.id);

  un_date_2_nw(stb->st_atime, f->last_access_date, 0);
  U32_TO_32(stb->st_size, f->size);
}

void get_dos_dir_attrib(NW_DOS_DIR_INFO *f,
                                struct stat *stb,
                                int   volume,
                                uint8 *path)
{
  uint8 spath[14];
  f->namlen=min(strlen((char*)path), 12);
  strmaxcpy(spath, path, 12);
  up_fn(spath);
  strncpy((char*)f->name, (char*)spath, f->namlen);
  f->attributes[0] = (uint8) un_nw_attrib(stb, 0, 0);
  un_date_2_nw(stb->st_mtime, f->created.date,0);
  un_time_2_nw(stb->st_mtime, f->created.time,0);
  U32_TO_BE32(get_file_owner(stb), f->created.id);
  un_date_2_nw(stb->st_mtime, f->modify_date, 0);
  un_time_2_nw(stb->st_mtime, f->modify_time, 0);
  U32_TO_BE32(MAX_U32, f->max_space);
}


int nw_scan_a_directory(uint8   *rdata,
                        int     dirhandle,
                        uint8   *data,
                        int     len,
                        int     searchattrib,
                        uint32  searchbeg)   /* 32 bit */
{
  NW_PATH nwpath;
  int     completition = conn_get_kpl_path(&nwpath, dirhandle, data, len, 0);
  XDPRINTF((5,0,"nw_scan_a_directory path:%s:, fn:%s:, completition:0x%x",
    nwpath.path, nwpath.fn, completition));
  if (completition > -1) {
     struct stat stbuff;
     int searchsequence = (searchbeg == MAX_U32) ? MAX_U16 : searchbeg;
     if (get_dir_entry(&nwpath,
                       &searchsequence,
                       searchattrib,
                       &stbuff)){

       NW_SCAN_DIR_INFO *scif = (NW_SCAN_DIR_INFO*)rdata;
       memset(rdata, 0, sizeof(NW_SCAN_DIR_INFO));
       U32_TO_BE32((uint32)searchsequence, scif->searchsequence);

       XDPRINTF((5,0, "nw_scan_a_directory = %s, uid=%d, gid=%d",
         conn_get_nwpath_name(&nwpath),
         stbuff.st_uid, stbuff.st_gid));

       if ( S_ISDIR(stbuff.st_mode)) {
          get_dos_dir_attrib(&(scif->u.d), &stbuff,
                             nwpath.volume, nwpath.fn);
       } else {
          get_dos_file_attrib(&(scif->u.f), &stbuff,
                             nwpath.volume,  nwpath.fn);
       }
       return(sizeof(NW_SCAN_DIR_INFO));
     } else return(-0xff); /* not found */
  } else return(completition); /* wrong path */
}

int nw_scan_a_root_dir(uint8   *rdata,
                       int     dirhandle)
{
  NW_PATH nwpath;
  uint8   data[2];
  int     completition = conn_get_kpl_path(&nwpath, dirhandle, data, 0, 1);
  XDPRINTF((5,0,"nw_scan_a_root_directory_2 path:%s:, fn:%s:, completition:0x%x",
    nwpath.path, nwpath.fn, completition));
  if (completition > -1) {
    struct stat stbuff;
    if (!s_stat(build_unix_name(&nwpath, 2), &stbuff, NULL)) {
      NW_DOS_DIR_INFO  *d=(NW_DOS_DIR_INFO*)rdata;
      memset(rdata, 0, sizeof(NW_DOS_DIR_INFO));
      get_dos_dir_attrib(d, &stbuff, nwpath.volume, nwpath.fn);
      return(sizeof(NW_DOS_DIR_INFO));
    } else return(-0xff); /* not found */
  } else return(completition); /* wrong path */
}

static int my_match(uint8 *s, uint8 *p)
{
  int len=0;
  int dot=0;
  for (; *s && *p; s++,p++) {
    if (len == 12) return(0);
    if (*s == '.') {
      if (dot) return(0);
      dot++;
    } else if (dot) {
      if (dot++ > 3) return(0);
    } else if (len == 8) return(0);
    if (!ufn_imatch(*s, *p))
      return(0);
    ++len;
  }
  return( ((!*s) && (*p=='/' || *p == '\0')) ? len : 0);
}

static int get_match(uint8 *unixname, uint8 *p)
{
  DIR       *d;
  uint8     *pp;
#if 0
  uint8     *p1;
  int       inode=0;
#endif
  if (!p || !*p)   return(1);
  *p        = '\0';
  pp=p+1;
  while (*pp == '/') ++pp;
#if 0
  p1=strchr(pp, '/');
  if (!p1) p1=pp+strlen(pp);
  if (p1 > pp+4 && *(p1-4) == '.'
                && *(p1-3) == '_'
                && *(p1-2) == '_'
                && *(p1-1) == '_' ) {
    *(p1-4) = '\0';
    inode=atoi(pp);
    *(p1-4) = '.';
  }
#endif
  if (NULL != (d=opendir(unixname))) {
    struct dirent *dirbuff;
    XDPRINTF((10, 0, "opendir OK unixname='%s' pp='%s'", unixname, pp));
    *p      = '/';
    while ((dirbuff = readdir(d)) != (struct dirent*)NULL){
      int len;
      if (dirbuff->d_ino) {
        XDPRINTF((10, 0, "get match found d_name='%s'", dirbuff->d_name));
        if (
#if 0
        (dirbuff->d_ino ==inode) ||
#endif
        0 != (len=my_match(dirbuff->d_name, pp))) {
          memcpy(pp, dirbuff->d_name, len);
          XDPRINTF((10, 0, "get match, match OK"));
          closedir(d);
          return(get_match(unixname, pp+len));
        }
      }
    }
    closedir(d);
  } else {
    XDPRINTF((2, 0, "DOS get_match opendir failed unixname='%s'", unixname));
    *p='/';
  }
  return(0);
}

void mangle_dos_name(NW_VOL *vol, uint8 *unixname, uint8 *pp)
{
  struct stat stb;
  if (!s_stat(unixname, &stb, NULL)) /* path is ok I hope */
    return;
  get_match(unixname, pp-1);
}


