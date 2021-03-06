/* connect.c  15-Apr-00 */
/* (C)opyright (C) 1993-2000, Martin Stover, Marburg, Germany
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

/* history since 01-Sep-00
 * mst:01-Sep-00: pcz:added real unix rights patch from Przemyslaw Czerpak
 *
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

int  default_uid=-1;
int  default_gid=-1;


#ifdef TEST_FNAME
  static int test_handle=-1;
#endif

static int  default_umode_dir  = 0751;
static int  default_umode_file = 0640;
static int  act_umode_dir=0;
static int  act_umode_file=0;

#include "nwfname.h"
#include "nwvolume.h"
#include "nwshare.h"
#include "nwattrib.h"
#include "trustee.h"
#include "nwfile.h"
#include "nwconn.h"
#include "namspace.h"
#include "connect.h"


typedef struct {
   int    dev;          /* unix dev           */
   ino_t  inode;        /* unix inode         */
   time_t timestamp;    /* Zeitmarke          */
   uint8  *path;        /* path ab Volume     */
   uint8  volume;       /* Welches Volume     */
   uint8  is_temp;      /* 0:perm. 1:temp 2: spez. temp */
   uint8  drive;        /* driveletter        */
   uint8  task;         /* actual task        */
} NW_DIR;


NW_DIR    dirs[MAX_NW_DIRS];
int       used_dirs=0;
int       act_uid=-1;     /* unix uid 0=root */
int       act_gid=-1;     /* unix gid */
int       act_obj_id=0L;  /* mars_nwe UID, 0=not logged in, 1=supervisor  */
int       act_id_flags=0; /* &1 == supervisor equivalence !!! */
int       entry8_flags=0; /* special flags, see examples nw.ini, entry 8 */
int       entry31_flags=0; /* not used yet */

static gid_t *act_grouplist=NULL;  /* first element is counter !! */

static int       connect_is_init = 0;

#define MAX_DIRHANDLES    80
#define EXPIRE_COST	  86400 /* Is one day enough? :) */

typedef struct {
  DIR    *f;
  char   unixname[256]; /* full unixname             */
  int    dev;           /* Unix dev                  */
  ino_t  inode;         /* Unix Inode                */
  time_t timestamp;     /* last allocation           */
  char   *kpath;        /* one char after unixname   */
  int    vol_options;   /* searchoptions             */
  int    volume;        /* Volume Number	     */
  int    sequence;      /* Search sequence           */
  off_t  dirpos;        /* Current pos in unix dir   */
  int    no_search_trustee; /* mst:15-Apr-00 */
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
  char *p;
  if (volume < 0 || volume >= used_nw_volumes
                 || !nw_volumes[volume].unixnamlen
                 || nw_volumes[volume].unixnamlen > 250) {
    errorp(10, "build_unix_name", "volume=%d not ok\n", volume);
    xstrcpy(unixname, "Z/Z/Z/Z"); /*  */
    return(unixname);
  }
  memcpy(unixname, (char*)nw_volumes[volume].unixname,
                   nw_volumes[volume].unixnamlen ); /* first UNIXNAME VOLUME */

  p  = unixname + nw_volumes[volume].unixnamlen;
  p += strmaxcpy(p, (char*)nwpath->path, sizeof(unixname) - nw_volumes[volume].unixnamlen -1);  /* now the path */

  if ( (!(modus & 1)) && nwpath->fn[0])
    strmaxcpy(p, (char*)nwpath->fn, sizeof(unixname) - (int)(p-unixname) -1); /* and now fn  */
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


static int new_dir_handle(struct stat *stb, NW_PATH *nwpath)
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
    } else if (dh->dev == stb->st_dev && dh->inode == stb->st_ino
                && dh->volume == nwpath->volume){
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
  dh->kpath = dh->unixname + xstrcpy(dh->unixname, build_unix_name(nwpath, 0));
  if (dh->f) {
    closedir(dh->f);
    dh->f=NULL;
  }

  if ( ( !(dh->no_search_trustee=tru_eff_rights_exists(nwpath->volume, dh->unixname, stb, TRUSTEE_F))) 
       || (dh->no_search_trustee & TRUSTEE_T) ) {
    if ((dh->f=opendir(dh->unixname)) == (DIR*)NULL) {
      seteuid(0);
      dh->f=opendir(dh->unixname);
      reseteuid();
    }
  }

  if (NULL != dh->f) {
    dh->volume      = nwpath->volume;
    dh->vol_options = nw_volumes[dh->volume].options;
    dh->dev         = stb->st_dev;
    dh->inode       = stb->st_ino;
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
  act_gid        = default_gid;
  act_uid        = default_uid;
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
    if (gid < 0  && uid < 0) {
      /* don't print error */
      gid = act_gid;
      uid = act_uid;
    }
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

/* pcz:01-Sep-00 */ 
int get_unix_access_rights(struct stat *stb, uint8 *unixname)
/* returns F_OK, R_OK, W_OK, X_OK  */
/* ----- old ----------------------*/
/* ORED with 0x10 if owner access  */
/* ORED with 0x20 if group access  */
/* ----- corrent-------------------*/
/* ORED with 0x20 if W_OK  access  */
/* --------------------------------*/
{
  int mode=0;
  uid_t ruid, euid, rgid;

  ruid=getuid();
  euid=geteuid();
  rgid=getgid();

  setreuid(act_uid,0);
  setgid(act_gid);
  
  if (!access(unixname, F_OK)) {

    if (!access(unixname, R_OK))
      mode |= R_OK;
    if (!access(unixname, W_OK))
      /* mode |= W_OK; */
      mode |= W_OK | 0x20;
    if (!access(unixname, X_OK))
      mode |= X_OK;

    /* mode |= get_unix_eff_rights(stb) & ~(R_OK|W_OK|X_OK); */
  }
  setgid(rgid);
  setreuid(ruid, euid);

  return(mode);
}


int get_unix_eff_rights(struct stat *stb)
/* returns F_OK, R_OK, W_OK, X_OK  */
/* ORED with 0x10 if owner access  */
/* ORED with 0x20 if group access  */
{
  int mode = 0;
  if (!act_uid)
    return(0x10 | R_OK | W_OK | X_OK) ;  /* root */
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



/* next routine is called after new login */
void set_nw_user(int gid, int uid,
                 int id_flags,
                 uint32 obj_id,   uint8 *objname,
                 int unxloginlen, uint8 *unxloginname,
                 int grpcount,    uint32 *grps)
{
  uint8 unxlogin[20];
  strmaxcpy(unxlogin, unxloginname, min(unxloginlen, sizeof(unxlogin)-1));
  nwconn_set_program_title(objname);
  set_guid(gid, uid);
  act_obj_id   = obj_id;
  act_id_flags = id_flags;
  XDPRINTF((5, 0, "actual obj_id is set to 0x%x", obj_id));
  if ((act_id_flags&1) && obj_id != 1) {
    XDPRINTF((1, 0, "logged in user %s (%s) has supervisor privilegs", objname, unxlogin));
  }
  nw_setup_vol_opts(act_gid, act_uid,
                    act_umode_dir, act_umode_file,
                    unxlogin);
  tru_init_trustees(grpcount, grps);
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
    slprintf(volname, sizeof(volname)-1, "<%d=NOT-OK>", (int)p->volume);
  } else xstrcpy(volname, (char*)nw_volumes[p->volume].sysname);
  slprintf(nwpathname, sizeof(nwpathname)-1, "%s:%s%s", volname, p->path, p->fn);
  return(nwpathname);
}

/* new from Andrew Sapozhnikov <sapa@hq.icb.chel.su>
 * added in 0.99.pl7, removes old x_str_match routine
 * Do not work for namespace routines, see namespace.c !!
 */
/* NW3.12/MSDOS-like filename matching. *
 * Wildcards '*' and '?' allowed.	*/
static int x_str_match(uint8 *s, uint8 *p, int soptions)
{
  while (*p) {
    switch (*p) {
      case 0xbf:
      case '?': if (*s && *s != '.') s++;
      		p++;
      		break;
#if 0
      case 0xaa:
      case '*': while (*s && *s != '.') s++;
		while (*p && *p != '.' && *p != 0xae) p++;
		break;
#else
      /* changed 27-May-98, 0.99.pl10 to handle '*' correct,
       * and to handle '*xy.xyz' in a `better` (non DOS) way.
       * now I know again why my old routine was so terrible ;-)
       */

      case 0xaa:
      case '*': ++p;
                if (*p) {
                  while (*s && *s != '.') {
                    if (x_str_match(s, p, soptions))
                      return(1);
                    else
                      s++;
                  }
                } else {
                  if (*(p-1) == '*') /* last star '*' match everything */
                    return(1);
                  else  /* but  0xaa not */
                    while (*s && *s != '.') s++;
                }
		break;
#endif

      case 0xae:
      case '.': if (*s) {
      		  if(*s != '.') return 0;
		  else s++;
                }
		p++;
		break;
      default : if (! ((soptions & VOL_OPTION_IGNCASE) ?
      			dfn_imatch(*s, *p) : (*s == *p)) ) return 0;
      		p++;
      		s++;

    }
  }
  return (*s ? 0 : 1);
}

int fn_dos_match(uint8 *s, uint8 *p, int options)
{
  uint8 *ss=s;
  int   len=0;
  int   pf=0;
  uint8 p_buff[200];
  uint8 *pp=p_buff;

  for (; *p; p++){
    if (*p != 0xff)
      *pp++=*p;
  }
  *pp='\0';
  p=p_buff;

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
  DIR            *f=NULL;
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
  xstrcpy(entry,  (char*)nwpath->fn);

  nwpath->fn[0] = '\0';
  xstrcpy(xkpath, build_unix_name(nwpath, 1|2));

  XDPRINTF((5,0,"func_search_entry attrib=0x%x path:%s:, xkpath:%s:, entry:%s:",
        attrib, nwpath->path, xkpath, entry));

  if ( (!stat(xkpath, &(fs->statb)))
    && !tru_eff_rights_exists(volume, xkpath, &(fs->statb), TRUSTEE_F)) {
    if ((f=opendir(xkpath)) == (DIR*)NULL) {
      seteuid(0);
      f=opendir(xkpath);
      reseteuid();
    }
  }

  if (f != (DIR*)NULL) {
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
                 ( (!strcmp((char*)dname, (char*)entry))
                 || fn_dos_match(dname, entry, soptions)));
        if (okflag) {
          *kpath = '\0';
          strmaxcpy(kpath, (char*)name, sizeof(xkpath) - (int)(kpath-xkpath) -1 );
          if (!s_stat(xkpath, &(fs->statb), NULL)) {
            okflag = (  ( ( (fs->statb.st_mode & S_IFMT) == S_IFDIR) &&  (attrib & 0x10))
                  ||    ( ( (fs->statb.st_mode & S_IFMT) != S_IFDIR) && !(attrib & 0x10)));
            if (okflag){
              xstrcpy(nwpath->fn, (char*)dname);
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

/* returns 0 if OK and errcode if not OK */
{
  struct dirent* dirbuff;
  DIR            *f=NULL;
  int            okflag=-0xff; /* not found */
  char           xkpath[256];
  uint8          entry[256];
  int            volume = nwpath->volume;
  int            soptions;
  int            akt_sequence=0;
  int            no_search_trustee = 0;

  if (volume < 0 || volume >= used_nw_volumes) return(-0x98); /* something wrong */
  else  soptions = nw_volumes[volume].options;
  xstrcpy(entry,  (char*)nwpath->fn);
  nwpath->fn[0] = '\0';
  xstrcpy(xkpath, build_unix_name(nwpath, 1|2));
  XDPRINTF((5,0,"get_dir_entry attrib=0x%x path:%s:, xkpath:%s:, entry:%s:",
                          attrib, nwpath->path, xkpath, entry));

  if ( (!stat(xkpath, statb))
    && ( (!(no_search_trustee = tru_eff_rights_exists(volume, xkpath, statb, TRUSTEE_F))) 
       || (no_search_trustee&TRUSTEE_T)) ) {
    if ((f=opendir(xkpath)) == (DIR*)NULL) {
      seteuid(0);
      f=opendir(xkpath);
      reseteuid();
    }
  }

  if (f != (DIR*)NULL) {
    char *kpath=xkpath+strlen(xkpath);
    *kpath++ = '/';
    if (*sequence == MAX_U16) *sequence = 0;

    while (akt_sequence++ < *sequence) {
      if (NULL == readdir(f)) {
        closedir(f);
        return(-0xff);
      }
    }

    while ((dirbuff = readdir(f)) != (struct dirent*)NULL){
      okflag = -0xff;
      (*sequence)++;
      if (dirbuff->d_ino) {
        uint8 *name=(uint8*)(dirbuff->d_name);
        uint8 dname[256];
        xstrcpy(dname, name);
        unix2doscharset(dname);
        okflag = ((name[0] != '.' &&
                 ( (!strcmp((char*)dname, (char*)entry))
                 || fn_dos_match(dname, entry, soptions)))) ? 0 : -0xff;
        if (!okflag) {
          *kpath = '\0';
          strmaxcpy(kpath, (char*)name, sizeof(xkpath) - (int)(kpath-xkpath) -1);
          if (!s_stat(xkpath, statb, NULL)) {
            okflag = ((  ( ( (statb->st_mode & S_IFMT) == S_IFDIR) &&  (attrib & 0x10))
                  ||    ( ( (statb->st_mode & S_IFMT) != S_IFDIR) && !(attrib & 0x10))))
                  ? 0 : -0xff;
            if (!okflag){
              if ( (!no_search_trustee) ||
                  !tru_eff_rights_exists(volume, xkpath, statb, TRUSTEE_T)) {

                if (soptions & VOL_OPTION_IS_PIPE)  {
                  statb->st_size  = 0x70000000|(statb->st_mtime&0xfffffff);
                }
                xstrcpy(nwpath->fn, (char*)dname);
                XDPRINTF((5,0,"FOUND=:%s: attrib=0x%x", nwpath->fn, statb->st_mode));
                break; /* ready */
              } else
                 okflag = -0xff;
            }
          } else okflag = -0xff;
        }
        XDPRINTF((6,0, "NAME=:%s: OKFLAG %d", name, okflag));
      }  /* if */
    } /* while */
    closedir(f);
  } else okflag=-0x89; /* no search rights */
  return(okflag);
}

static DIR *give_dh_f(DIR_HANDLE    *dh)
{
  if (!dh->f) {
    *(dh->kpath) = '\0';
    seteuid(0);
    dh->f  = opendir(dh->unixname);
    reseteuid();
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
                        int    size_search,
                        int    *sequence,
                        int    attrib,
                        char   *unixname,
                        int    size_unixname,
                        struct stat *statb)

/* returns 1 if OK and 0 if not OK */
{
  DIR            *f     = give_dh_f(dh);
  int            okflag = 0;

  if (f != (DIR*)NULL) {
    struct  dirent *dirbuff;
    uint8   entry[256];
    xstrcpy(entry, search);
    if ( (uint16)*sequence == MAX_U16)  *sequence = 0;

    if (*sequence < dh->sequence || ((long)dh->dirpos) < 0L) {
      dh->dirpos   = (off_t)0;
      dh->sequence = 0;
    }
    SEEKDIR(f, dh->dirpos);

    if (dh->sequence != *sequence) {
      while (dh->sequence < *sequence) {
        if (NULL == readdir(f)) {
          dh->dirpos = TELLDIR(f);
          release_dh_f(dh, 1);
          return(0);
        }
        dh->sequence++;
      }
      dh->dirpos = TELLDIR(f);
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
        okflag = (name[0] != '.' &&
                 ( (!strcmp((char*)dname, (char*)entry))
                 || fn_dos_match(dname, entry, dh->vol_options)));

        if (okflag) {
          strmaxcpy(dh->kpath, (char*)name,
                    sizeof(dh->unixname) - (int)(dh->kpath-dh->unixname) -1 );
          XDPRINTF((5,0,"get_dh_entry Name=%s unixname=%s",
                                  name, dh->unixname));

          if (!s_stat(dh->unixname, statb, NULL)) {
            okflag = ( (( (statb->st_mode & S_IFMT) == S_IFDIR) &&  (attrib & 0x10))
                     || (((statb->st_mode & S_IFMT) != S_IFDIR) && !(attrib & 0x10)));
            
            /* mst:15-Apr-00 */ 
            if (okflag && dh->no_search_trustee  
               && tru_eff_rights_exists(dh->volume, dh->unixname, statb, TRUSTEE_T))
              okflag=0;
            
            if (okflag){
              if (unixname)
                strmaxcpy(unixname, dh->unixname, size_unixname-1);
              strmaxcpy((char*)search, (char*)dname, size_search-1);
              break; /* ready */
            }
          } else okflag = 0;
        }
      }  /* if */
    } /* while */
    dh->kpath[0] = '\0';
    *sequence  = dh->sequence;
    dh->dirpos = TELLDIR(f);
    release_dh_f(dh, (dirbuff==NULL));
  } /* if */
  return(okflag);
}

static void conn_build_path_fn(uint8 *vol,
                               uint8 *path,
                               uint8 *fn,
                               int   *has_wild,
                               uint8 *data,
                               int   len,
			       int   lenn)

/* is called from build_path  */
{
   uint8  *p  = NULL;
   uint8  *p1 = path;
   *vol       = '\0';
   *has_wild  = 0;     /* no wild char */
   while (len-- && *data){
     if (*data == 0xae) *p1++ = '.';
     else if (*data == 0xaa|| *data == '*' ) {
       /* *p1++ = '*'; */
       *p1++ = *data;
       (*has_wild)++;
     } else if (*data == 0xbf|| *data == '?' ) {
       *p1++ = '?';
       (*has_wild)++;
     } else if (*data == '/' || *data == '\\') {
       *p1++ = '/';
       p = p1;
     } else if (*data == ':') { /* extract volume */
       int len = (int)(p1 - path);
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
       strmaxcpy((char*)fn, (char*)p, 255);
       *p = '\0';
     } else {         /* only filename */
       strmaxcpy((char*)fn, (char*)path, 255);
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
  if (len > 255) len = 255;
  conn_build_path_fn(vol,
                     path->path,
                     (only_dir) ? (uint8)NULL
                                : path->fn,
                     &(path->has_wild),
                     data, len, sizeof(path->fn));

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

static int nw_path_directory_is_ok(NW_PATH *nwpath, struct stat *stbuff)
/* returns UNIX inode of path  */
{
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
  if (!result && !s_stat(build_unix_name(nwpath, 1 | 2 ), stbuff, NULL)
      && S_ISDIR(stbuff->st_mode))
    return(stbuff->st_ino);
  else if (!result)
    result = -0x9c;   /* wrong path */
  XDPRINTF((4,0x10, "NW_PATH_OK failed:`%s`", conn_get_nwpath_name(nwpath)));
  return(result);
}

static int build_dir_name(NW_PATH *nwpath,     /* gets complete path     */
                           struct  stat *stbuff,
                           int     dir_handle)  /* search with dirhandle  */

/* return -completition code or inode */
{
   uint8      searchpath[256];
   uint8      *p=searchpath;
   uint8      *ppp=nwpath->path;
   int        completition=0;

   xstrcpy(searchpath, (char*)ppp);  /* save path */

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
         strmaxcpy((char*)ppp, (char*)dirs[dir_handle].path,
                  sizeof(nwpath->path) - (int)(ppp-nwpath->path) -1 );
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
         XDPRINTF((5,0,"in build_dir_name path=:%s:", nwpath->path));
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
         strmaxcpy(pp, nwpath->path, sizeof(unixname) - v->unixnamlen-1);
         if (fnlen)
           strmaxcpy(pp+pathlen, nwpath->fn,
                   sizeof(unixname) - v->unixnamlen - pathlen-1 );
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
     completition = nw_path_directory_is_ok(nwpath, stbuff);
   }
   return(completition);
}

static int conn_get_kpl_path(NW_PATH *nwpath, struct stat *stbuff,
                          int dirhandle, uint8 *data, int len, int only_dir)
/*
 * if ok then the inode of dir will be returned
 * else a negativ errcode will be returned
 */
{
   int completition = build_path(nwpath, data, len, only_dir);
   XDPRINTF((5, 0, "compl=0x%x, conn_get_kpl_path %s",
      completition, conn_get_nwpath_name(nwpath)));
   if (!completition) completition = build_dir_name(nwpath, stbuff, dirhandle);
   return(completition);
}

int conn_get_full_path(int dirhandle, uint8 *data, int len,
                       uint8 *fullpath, int size_fullpath)
/* returns path in form VOLUME:PATH */
{
  NW_PATH nwpath;
  struct stat stbuff;
  int result = build_path(&nwpath, data, len, 0);
  fullpath[0]='\0';
  if (!result)
     result = build_dir_name(&nwpath, &stbuff, dirhandle);
  if (result > -1) {
    uint8 *p=(*nwpath.path=='/') ? nwpath.path+1 : nwpath.path;
    int len = slprintf(fullpath, size_fullpath-1, "%s:%s",
       nw_volumes[nwpath.volume].sysname, p);
    if (len > 0) {
      if (nwpath.fn[0]) {
        if (*p) fullpath[len++]='/';
        strmaxcpy(fullpath+len, nwpath.fn, size_fullpath - len -1);
      }
      result = len + strlen(nwpath.fn);
    } else result = -0x9c; /* wrong path */
  }
  XDPRINTF((1, 0, "conn_get_full_path: result=%d,(0x%x),`%s`", result, result, fullpath));
  return(result);
}

int conn_get_kpl_unxname(char *unixname,
                         int size_unixname,
                         int dirhandle,
                         uint8 *data, int len)
/*
 * gives the unixname of dirhandle + path
 * returns volumenumber, or < 0 if error
 */
{
  NW_PATH nwpath;
  struct stat stbuff;
  int completition = build_path(&nwpath, data, len, 0);
  if (!completition)
     completition = build_dir_name(&nwpath, &stbuff, dirhandle);
  if (completition > -1) {
    if (unixname)
      strmaxcpy(unixname, build_unix_name(&nwpath, 0), size_unixname-1);
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

static int get_file_attrib(NW_FILE_INFO *f, char *unixname, struct stat *stb,
                           NW_PATH *nwpath)
{
  uint32  dwattrib;
  strncpy((char*)f->name, (char*)nwpath->fn, sizeof(f->name));
  f->attrib[0]=0;  /* d->name could be too long */
  up_fn(f->name);

  dwattrib  = get_nw_attrib_dword(nwpath->volume, unixname, stb);
  U16_TO_16(dwattrib, f->attrib);

  un_date_2_nw(stb->st_mtime, f->create_date, 1);
  un_date_2_nw(stb->st_atime, f->acces_date,  1);
  un_date_2_nw(stb->st_mtime, f->modify_date, 1);
  un_time_2_nw(stb->st_mtime, f->modify_time, 1);
  U32_TO_BE32(stb->st_size,   f->size);
  return(1);
}


static int get_dir_attrib(NW_DIR_INFO *d, char *unixname, struct stat *stb,
                         NW_PATH *nwpath)
{
  uint32  dwattrib;
  XDPRINTF((5,0, "get_dir_attrib of %s", conn_get_nwpath_name(nwpath)));
  strncpy((char*)d->name, (char*)nwpath->fn, sizeof(d->name));
  d->attrib[0]=0;  /* d->name could be too long */
  up_fn(d->name);

  dwattrib = get_nw_attrib_dword(nwpath->volume, unixname, stb);
  U16_TO_16(dwattrib, d->attrib);

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
  xstrcpy(unname, build_unix_name(nwpath, 0));
  XDPRINTF((5,0,"DELETE FILE unname:%s:", unname));
  return(nw_unlink_node(nwpath->volume, unname, &(fs->statb)));
}

int nw_delete_files(int dir_handle, int searchattrib, uint8 *data, int len)
{
  NW_PATH nwpath;
  struct stat stbuff;
  int completition = conn_get_kpl_path(&nwpath, &stbuff, dir_handle, data, len, 0);
  if (completition >  -1) {
    completition = func_search_entry(&nwpath, searchattrib, do_delete_file, NULL);
    if (completition < 0) return(completition);
    else if (!completition) return(-0xff);
  }
  return(completition);
}

#if 1
/* new since 22-May-99, 0.99.pl16 */
/* corrected 03-Jun-99, 0.99.pl17 */
typedef struct {
  NW_PATH     destpath;
  struct stat statbuf;
} MV_FILES_STRUCT;

static int do_mv_file(NW_PATH *nwpath, FUNC_SEARCH *fs)
/*  rename one File */
{
  char unsource[256];
  int  result=0;
  MV_FILES_STRUCT *nws=(MV_FILES_STRUCT*)fs->ubuf;
  struct stat statb;
  xstrcpy(unsource, build_unix_name(nwpath, 0));

  if (stat(unsource, &statb) ||
     tru_eff_rights_exists(nwpath->volume, unsource, &statb,
           TRUSTEE_W|TRUSTEE_M|TRUSTEE_R))
          result=-0x8b;
  else if ( !S_ISDIR(fs->statb.st_mode) /* file */ && !(entry8_flags&0x80) &&
            -1 == share_file(fs->statb.st_dev, fs->statb.st_ino, 0x10f, 2) )
      result = -0x8a; /* NO Rename Privileges, file is shared open */
                      /* patch from Przemyslaw Czerpak */
  if (!result) {
    /* sourcefile is ok, now try to move to destfile */
    char undest[256];
    char saved_fn[256];

    uint8 *frompath = nwpath->fn;
    uint8 *topath   = nws->destpath.fn;
    uint8 *otopath  = saved_fn;
    uint8 c;

    /* we must save destpath, because perhaps we must modify it */
    strmaxcpy(saved_fn, nws->destpath.fn, sizeof(saved_fn)-1);

    while ((0 != (c = *otopath++))) {
      switch (c) {
        case '?' :
        case 0xbf:
              if ( *frompath
                && *frompath != '.'
                && *frompath != 0xae )
                *topath++ =  *frompath++;
              break;

        case '.' :
        case 0xae:
              while (*frompath
                && *frompath != '.'
                && *frompath != 0xae )
                frompath++;
              if (*frompath)
                frompath++;
              /* only append '.' if not at end */
              if (*otopath)
                *topath++='.';
              break;

          default:
              if (*frompath && *frompath != '.' && *frompath != 0xae)
                frompath++;
              *topath++=c;
              break;

      } /* switch */
    } /* while */
    *topath='\0';
    xstrcpy(undest, build_unix_name(&nws->destpath, 0));
    /* now restore destpath.fn */
    xstrcpy(nws->destpath.fn, saved_fn);

    seteuid(0);
    if (entry8_flags & 0x4)  /* new: 20-Nov-96 */
      result = unx_mvfile_or_dir(unsource, undest);
    else
      result = unx_mvfile(unsource, undest);
    reseteuid();

    switch (result) {
      case   0      : break;                 /* ok             */
      case   EEXIST : result = -0x92; break; /* allready exist */
      case   EXDEV  : result = -0x9a; break; /* cross device   */
      case   EROFS  : result = -0x8b; break; /* no rights      */
      default       : result = -0xff; break; /* unknown error  */
    }
  }

  return(result);
}

int nw_mv_files(int searchattrib,
                int sourcedirhandle, uint8 *sourcedata, int sourcedatalen,
                int destdirhandle,   uint8 *destdata,   int destdatalen)
{
  NW_PATH nwpath;
  struct stat stbuff;
  int completition = conn_get_kpl_path(&nwpath, &stbuff, sourcedirhandle, sourcedata, sourcedatalen, 0);
  if (completition >  -1) {
    FUNC_SEARCH  fs;
    MV_FILES_STRUCT mvs;
    completition=conn_get_kpl_path(&mvs.destpath, &mvs.statbuf,
                 destdirhandle, destdata, destdatalen, 0);
    if (completition > -1) {
      char destpath[256];
      completition=0;
      xstrcpy(destpath, build_unix_name(&mvs.destpath, 1));
      if (tru_eff_rights_exists(mvs.destpath.volume, destpath,
              &mvs.statbuf, TRUSTEE_W))
        completition=-0x8b;
      /* now destpath is tested to be writable */
    }
    if (completition > -1) {
      fs.ubuf = (uint8*)&mvs;
      completition = func_search_entry(&nwpath, searchattrib, do_mv_file, &fs);
      if (completition < 0) return(completition);
      else if (!completition) return(-0xff);
      else return(0);
    }
  }
  return(completition);
}


#else /* old version (before 22-May-99, 0.99.pl15 ) */

/* "Resolve" string 'topath' (possible with wildcards '?') using
 *  'frompath' as source for substitution. Make changes directly in
 *  'topath'. Return new length of topath (equal or less than original value)
 *  Routine from: Andrew Sapozhnikov
 */

static int apply_wildcards (uint8 *frompath, int flen, uint8 *topath, int tlen)
{
  int i,tlen2;
  uint8 c,*topath2;

  for (i=flen; i > 0; i--) {
    c=frompath[i-1];
    if (c == ':' || c == '\\' || c == '/') break;
  }
  frompath+=i;
  flen-=i;

  for (i=tlen; i > 0; i--) {
    c=topath[i-1];
    if(c == ':' || c == '\\' || c == '/')    break;
  }
  topath2=(topath+=i);
  tlen2=tlen-i;

  while (tlen2--) {
    switch (c=*topath2++) {
      case '?':
      case 0xbf:
            if (flen && *frompath != '.' && *frompath != 0xae) {
              *topath++ = *frompath++;
              flen--;
            } else tlen--;
            break;
      case '.':
      case 0xae:
            while (flen && *frompath != '.' && *frompath != 0xae) {
              frompath++;
              flen--;
            }
            if (flen) {
              frompath++;
              flen--;
            }
            *topath++=c;
            break;
        default:
            if (flen && *frompath != '.' && *frompath != 0xae) {
              frompath++;
              flen--;
            }
            *topath++=c;
    }
  }
  return tlen;
}

int mv_file(int qdirhandle, uint8 *q, int qlen,
            int zdirhandle, uint8 *z, int zlen)
{
  NW_PATH quellpath;
  struct stat qstbuff;
  NW_PATH zielpath;
  struct stat zstbuff;
  int completition;
  zlen=apply_wildcards(q, qlen, z, zlen);
  completition=conn_get_kpl_path(&quellpath, &qstbuff, qdirhandle, q, qlen, 0);

  if (completition > -1) {
    char qfn[256];
    xstrcpy(qfn, build_unix_name(&quellpath,0));
    completition=conn_get_kpl_path(&zielpath, &zstbuff, zdirhandle, z, zlen, 0);
    if (completition > -1) {
      char zpath[256];
      completition=0;
      xstrcpy(zpath, build_unix_name(&zielpath, 1));
      if (stat(qfn, &qstbuff) ||
        tru_eff_rights_exists(quellpath.volume, qfn, &qstbuff,
           TRUSTEE_W|TRUSTEE_M|TRUSTEE_R))
          completition=-0x8b;
      else if (tru_eff_rights_exists(zielpath.volume, zpath, &zstbuff,
           TRUSTEE_W))
        completition=-0x8b;
    }
    if (!completition){
      char unziel[256];
      xstrcpy(unziel, build_unix_name(&zielpath,0));

      seteuid(0);
      if (entry8_flags & 0x4)  /* new: 20-Nov-96 */
        completition = unx_mvfile_or_dir(qfn, unziel);
      else
        completition = unx_mvfile(qfn, unziel);
      reseteuid();

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
#endif

static int do_set_file_info(NW_PATH *nwpath, FUNC_SEARCH *fs)
{
  char unname[256];
  int  result=0;
  NW_FILE_INFO *f=(NW_FILE_INFO*)fs->ubuf;
  int voloptions = get_volume_options(nwpath->volume);
  struct stat statb;
  xstrcpy(unname, build_unix_name(nwpath, 0));
  if (!stat(unname, &statb)) {
    if (S_ISFIFO(statb.st_mode) || (voloptions&VOL_OPTION_IS_PIPE))
      return(0); /* do nothing but report OK */
    if (tru_eff_rights_exists(nwpath->volume, unname, &statb, TRUSTEE_M))
      result=-0x8c;  /* no modify rights */
  } else result=-0xff;
  if (!result) {
    struct utimbuf ut;
    ut.actime = ut.modtime = nw_2_un_time(f->modify_date, f->modify_time);
    result=set_nw_attrib_word(nwpath->volume, unname, &statb,
                    (int)GET_16(f->attrib));
    if (!result) {
      seteuid(0);
      if (utime(unname, &ut))
        result= (-0x8c); /* no modify rights */
      reseteuid();
    }
  }
  XDPRINTF((5,0,"set_file_info result=0x%x, unname:%s:", unname, -result));
  return(result);
}

static void free_dir_stuff(struct stat *stb, int complete)
{
  NW_DIR *d=&(dirs[0]);
  int j = -1;
  if (complete) {
    while (++j < (int)used_dirs){
      if (  d->dev   == stb->st_dev &&
            d->inode == stb->st_ino)
        d->inode = 0;
      d++;
    }
    j = -1;
  }
  while (++j < (int)anz_dirhandles){
    DIR_HANDLE  *dh=&(dir_handles[j]);
    if (dh && dh->dev == stb->st_dev && dh->inode == stb->st_ino) {
      if (complete)
        free_dir_handle(j+1);
      else if (dh->f) { /* only close directories */
        /* needed to ask for null pointer, hint from Boris Popov, 21-Feb-99 */
        closedir(dh->f);
        dh->f = (DIR*)NULL;
      }
    }
  }
}

int nw_set_file_information(int dir_handle, uint8 *data, int len,
                             int searchattrib, NW_FILE_INFO *f)
{
  NW_PATH nwpath;
  struct stat stbuff;
  int completition = conn_get_kpl_path(&nwpath, &stbuff, dir_handle, data, len, 0);
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

int nw_set_file_attributes(int dir_handle, uint8 *data, int len,
                          int attrib, int newattrib)

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
    completition = build_dir_name(&nwpath, &stbuff, dir_handle);
  }
  if (completition < 0) return(completition);
  xstrcpy(unname, build_unix_name(&nwpath, 2));
  XDPRINTF((5,0,"set file attrib 0x%x, unname:%s:", newattrib,  unname));

  if (!s_stat(unname, &stbuff, &stb)){
    int result = set_nw_attrib_byte(nwpath.volume, unname, &stbuff, newattrib);
    return( (result != 0) ? -0x8c : 0);  /* no modify rights */
  }
  return(-0x9c); /* wrong path */
}

static int nw_rmdir(uint8 *unname)
{
  if (rmdir(unname)) {
    if (errno == /*EEXIST*/ ENOTEMPTY) return(-0xa0); /* directory not empty */
    return(-0x8a); /* no privilegs */
  }
  return(0);
}

int nw_unlink_node(int volume, uint8 *unname, struct stat *stb)
{
  int result=-1;
  uint32 attrib=get_nw_attrib_dword(volume, unname, stb);
  /* first we look for attributes */
  if (attrib & (FILE_ATTR_R|FILE_ATTR_DELETE_INH))
     return(-0x8a); /* don't delete 'readonly' */
  if (tru_eff_rights_exists(volume, unname, stb, TRUSTEE_E))
     return(-0x8a); /* no delete rights */

  if (S_ISDIR(stb->st_mode)) { /* directory */
    free_dir_stuff(stb, 0);
    result=nw_rmdir(unname);
    if (result==-0x8a){  /* no privilegs */
      seteuid(0);
      result=nw_rmdir(unname);
      reseteuid();
    }
    if (!result)
      free_dir_stuff(stb, 1);
  } else {
    /* changed by: Ingmar Thiemann <ingmar@gefas.com>
     * because share_file() changed
     */
    if (!(entry8_flags&0x10) &&
         -1 == share_file(stb->st_dev, stb->st_ino, 0x10f, 2))
      return(-0x8a); /* NO Delete Privileges, file is open */
    if (0 != (result=unlink(unname))){
      seteuid(0);
      result=unlink(unname) ? -0x8a : 0;
      reseteuid();
    }
  }
  if (!result) {
    free_nw_ext_inode(volume, unname, stb->st_dev, stb->st_ino);
  }
  return(result);
}

int nw_creat_node(int volume, uint8 *unname, int mode)
/* creat file or directory, depending on mode */
/* mode & 0x1 == directory     */
/* mode & 0x2 == creat/trunc   */
/* next is only for file creation, needed by some queue functions */
/* mode & 0x8 == ignore rights, try to open as root */
{
  struct stat stb;
  uint8 path[260];
  uint8 *p=path+strlen(unname);
  xstrcpy(path, unname);
  while (p > path && *p != '/') --p;
  if (p > path) {
    *p='\0';
    if (stat(path, &stb)) return(-0x9c);
  } else if (*p=='/') {
    *(p+1)='.';
    *(p+2)='\0';
    if (stat(path, &stb)) return(-0x9c);
  } else
    return(-0x9c);

  if (mode & 1) {  /* directory */
    int result=-0xff;
    if (!tru_eff_rights_exists(volume, path, &stb, TRUSTEE_C)){
      result=mkdir(unname, 0777);
      if (result) {
        seteuid(0);
        if (0==(result=mkdir(unname, 0755)))
          chown(unname, act_uid, act_gid);
        reseteuid();
      }
      if (result)
        result=-0xff;
    } else result=-0x84; /* no creat rights */
    if (!result) {
      int umode_dir=get_volume_umode_dir(volume);
      if (umode_dir) {
        if (umode_dir == -1)   /* we get parent dir */
          umode_dir=stb.st_mode;
        seteuid(0);
        chmod(unname, umode_dir);
        reseteuid();
      }
    }
    return(result);
  } else {  /* file */
    int fd=-1;
    struct stat stbuff;
    int exist;

    seteuid(0);
    exist=stat(unname, &stbuff) ? 0 : 1;
    reseteuid();

    if (!(mode&0x8)) { /* we must test for access */
      if (exist) { /* test for write rights */
        if (get_nw_attrib_dword(volume, unname, &stbuff) & FILE_ATTR_R)
          return(-0x94); /* No write rights */
        if (tru_eff_rights_exists(volume, unname, &stbuff, TRUSTEE_W))
          return(-0x94); /* No write rights */
      } else { /* test for creat rights */
        if (tru_eff_rights_exists(volume, path, &stb, TRUSTEE_C))
          return(-0x84); /* No creat rights */
      }
    }

    if (mode & 2 || (exist && (mode&0x8)) ) { /* trunc */
      if (0 > (fd=open(unname, O_CREAT|O_TRUNC|O_RDWR, 0666))) {
        seteuid(0);
        if (-1 < (fd=open(unname, O_CREAT|O_TRUNC|O_RDWR, 0600)))
          chown(unname, act_uid, act_gid);
        reseteuid();
      }
    } else if (!exist) {
      if (0 > (fd = creat(unname, 0777))) {
        seteuid(0);
        if (-1 < (fd = creat(unname, 0751)))
          chown(unname, act_uid, act_gid);
        reseteuid();
      }
    }
    if ( fd > -1 ) {
      int umode_file=get_volume_umode_file(volume);
      close(fd);
      if (umode_file > 0)
        chmod(unname, umode_file);
      return(0);
    }
  }
  return(-0xff);
}

int nw_utime_node(int volume, uint8 *unname, struct stat *stb,
                   time_t t)
{
  if (!tru_eff_rights_exists(volume, unname, stb, TRUSTEE_M)){
    struct utimbuf ut;
    ut.actime = ut.modtime = t;
    if (!utime(unname, &ut))
      return(0);
    seteuid(0);
    if (!utime(unname, &ut)) {
      reseteuid();
      return(0);
    }
    reseteuid();
  }
  return(-0x8c); /* no modify privileges */
}

int nw_mk_rd_dir(int dir_handle, uint8 *data, int len, int mode)
{
  NW_PATH nwpath;
  struct stat stbuff;
  int completition = conn_get_kpl_path(&nwpath, &stbuff,
                    dir_handle, data, len, (mode) ? 0 : 1 );
  if (completition > -1) {
    char unname[256];
    xstrcpy(unname, build_unix_name(&nwpath, 2));
    if (mode) {
      completition=nw_creat_node(nwpath.volume, unname, 1);
    } else { /* rmdir */
      if (!stat(unname, &stbuff))
        completition=nw_unlink_node(nwpath.volume, unname, &stbuff);
      else
       completition=-0x9c;
    }
  }
  return(completition);
}


int mv_dir(int dir_handle, uint8 *sourcedata, int sourcedatalen,
                           uint8 *destdata,   int destdatalen)
{
  NW_PATH quellpath;
  struct stat qstbuff;
  NW_PATH zielpath;
  int completition=conn_get_kpl_path(&quellpath, &qstbuff, dir_handle,
                                     sourcedata, sourcedatalen, 0);
  if (completition > -1){
    char qfn[256];
    char zpath[256];
    struct stat zstbuff;
    completition = 0;
    xstrcpy(qfn, build_unix_name(&quellpath,0));
    memcpy(&zielpath, &quellpath, sizeof(NW_PATH));
    strmaxcpy(zielpath.fn, destdata, destdatalen);

    /* patch from Sven Norinder <snorinder@sgens.ericsson.se> :09-Nov-96 */
    if (get_volume_options(zielpath.volume) & VOL_OPTION_DOWNSHIFT)
      down_fn(zielpath.fn);
    else
      up_fn(zielpath.fn);

#if 0
    /* this is not possible ---- */
    completition=conn_get_kpl_path(&zielpath, &zstbuff, dir_handle, destdata, destdatalen, 0);
    /* ----------- because ----- */
    /* for example the novell rendir.exe does something like this
     * 0x0,0xd,0xf,0x0,0x7,'T','M','P',':','\','I','I',0x2,'K','K'
     * no dirhandle, qpath = fullpath, zpath = only name
     */
#endif

    xstrcpy(zpath, build_unix_name(&zielpath, 1));
    if (stat(qfn, &qstbuff) ||
        tru_eff_rights_exists(quellpath.volume, qfn, &qstbuff,
           TRUSTEE_W|TRUSTEE_M|TRUSTEE_R))
      completition=-0x8b;
    else if (stat(zpath, &zstbuff) ||
       tru_eff_rights_exists(zielpath.volume, zpath, &zstbuff,
           TRUSTEE_W))
      completition=-0x8b;

    if (completition > -1){
      int result;
      char unziel[256];
      xstrcpy(unziel,   build_unix_name(&zielpath,  0));

      seteuid(0);
      result = unx_mvdir((uint8 *)qfn, (uint8 *)unziel);
      reseteuid();

      XDPRINTF((4,0, "rendir result=%d, '%s'->'%s'",
                 result, qfn, unziel));
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
                             uint8 *path,
                             int dev,         ino_t inode,
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
    dir->dev          = dev;
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
#if WITH_NAME_SPACE_CALLS
  exit_name_space_module();
#endif
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
    int namspace_max_baseh=0;
    int namspace_max_searchh=0;
    xstrcpy(nwlogin.path, (char*)login);
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
      while (k++ < anz_dirhandles)
        free_dir_handle(k);
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
        uint8 buf1[300], buf2[300];
        if (2 == sscanf((char*)buff, "%300s %300s", buf1, buf2)) {
          default_umode_dir  = octtoi(buf1);
          default_umode_file = octtoi(buf2);
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
      } else if (what == 31) { /* entry31_flags */
        entry31_flags = hextoi((char*)buff);
      } else if (50 == what) {
        init_nwfname(buff);
      } else if (63 == what) {  /* MAX_DIR_BASE */
        namspace_max_baseh=atoi(buff);
      } else if (68 == what) {  /* USE_MMAP */
        use_mmap=atoi(buff);
      } else if (80 == what) {
        namspace_max_searchh=atoi(buff);
      } else if (what == 103) { /* Debug */
        get_debug_level(buff);
      }
    } /* while */
    nw_init_volumes(f);
    fclose(f);
#if WITH_NAME_SPACE_CALLS
    init_name_space_module(namspace_max_baseh, namspace_max_searchh);
#endif
    if (used_nw_volumes < 1) {
      errorp(1, "No Volumes defined. Look at ini file entry 1, Abort !!", NULL);
      return(-1);
    }
    if (get_volume_options(0) & VOL_OPTION_DOWNSHIFT)
      down_fn(nwlogin.path);
    if (stat(build_unix_name(&nwlogin, 0), &stbuff)) {
      errorp(1, "Stat error LOGIN Directory, Abort !!",
            "UnixPath=`%s`, nwlogin.path=%s, nwlogin.fn=%s",
               build_unix_name(&nwlogin, 0), nwlogin.path, nwlogin.fn  );
      return(-1);
    }
    (void)change_dir_entry(&(dirs[0]), 0, nwlogin.path,
                       stbuff.st_dev, stbuff.st_ino,
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

int xinsert_new_dir(int volume, uint8 *path, int dev, int inode, int drive, int is_temp, int task)
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
          volume, path, dev, inode,
          drive, is_temp, 1, task);
    while (used_dirs > freehandle && !dirs[used_dirs-1].inode) used_dirs--;
    return(freehandle);
  } else return(-0x9d);  /* no dir Handles */
}

static int insert_new_dir(NW_PATH *nwpath, int dev, int inode,
                    int drive, int is_temp, int task)
{
 return(xinsert_new_dir(nwpath->volume, nwpath->path,
                       dev, inode, drive, is_temp, task));
}


int nw_search(uint8 *info, uint32 *fileowner,
              int dirhandle, int searchsequence,
              int search_attrib, uint8 *data, int len)

{
   NW_PATH nwpath;
   struct stat stbuff;
   int     completition = conn_get_kpl_path(&nwpath, &stbuff, dirhandle, data, len, 0);
   XDPRINTF((5,0,"nw_search path:%s:, fn:%s:, completition:0x%x",
     nwpath.path, nwpath.fn, completition));
   if (completition > -1) {
      struct stat stbuff;
      completition=get_dir_entry(&nwpath,
                        &searchsequence,
                        search_attrib,
                        &stbuff);
      if (!completition) {
         char unixname[300];
         xstrcpy(unixname, build_unix_name(&nwpath, 0));
         if ( S_ISDIR(stbuff.st_mode) ) {
           get_dir_attrib((NW_DIR_INFO*)info, unixname, &stbuff,
                   &nwpath);
         } else {
           get_file_attrib((NW_FILE_INFO*)info, unixname, &stbuff,
                &nwpath);
         }
         if (fileowner) *fileowner = get_file_owner(&stbuff);
         return(searchsequence);
      }
   }
   return(completition);
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
     char unixname[300];
     struct stat stbuff;
     if (dh->vol_options & VOL_OPTION_DOWNSHIFT) {
       down_fn(nwpath.fn);
     } else {
       up_fn(nwpath.fn);
     }
     nwpath.volume=dh->volume;
     if (get_dh_entry(dh,
                      nwpath.fn,
                      sizeof(nwpath.fn),
                      &searchsequence,
                      search_attrib,
                      unixname,
                      sizeof(unixname),
                      &stbuff)){

       if ( S_ISDIR(stbuff.st_mode) ) {
         get_dir_attrib((NW_DIR_INFO*)info, unixname,  &stbuff,
              &nwpath);
       } else {
         if (dh->vol_options & VOL_OPTION_IS_PIPE)
           stbuff.st_size  = 0x70000000|(stbuff.st_mtime&0xfffffff);
         get_file_attrib((NW_FILE_INFO*)info, unixname, &stbuff,
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
                         int    task,          /* process task            */
                         int    *eff_rights)
{
   NW_PATH nwpath;
   struct stat stbuff;
   int inode=conn_get_kpl_path(&nwpath, &stbuff, dir_handle, data, len, 1);
   if (inode > -1) {
     uint8   unixname[257];
     xstrcpy(unixname, build_unix_name(&nwpath, 0));
     inode = insert_new_dir(&nwpath, stbuff.st_dev, stbuff.st_ino,
                           driveletter, is_temphandle, task);
     *eff_rights=tru_get_eff_rights(nwpath.volume, unixname, &stbuff);
   }
   XDPRINTF((4,0,"Allocate %shandle:%s, Qhandle=%d, drive=%d, Task=%d, result=0x%x",
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
   struct stat stbuff;
   int completition = conn_get_kpl_path(&nwpath, &stbuff, dir_handle, data, len, 1);
   if (completition > -1) {
     XDPRINTF((5,0,"NW_OPEN_DIR: completition = 0x%x; nwpath= %s",
           (int)completition, conn_get_nwpath_name(&nwpath) ));

     completition = new_dir_handle(&stbuff, &nwpath);
     if (completition > -1) {
       struct stat stb;
       DIR_HANDLE *dh  = &(dir_handles[completition-1]);
       s_stat(dh->unixname, &stb, NULL);
       *volume         = dh->volume;
       *dir_id         = completition;
       *searchsequence = MAX_U16;
       completition    = tru_get_eff_rights(dh->volume, dh->unixname, &stb);
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
    XDPRINTF((4,0,"free dhandle:%d, task=%d, d->inode=0x%x, used_dirs=%d",
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
                     int dev, int inode, int task)
/* targetdir will be changed */
{
  if (targetdir > 0 && --targetdir < used_dirs
    && dirs[targetdir].is_temp != 2) {
    /* do not change special temphandles */
    XDPRINTF((5,0,"Change dhandle:%d(%d) -> '%d:%s'", targetdir+1, task,
              volume, path));
    return(change_dir_entry(&dirs[targetdir],
                    volume, path, dev, inode,
                    -1, -1, 0, task));
    /* here the existing handle is only modified */
  } else return(-0x9b); /* BAD DIR Handle */
}


int nw_set_dir_handle(int targetdir, int dir_handle,
                          uint8 *data, int len, int task)
/* targetdirs gets path of dirhandle + data */
{
  NW_PATH nwpath;
  struct stat stbuff;
  int inode = conn_get_kpl_path(&nwpath, &stbuff,  dir_handle, data, len, 1);
  if (inode > -1)
    inode = alter_dir_handle(targetdir, nwpath.volume, nwpath.path,
                              stbuff.st_dev, stbuff.st_ino, task);
  return(inode);  /* invalid PATH */
}

int nw_get_directory_path(int dir_handle, uint8 *name, int size_name)
{
  int     result   = -0x9b;
  name[0] = '\0';
  if (dir_handle > 0 && --dir_handle < (int)used_dirs
                     && dirs[dir_handle].inode) {
    int volume = dirs[dir_handle].volume;
    if (volume > -1 && volume < used_nw_volumes){
      result = slprintf((char*)name, size_name-1, "%s:%s", nw_volumes[volume].sysname, dirs[dir_handle].path);
      if (result > 0) {
        if (name[result-1] == '/') name[--result] = '\0';
        up_fn(name);
      } else result= -0x9c; /* wrong path */
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
  int completition = conn_get_kpl_path(&nwpath, &stbuff,
                               dir_handle, data, len, 0);
  if (completition < 0) return(completition);
  xstrcpy(unname, build_unix_name(&nwpath, 0));
  if (s_stat(unname, &stbuff, NULL) ||
    (!modus && !S_ISDIR(stbuff.st_mode)) ) {
    completition = -0x9c;
  } else
    completition=tru_get_eff_rights(nwpath.volume, unname, &stbuff);
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
  struct stat stbuff;
  int completition = conn_get_kpl_path(&nwpath, &stbuff, dir_handle, data, len, 0);
  if (completition > -1) {
     char unixname[300];
     xstrcpy(unixname, build_unix_name(&nwpath, 0));
     completition=file_creat_open(nwpath.volume, (uint8*)unixname,
                      &stbuff, attrib, access, creatmode, task);

     if (completition > -1)
       get_file_attrib(info, unixname, &stbuff, &nwpath);
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
    uint16 dirsequence     = GET_BE16(subnr);
    uint16 aktsequence     = 0;
    uint8  dirname[256];
    if (!dirsequence) dirsequence++;

    if (dh->vol_options & VOL_OPTION_DOWNSHIFT) {
      down_fn(wild);
    } else {
      up_fn(wild);
    }

    xstrcpy(dirname, (char*)wild);
    XDPRINTF((5,0,"SCAN_DIR: rights = 0x%x, subnr = %d",
                (int)rights, (int)GET_BE16(subnr)));

    if (*dirname) {
      char unixname[300];
      while ( get_dh_entry( dh,
                            dirname,
                            sizeof(dirname),
                            &searchsequence,
                            0x10,
                            unixname,
                            sizeof(unixname),
                            &stbuff) ) {

        XDPRINTF((5,0,"SCAN_DIR: von %s, found %s:", dh->unixname, dirname));
        if (++aktsequence == dirsequence) { /* actual found */
          U16_TO_BE16(aktsequence, subnr);
          up_fn(dirname);
          strncpy((char*)subname, (char*)dirname, 16);
          U32_TO_BE32(get_file_owner(&stbuff),  owner);
          un_date_2_nw(stbuff.st_mtime, subdatetime,   1);
          un_time_2_nw(stbuff.st_mtime, subdatetime+2, 1);
          return(tru_get_inherited_mask(volume, unixname, &stbuff));
        }
        xstrcpy(dirname, (char*)wild);
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
        return(tru_get_inherited_mask(volume, dh->unixname, &stbuff));
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
                               uint8        *path,
                               char         *unixname)
{
  uint8  spath[14];
  uint32 nw_owner=get_file_owner(stb);
  f->namlen=min(strlen((char*)path), 12);
  strmaxcpy(spath, path, 12);
  up_fn(spath);
  strncpy((char*)f->name, (char*)spath, f->namlen);
  U32_TO_32(get_nw_attrib_dword(volume, unixname, stb), f->attributes);
  U16_TO_16(tru_get_inherited_mask(volume, unixname, stb),
        f->inherited_rights_mask);
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
                                uint8 *path,
                                char  *unixname)
{
  uint8 spath[14];
  f->namlen=min(strlen((char*)path), 12);
  strmaxcpy(spath, path, 12);
  up_fn(spath);
  strncpy((char*)f->name, (char*)spath, f->namlen);
  U32_TO_32(get_nw_attrib_dword(volume, unixname, stb),
             f->attributes);
  U16_TO_16(tru_get_inherited_mask(volume, unixname, stb),
        f->inherited_rights_mask);
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
  struct stat stbuff;
  int     completition = conn_get_kpl_path(&nwpath, &stbuff, dirhandle, data, len, 0);
  XDPRINTF((5,0,"nw_scan_a_directory path:%s:, fn:%s:, completition:0x%x",
    nwpath.path, nwpath.fn, completition));
  if (completition > -1) {
     int searchsequence = (searchbeg == MAX_U32) ? MAX_U16 : searchbeg;
     completition=get_dir_entry(&nwpath,
                       &searchsequence,
                       searchattrib,
                       &stbuff);

     if (!completition) {
       char unixname[300];
       NW_SCAN_DIR_INFO *scif = (NW_SCAN_DIR_INFO*)rdata;
       xstrcpy(unixname, build_unix_name(&nwpath, 0));
       memset(rdata, 0, sizeof(NW_SCAN_DIR_INFO));
       U32_TO_BE32((uint32)searchsequence, scif->searchsequence);

       XDPRINTF((5,0, "nw_scan_a_directory = %s, uid=%d, gid=%d",
         conn_get_nwpath_name(&nwpath),
         stbuff.st_uid, stbuff.st_gid));

       if ( S_ISDIR(stbuff.st_mode)) {
          get_dos_dir_attrib(&(scif->u.d), &stbuff,
                             nwpath.volume, nwpath.fn, unixname);
       } else {
          get_dos_file_attrib(&(scif->u.f), &stbuff,
                             nwpath.volume,  nwpath.fn, unixname);
       }
       return(sizeof(NW_SCAN_DIR_INFO));
     }
  }
  return(completition); /* wrong path */
}

int nw_scan_a_root_dir(uint8   *rdata,
                       int     dirhandle)
{
  NW_PATH nwpath;
  struct stat stbuff;
  uint8   data[2];
  int     completition = conn_get_kpl_path(&nwpath, &stbuff, dirhandle, data, 0, 1);
  XDPRINTF((5,0,"nw_scan_a_root_directory_2 path:%s:, fn:%s:, completition:0x%x",
    nwpath.path, nwpath.fn, completition));
  if (completition > -1) {
    char unixname[300];
    xstrcpy(unixname, build_unix_name(&nwpath, 2));
    if (!s_stat(unixname, &stbuff, NULL)) {
      NW_DOS_DIR_INFO  *d=(NW_DOS_DIR_INFO*)rdata;
      memset(rdata, 0, sizeof(NW_DOS_DIR_INFO));
      get_dos_dir_attrib(d, &stbuff, nwpath.volume, nwpath.fn, unixname);
      return(sizeof(NW_DOS_DIR_INFO));
    } else return(-0xff); /* not found */
  } else return(completition); /* wrong path */
}

int nw_set_a_directory_entry(int     dirhandle,
                             uint8   *data,
                             int     len,
                             int     searchattrib,
                             uint32  searchbeg,
                             NW_SET_DIR_INFO *f)
{
  NW_PATH nwpath;
  struct stat stbuff;
  int     completition = conn_get_kpl_path(&nwpath, &stbuff, dirhandle, data, len, 0);
  XDPRINTF((5,0,"nw_set_a_directory_entry path:%s:, fn:%s:, completition:0x%x",
    nwpath.path, nwpath.fn, completition));
  if (completition > -1) {
     int searchsequence = MAX_U16;
     /* (searchbeg == MAX_U32) ? MAX_U16 : searchbeg; */
     completition=get_dir_entry(&nwpath,
                       &searchsequence,
                       searchattrib,
                       &stbuff);
     if (!completition) {
       char unixname[300];
       uint32 change_mask=GET_32(f->change_bits);
       xstrcpy(unixname,build_unix_name(&nwpath, 0));
       if (change_mask & 0x2) {
         completition=set_nw_attrib_dword(nwpath.volume, unixname, &stbuff,
            GET_32(f->u.f.attributes));
       }
       if (S_ISDIR(stbuff.st_mode)) {
         if (change_mask & 0x1000) {
           int result=tru_set_inherited_mask(nwpath.volume, unixname,
                         &stbuff, GET_16(f->u.d.inherited_rights_mask));
           if (result)
             completition=result;
         }
       } else {
         if (change_mask & 0x1000) {
           int result=tru_set_inherited_mask(nwpath.volume, unixname,
                         &stbuff, GET_16(f->u.f.inherited_rights_mask));
           if (result)
             completition=result;
         }
       }
     }
  }
  return(completition);
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

  seteuid(0);
  d=opendir(unixname);
  reseteuid();

  if (NULL != d) {
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
    XDPRINTF((3, 0, "DOS get_match opendir failed unixname='%s'", unixname));
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


int nw_add_trustee(int dir_handle, uint8 *data, int len,
                   uint32 id,  int trustee, int extended)
/* extended 0=add trustee to dir, 1= add ext trustee to dirs and files */
{
  char          unname[256];
  struct stat   stbuff;
  NW_PATH       nwpath;
  int result = conn_get_kpl_path(&nwpath, &stbuff, dir_handle, data, len, 0);
  if (result < 0) return(result);
  xstrcpy(unname, build_unix_name(&nwpath, 0));
  if (s_stat(unname, &stbuff, NULL) ||
    (!extended && !S_ISDIR(stbuff.st_mode)) ) {
    result = -0x9c;
  } else {
    NW_OIC nwoic;
    nwoic.id      = id;
    nwoic.trustee = trustee;
    result=tru_add_trustee_set(nwpath.volume, unname,
                              &stbuff,
                              1, &nwoic);
  }
  return(result);
}

int nw_del_trustee(int dir_handle, uint8 *data, int len,
                   uint32 id, int extended)
/* extended 0=del trustee from dir, 1= del ext trustee from dirs and files */
{
  char          unname[256];
  struct stat   stbuff;
  NW_PATH       nwpath;
  int result = conn_get_kpl_path(&nwpath, &stbuff, dir_handle, data, len, 0);
  if (result < 0) return(result);
  xstrcpy(unname, build_unix_name(&nwpath, 0));
  if (s_stat(unname, &stbuff, NULL) ||
    (!extended && !S_ISDIR(stbuff.st_mode)) ) {
    result = -0x9c;
  } else {
    result=tru_del_trustee(nwpath.volume, unname, &stbuff, id);
  }
  return(result);
}

int nw_set_dir_info(int dir_handle, uint8 *data, int len,
                   uint32 owner_id, int max_rights,
                   uint8 *creationdate, uint8 *creationtime)
{
  char          unname[256];
  struct stat   stbuff;
  NW_PATH       nwpath;
  int result = conn_get_kpl_path(&nwpath, &stbuff, dir_handle, data, len,0);
  if (result < 0) return(result);
  xstrcpy(unname, build_unix_name(&nwpath, 0));
  if (s_stat(unname, &stbuff, NULL) || !S_ISDIR(stbuff.st_mode)) {
    result = -0x9c;
  } else {
    result=nw_utime_node(nwpath.volume, unname, &stbuff,
                     nw_2_un_time(creationdate, creationtime));
    if (!result)
      result=tru_set_inherited_mask(nwpath.volume, unname,
                       &stbuff, max_rights);
  }
  return(result);
}

int nw_scan_user_trustee(int volume, int *sequence, uint32 id,
                        int *access_mask, uint8 *path)
{
  uint8 volname[100];
  int result = nw_get_volume_name(volume, volname, sizeof(volname));
  if (result > 0) {
    if (*sequence==-1 || *sequence==0xffff)
      *sequence=0;
    result=get_volume_user_trustee(volume, id, 0, sequence,
                                access_mask, path);
  }
  return(result);
}

int nw_scan_for_trustee( int    dir_handle,
                         int    sequence,
                         uint8 *path,
                         int    len,
                         int     max_entries,
                         uint32 *ids,
                         int    *trustees,
                         int     extended)
{
  char          unname[256];
  struct stat   stbuff;
  NW_PATH       nwpath;
  int result = conn_get_kpl_path(&nwpath, &stbuff, dir_handle, path, len,
                     (extended) ? 0 : 1);
  if (result < 0) return(result);
  xstrcpy(unname, build_unix_name(&nwpath, 0));
  if (s_stat(unname, &stbuff, NULL) ||
    (!extended && !S_ISDIR(stbuff.st_mode)) ) {
    result = -0x9c;
  } else {
    result=tru_get_trustee_set(nwpath.volume, unname,
                              &stbuff,
                              sequence,
                              max_entries, ids, trustees);
  }
  return(result);
}


/* quick and dirty hack for 0.99.pl18, 25-Sep-99 */
int nw_log_file(int lock_flag,
                  int timeout,
                  int dir_handle,
                  int len,
                  char *data)

/*
 *   lock_flag
 *   -1 = remove lock
 *   -2 = remove lock + log 
 *    0 = log               
 *    1 = lock exclusive
 *    3 = shared lock
*/

{
  NW_PATH nwpath;
  struct stat stbuff;
  int completition = conn_get_kpl_path(&nwpath, &stbuff, dir_handle, data, len, 0);
  if (completition > -1) {
    char unixname[300];
    xstrcpy(unixname, build_unix_name(&nwpath, 0));
    seteuid(0);
    completition = stat(unixname, &stbuff);
    reseteuid();
    if (!completition) {
      if (lock_flag < 0) {  /* remove lock */
        if (lock_flag != -1) 
          completition =
            share_set_file_add_rm(lock_flag, stbuff.st_dev, stbuff.st_ino);
        if (!completition)
           completition = share_file(stbuff.st_dev, stbuff.st_ino,
                         0x300, 0) ? -0xff : 0;
      } else if (lock_flag == 0 || lock_flag == 1 || lock_flag == 3) {  
        completition =
          share_set_file_add_rm(lock_flag, stbuff.st_dev, stbuff.st_ino);
        if ((!completition) && lock_flag)
          /* add lock */
          completition = share_file(stbuff.st_dev, stbuff.st_ino,
                         lock_flag << 8, 1) ? -0xfe : 0;
      } else
        completition = -0xfb;
    } else
      completition = -0xff;   /* not exist, errorcode TODO  ! */
  }
  return(completition);
}

