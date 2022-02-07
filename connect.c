/* connect.c  13-Jan-96 */
/* (C)opyright (C) 1993,1995  Martin Stover, Marburg, Germany
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

#include <dirent.h>
#include <utime.h>

#include <sys/errno.h>
extern int errno;

/* #define TEST_FNAME   "PRINT.000"
*/
#ifdef TEST_FNAME
  static int test_handle=-1;
#endif

static int  default_uid=-1;
static int  default_gid=-1;

#include "connect.h"

#define MAX_NW_DIRS    255
static NW_DIR    dirs[MAX_NW_DIRS];
static int       used_dirs=0;
static NW_VOL    vols[MAX_NW_VOLS];
static int       used_vols=0;

static int       connect_is_init = 0;

#define MAX_FILEHANDLES   80
#define MAX_DIRHANDLES    80

static FILE_HANDLE  file_handles[MAX_FILEHANDLES];
static DIR_HANDLE   dir_handles[MAX_DIRHANDLES];

static int anz_fhandles=0;
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
  if (volume < 0 || volume >= used_vols) {
    fprintf(stderr, "build_unix_name volume=%d not ok\n", volume);
    strcpy(unixname, "ZZZZZZZZZZZZ"); /* vorsichthalber */
    return(unixname);
  }
  strcpy(unixname, vols[volume].unixname); /* first UNIXNAME VOLUME */

  p  = pp = unixname+strlen(unixname);
  strcpy(p,  nwpath->path);  /* now the path */
  p += strlen(nwpath->path);
  if ( (!(modus & 1)) && nwpath->fn[0])
    strcpy(p, nwpath->fn);    /* und jetzt fn  */
  else if ((modus & 2) && (*(p-1) == '/')) *(p-1) = '\0';
  if (vols[volume].options & 1) downstr((uint8*)pp);
  return(unixname);
}

static int new_file_handle(void)
{
  int rethandle = -1;
  FILE_HANDLE  *fh=NULL;
  while (++rethandle < anz_fhandles) {
    if (file_handles[rethandle].fd < 0) { /* empty slot */
      fh = &(file_handles[rethandle]);
      rethandle++;
      break;
    }
  }
  if (fh == NULL) {
    if (anz_fhandles < MAX_FILEHANDLES) {
      fh=&(file_handles[anz_fhandles]);
      rethandle = ++anz_fhandles;
    } else return(0); /* no free handle anymore */
  }
  /* init handle  */
  fh->fd      = -1;
  fh->offd    = 0L;
  fh->tmodi   = 0L;
  fh->name[0] = '\0';
  return(rethandle);
}

static int free_file_handle(int fhandle)
{
  if (fhandle > 0 && (fhandle <= anz_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle-1]);
    if (fh->fd > -1) {
      close(fh->fd);
      fh->fd = -1;
      if (fh->tmodi > 0L) {
      /* now set date and time */
        struct utimbuf ut;
        ut.actime = ut.modtime = fh->tmodi;
        utime(fh->name, &ut);
      }
    }
    if (fhandle == anz_fhandles) {
      /* was last */
      anz_fhandles--;
      while (anz_fhandles && file_handles[anz_fhandles-1].fd < 0)
          anz_fhandles--;
    }
    return(0);
  }
  return(-0x88); /* wrong filehandle */
}

static int new_dir_handle(ino_t inode, NW_PATH *nwpath)
/*
 * RETURN=errorcode (<0) or dir_handle
 */
{
  int rethandle;
  DIR_HANDLE  *fh   = NULL;
  time_t  akttime   = time(NULL);
  time_t  last_time = akttime;
  int  thandle      = 0;
  int  nhandle      = 0;
  for (rethandle=0; rethandle < anz_dirhandles; rethandle++){
    fh=&(dir_handles[rethandle]);
    if (fh->f == (DIR*) NULL) {
      if (!nhandle) nhandle = rethandle+1;
    } else if (fh->inode == inode && fh->volume == nwpath->volume){
      /* Dieser hat Vorrang */
      if (fh->f) closedir(fh->f);
      fh->f         = NULL;
      fh->timestamp = akttime;
      nhandle       = rethandle+1;
      break;
    } else if (fh->timestamp < last_time){
      thandle   = rethandle+1;
      last_time = fh->timestamp;
    }
  }
  if (!nhandle){
   if (anz_dirhandles < MAX_DIRHANDLES) {
      fh=&(dir_handles[anz_dirhandles]);
      rethandle = ++anz_dirhandles;
    } else {
      fh=&(dir_handles[thandle-1]);
      if (fh->f) closedir(fh->f);
      fh->f         = NULL;
      rethandle = thandle;
    }
  } else rethandle=nhandle;

  /* init dir_handle */
  fh=&(dir_handles[rethandle-1]);
  strcpy(fh->unixname, build_unix_name(nwpath, 0));
  if ((fh->f        = opendir(fh->unixname)) != (DIR*) NULL){
    fh->kpath       = fh->unixname + strlen(fh->unixname);
    fh->volume      = nwpath->volume;
    fh->vol_options = vols[fh->volume].options;
    fh->inode       = inode;
    fh->timestamp   = akttime;
  } else {
    fh->f           = (DIR*)NULL;
    fh->unixname[0] = '\0';
    fh->vol_options = 0;
    fh->kpath       = (char*)NULL;
    rethandle       = -0x9c;
  }
  return(rethandle);
}

static int free_dir_handle(int dhandle)
{
  if (dhandle > 0 && --dhandle < anz_dirhandles) {
    DIR_HANDLE  *fh=&(dir_handles[dhandle]);
    if (fh->f != (DIR*) NULL) {
      closedir(fh->f);
      fh->f = (DIR*)NULL;
    }
    while (anz_dirhandles && dir_handles[anz_dirhandles-1].f == (DIR*)NULL)
      anz_dirhandles--;
    return(0);
  }
  return(-0x88); /* wrong dir_handle */
}

void set_default_guid(void)
{
  seteuid(0);
  if (setegid(default_gid) < 0 || seteuid(default_uid) < 0) {
    fprintf(stderr, "Cannot set default gid=%d and uid=%d\nABORTET!!\n" ,
       default_gid, default_uid);
    exit(1);
  }
}

void set_guid(int gid, int uid)
{
  if ( gid < 0 || uid < 0
     || seteuid(0)
     || setegid(gid) == -1
     || seteuid(uid) == -1 ) {
    DPRINTF(("SET GID=%d, UID=%d failed\n", gid, uid));
    set_default_guid();
  } else XDPRINTF((5,0,"SET GID=%d, UID=%d OK", gid, uid));
}

char *conn_get_nwpath_name(NW_PATH *p)
/* for debugging */
{
static char nwpathname[300];
  char volname[100];
  if (p->volume < 0 || p->volume >= used_vols) {
    sprintf(volname, "<%d=NOT-OK>", (int)p->volume);
  } else strcpy(volname, vols[p->volume].sysname);
  sprintf(nwpathname, "%s:%s%s", volname, p->path, p->fn);
  return(nwpathname);
}

static int x_str_match(uint8 *s, uint8 *p)
{
  uint8    pc, sc;
  uint     state = 0;
  uint8    anf, ende;
  int  not = 0;
  uint found = 0;
  while ( (pc = *p++) != 0) {
    switch (state){
      case 0 :
      switch  (pc) {
        case 255:   if (*p == '*' || *p == '?') continue;
                    break;

        case '\\': /* beliebiges Folgezeichen */
                    if (*p++ != *s++) return(0);
                    break;

        case '?' :  if ((sc = *s++) == '.') {
                      uint8 *pp=p;
                      while (*pp) {
                        if (*pp++ == '.') p=pp;
                      }
                    } else if (!sc) return(1); /* one character */
                    break;

        case  '.' :
#if 0
                    if (!*s && !*p) return(1);  /* point at end */
#else
                    if (!*s && (!*p || *p == '*' || *p == '?')) return(1);
#endif
                    if (pc != *s++) return(0);
                    if (*p == '*') return(1);
                    break;

        case '*' :
#if 0
                   if (!*p) {
                     uint8 *ss=s;
                     while (*ss) if (*ss++ == '.') return(0);
                     return(1);   /* last star */
                   }
#else
                   if (!*p) return(1);
#endif
                   while (*s){
                     if (x_str_match(s, p) == 1) return(1);
                     ++s;
                   }
                   return((*p == '.' &&  *(p+1) == '*') ? 1 : 0);

        case '[' :  if ( (*p == '!') || (*p == '^') ){
                       ++p;
                       not = 1;
                     }
                     state = 1;
                     continue;

        default  :  if (pc != *s++) return(0); /* normal char */
                    break;

      }  /* switch */
      break;

      case   1  :   /*  Bereich von Zeichen  */
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
  return ( (*s) ? 0 : 1);
}

static int str_match(uint8 *s, uint8 *p, uint8 options)
{
  uint8 *ss=s;
  int   len=0;
  int   pf=0;
  for (; *ss; ss++){
    if (*ss == '.') {
      if (pf++) return(0); /* Kein 2. Punkt */
      len=0;
    } else {
      if ((pf && len > 3) || len > 8) return(0);
      if (options & 1){   /* only downshift chars */
        if (*ss >= 'A' && *ss <= 'Z') return(0);
      } else {            /* only upshift chars   */
        if (*ss >= 'a' && *ss <= 'z') return(0);
      }
    }
  }
  return(x_str_match(s, p));
}

typedef struct {
  int        attrib;
  struct stat statb;
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
  uint8          soptions;
  FUNC_SEARCH    fs_local;
  if (!fs) fs = &fs_local;
  fs->attrib  = attrib;
  if (volume < 0 || volume >= used_vols) return(-1); /* something wrong */
  else  soptions = vols[volume].options;
  strcpy(entry,  nwpath->fn);
  if (soptions & 1) downstr(entry);   /* now downshift chars */
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
        okflag = (name[0] != '.' &&
                 (  (entry[0] == '*' && entry[1] == '\0')
                 || (!strcmp(name, entry))
                 || str_match(name, entry, soptions)));
        if (okflag) {
          *kpath = '\0';
          strcpy(kpath, name);
          if (!stat(xkpath, &(fs->statb))) {
            okflag = (  ( ( (fs->statb.st_mode & S_IFMT) == S_IFDIR) &&  (attrib & 0x10))
                  ||    ( ( (fs->statb.st_mode & S_IFMT) != S_IFDIR) && !(attrib & 0x10)));
            if (okflag){
              strcpy(nwpath->fn, name);
              if (soptions & 1) upstr(nwpath->fn);
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
  uint8          soptions;
  if (volume < 0 || volume >= used_vols) return(0); /* something wrong */
  else  soptions = vols[volume].options;
  strcpy(entry,  nwpath->fn);
  if (soptions & 1) downstr(entry);   /* now downshift chars */

  nwpath->fn[0] = '\0';
  strcpy(xkpath, build_unix_name(nwpath, 1|2));
  XDPRINTF((5,0,"get_dir_entry attrib=0x%x path:%s:, xkpath:%s:, entry:%s:",
                          attrib, nwpath->path, xkpath, entry));

  if ((f=opendir(xkpath)) != (DIR*)NULL) {
    char *kpath=xkpath+strlen(xkpath);
    *kpath++ = '/';
    if (*sequence == MAX_U16) *sequence = 0;
    else seekdir(f, (long)*sequence);

    while ((dirbuff = readdir(f)) != (struct dirent*)NULL){
      okflag = 0;
      if (dirbuff->d_ino) {
        uint8 *name=(uint8*)(dirbuff->d_name);
        okflag = (name[0] != '.' &&
                 (  (entry[0] == '*' && entry[1] == '\0')
                 || (!strcmp(name, entry))
                 || str_match(name, entry, soptions)));
        if (okflag) {
          *kpath = '\0';
          strcpy(kpath, name);
          if (!stat(xkpath, statb)) {
            okflag = (  ( ( (statb->st_mode & S_IFMT) == S_IFDIR) &&  (attrib & 0x10))
                  ||    ( ( (statb->st_mode & S_IFMT) != S_IFDIR) && !(attrib & 0x10)));
            if (okflag){
              strcpy(nwpath->fn, name);
              if (soptions & 1) upstr(nwpath->fn);
              XDPRINTF((5,0,"FOUND=:%s: attrib=0x%x", nwpath->fn, statb->st_mode));
              break; /* ready */
            }
          } else okflag = 0;
        }
        XDPRINTF((6,0, "NAME=:%s: OKFLAG %d", name, okflag));
      }  /* if */
    } /* while */
    *sequence = (int) telldir(f);
    closedir(f);
  } /* if */
  return(okflag);
}

static int get_dh_entry(DIR_HANDLE *dh,
                        uint8  *search,
                        int    *sequence,
                        int    attrib,
                        struct stat *statb)

/* returns 1 if OK and 0 if not OK */
{
  DIR            *f     = dh->f;
  int            okflag = 0;
  if (f != (DIR*)NULL) {
    struct  dirent *dirbuff;
    uint8   entry[256];
    strmaxcpy(entry, search, 255);

    if (dh->vol_options & 1)   downstr(entry);
    if ( (uint16)*sequence == MAX_U16)  *sequence = 0;
    seekdir(f, (long) *sequence);

    XDPRINTF((5,0,"get_dh_entry attrib=0x%x path:%s:, entry:%s:",
                attrib, dh->unixname, entry));

    while ((dirbuff = readdir(f)) != (struct dirent*)NULL){
      okflag = 0;
      if (dirbuff->d_ino) {
        uint8 *name=(uint8*)(dirbuff->d_name);
        okflag = (name[0] != '.' && (
                        (!strcmp(name, entry)) ||
                        (entry[0] == '*' && entry[1] == '\0')
                     || str_match(name, entry, dh->vol_options)));

        if (okflag) {
          strcpy(dh->kpath, name);
          XDPRINTF((5,0,"get_dh_entry Name=%s unixname=%s",
                                  name, dh->unixname));

          if (!stat(dh->unixname, statb)) {
            okflag = ( (( (statb->st_mode & S_IFMT) == S_IFDIR) &&  (attrib & 0x10))
                     || (((statb->st_mode & S_IFMT) != S_IFDIR) && !(attrib & 0x10)));
            if (okflag){
              strcpy(search, name);
              if (dh->vol_options & 1) upstr(search);
              break; /* ready */
            }
          } else okflag = 0;
        }
      }  /* if */
    } /* while */
    dh->kpath[0] = '\0';
    *sequence = (int) telldir(f);
  } /* if */
  return(okflag);
}

void conn_build_path_fn( uint8 *vol,
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
     else if (*data > 0x60 && *data < 0x7b) {
       *p1++ = *data - 0x20;  /* all is upshift */
     } else if (*data == 0xaa|| *data == '*' ) {
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
       p1 = path;
     } else *p1++ = *data;
     data++;
   }
   *p1 = '\0';
   if (fn != NULL) {  /* if with filename     */
     if (p != NULL){  /* exist directory-path  */
       strcpy(fn, p);
       *p = '\0';
     } else {         /* only filename */
       strcpy(fn, path);
       *path= '\0';
     }
   }
}

static int build_path( NW_PATH *path,
                       uint8   *data,
                       int     len,
                       int     only_dir)
/*
 * fills path structur with the right values
 * if only_dir > 0, then the full path will be interpreted
 * as directory, in the other way, the last segment of path
 * will be interpreted as fn.
 * returns -0x98, if volume is wrong
 */
{
  uint8 vol[20];
  conn_build_path_fn(vol, path->path,
                     (only_dir) ? (uint8)NULL
                                : path->fn,
                     &(path->has_wild),
                     data, len);

  path->volume = -1;
  if (only_dir) path->fn[0] = '\0';

  if (vol[0]) {  /* there is a volume in path */
    int j = used_vols;
    while (j--) {
      if (!strcmp(vols[j].sysname, vol)) {
        path->volume = j;
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
  int result = -0x9c;   /* wrong path */

  while (j++ < (int)used_dirs){
    if (d->inode && d->volume == nwpath->volume
                 && !strcmp(nwpath->path, d->path)){
      return(d->inode);
    }
    d++;
  } /* while */
  if (!stat(build_unix_name(nwpath, 1 | 2 ), &stbuff)
      && (stbuff.st_mode & S_IFMT) == S_IFDIR) result=stbuff.st_ino;
  else {
    XDPRINTF((4,0, "NW_PATH_OK failed:`%s`", conn_get_nwpath_name(nwpath)));
  }
  return(result);
}

static int build_verz_name(NW_PATH *nwpath,    /* gets complete path     */
                           int     dir_handle) /* search with dirhandle  */

/* return -completition code or inode */
{
   uint8      searchpath[256];
   uint8      *p=searchpath;
   int        completition=0;

   strcpy(searchpath, nwpath->path);  /* save path */

   if (nwpath->volume > -1) { /* absolute path */
     nwpath->path[0] = '\0';
   } else  {  /* volume not kwown yet, I must get it about dir_handle */
     if (dir_handle > 0 &&
       --dir_handle < (int)used_dirs && dirs[dir_handle].inode){
       nwpath->volume = dirs[dir_handle].volume;
       if (searchpath[0] == '/') { /* absolute path */
         p++;
         nwpath->path[0] = '\0';
       } else  /* get path from dir_handle */
         strcpy(nwpath->path, dirs[dir_handle].path);
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
       } else if (state == 9) completition= -0x9c; /* something wrong */
       else if (state < 14){
         if (w == '.') return(-0x9c);
         else if (w == '/') state = 30;
         else state++;
       } else if (state == 14) completition= -0x9c; /* something wrong  */
       else if (state == 20){
         if (w == '/') state = 30;
         else if (w != '.') completition= -0x9c; /* something wrong  */
       }
       if (state == 30 || !*p) { /* now action */
         uint8 *xpath=a;
         int   len = (int)(p-a);
         if (len && state == 30) --len; /* '/' stoert hier */
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
   if (!completition) completition = nw_path_ok(nwpath);
   return(completition);
}

int conn_get_kpl_path(NW_PATH *nwpath, int dirhandle,
                          uint8 *data, int len, int only_dir)
/*
 * if ok then the inode of dir will be returned
 * else a negativ errcode will be returned
 */
{
   int completition                = build_path(nwpath, data, len, only_dir);
   if (!completition) completition = build_verz_name(nwpath, dirhandle);
   return(completition);
}

static uint16 un_date_2_nw(time_t time, uint8 *d)
{
  struct tm  *s_tm=localtime(&time);
  uint16  xdate=s_tm->tm_year - 80;
  xdate <<= 4;
  xdate |= s_tm->tm_mon+1;
  xdate <<= 5;
  xdate |= s_tm->tm_mday;
  if (d) U16_TO_BE16(xdate, d);
  return(xdate);
}

static time_t nw_2_un_time(uint8 *d, uint8 *t)
{
  uint16 xdate = GET_BE16(d);
  uint16 xtime = (t != (uint8*) NULL) ? GET_BE16(t) : 0;

  int year     = (xdate >> 9) + 80;
  int month    = (xdate >> 5) & 0x0F;
  int day      = xdate & 0x1f;
  int hour     = xtime >> 11;
  int minu     = (xtime >> 5) & 0x3f;
  int sec      =  xtime & 0x1f;
  struct tm    s_tm;
  s_tm.tm_year   = year;
  s_tm.tm_mon    = month-1;
  s_tm.tm_mday   = day;
  s_tm.tm_hour   = hour;
  s_tm.tm_min    = minu;
  s_tm.tm_sec    = sec;
  return(mktime(&s_tm));
}

static uint16 un_time_2_nw(time_t time, uint8 *d)
{
  struct tm  *s_tm=localtime(&time);
  uint16  xdate=s_tm->tm_hour;
  xdate <<= 6;
  xdate |= s_tm->tm_min;
  xdate <<= 5;
  xdate |= (s_tm->tm_sec / 2);
  if (d) U16_TO_BE16(xdate, d);
  return(xdate);
}

static int get_file_attrib(NW_FILE_INFO *f, struct stat *stb,
                           NW_PATH *nwpath)
{
  XDPRINTF((5,0, "get_file_attrib of %s", conn_get_nwpath_name(nwpath) ));
  strncpy(f->name, nwpath->fn, sizeof(f->name));
  /* Attribute */
  /* 0x20   Archive Flag */
  /* 0x80   Sharable     */  /* TLINK (TCC 2.0) don't like it ???? */
#if 1
  if (!strcmp(nwpath->fn, "TURBOC.$LN")) f->attrib = 0x20;
  else f->attrib = 0x80;
#else
  f->attrib = 0x20;
#endif
  f->ext_attrib  = 0;
  un_date_2_nw(stb->st_mtime, f->create_date);
  un_date_2_nw(stb->st_atime, f->acces_date );
  un_date_2_nw(stb->st_mtime, f->modify_date);
  un_time_2_nw(stb->st_mtime, f->modify_time);
  U32_TO_BE32(stb->st_size,   f->size);
  return(1);
}

static int get_dir_attrib(NW_DIR_INFO *d, struct stat *stb,
                         NW_PATH *nwpath)
{
  XDPRINTF((5,0, "get_dir_attrib of %s", conn_get_nwpath_name(nwpath)));
  strncpy(d->name, nwpath->fn, sizeof(d->name));

  d->attrib     = 0x10; /* Verzeichnis         */
  d->ext_attrib = 0xff; /* effektive rights ?? */

  un_date_2_nw(stb->st_mtime, d->create_date);
  un_time_2_nw(stb->st_mtime, d->create_time);

  U32_TO_BE32(1L, d->owner_id);
  d->access_right_mask = 0;
  d->reserved          = 0;
  U16_TO_BE16(0, d->next_search);
  return(1);
}


int nw_creat_open_file(int dir_handle, uint8 *data, int len,
            NW_FILE_INFO *info, int attrib, int access, int creatmode)
/*
 * creatmode: 0 = open, 1 = creat, 2 = creatnew
 * attrib ??
 * access: 0x1=read, 0x2=write
 */
{
   int fhandle=new_file_handle();

   if (fhandle){
     FILE_HANDLE *fh=&(file_handles[fhandle-1]);
     NW_PATH nwpath;
     int completition = conn_get_kpl_path(&nwpath, dir_handle, data, len, 0);

#ifdef TEST_FNAME
       int got_testfn = 0;
       if (!nw_debug){
         if (strstr(nwpath.fn, TEST_FNAME)){
           nw_debug = 99;
           got_testfn++;
         }
       }
#endif

     if (completition > -1) {
       struct stat stbuff;
       completition = -0xff;  /* no File  Found */
       strcpy(fh->name, build_unix_name(&nwpath, 0));

       if (creatmode) {  /* creat File  */
         if (creatmode & 0x2) { /* creatnew */
           if (!stat(fh->name, &stbuff)) {
             XDPRINTF((5,0,"CREAT File exist!! :%s:", fh->name));
             fh->fd   = -1;
             completition =   -0x85; /* No Priv */
           } else {
             XDPRINTF((5,0,"CREAT FILE:%s: Handle=%d", fh->name, fhandle));
             fh->fd   = creat(fh->name, 0777);
             if (fh->fd < 0) completition = -0x84; /* no create Rights */
           }
         } else {
           XDPRINTF((5,0,"CREAT FILE, ever with attrib:0x%x, access:0x%x, fh->name:%s: handle:%d",
             attrib,  access, fh->name, fhandle));
           fh->fd = open(fh->name, O_CREAT|O_TRUNC|O_RDWR, 0777);
           if (fh->fd < 0) completition = -0x85; /* no delete /create Rights */
         }

         if (fh->fd > -1) {
           close(fh->fd);
           fh->fd   = open(fh->name, O_RDWR);
           fh->offd = 0L;
           stat(fh->name, &stbuff);
         }
       } else {
         int statr = stat(fh->name, &stbuff);
         int acm  = (access & 2) ? (int) O_RDWR /*|O_CREAT*/ : (int)O_RDONLY;
         if ( (!statr && (stbuff.st_mode & S_IFMT) != S_IFDIR)
              || (statr && (acm & O_CREAT))){
            XDPRINTF((5,0,"OPEN FILE with attrib:0x%x, access:0x%x, fh->name:%s: fhandle=%d",attrib,access, fh->name, fhandle));
            fh->fd = open(fh->name, acm, 0777);
            fh->offd = 0L;
            if (fh->fd > -1) {
              if (statr) stat(fh->name, &stbuff);
            } else completition = -0x9a;
         }

       }

       if (fh->fd > -1) {
         get_file_attrib(info, &stbuff, &nwpath);
#ifdef TEST_FNAME
         if (got_testfn) test_handle = fhandle;
#endif
         return(fhandle);
       }
     }

     XDPRINTF((5,0,"OPEN FILE not OK ! fh->name:%s: fhandle=%d",fh->name, fhandle));
     free_file_handle(fhandle);
#ifdef TEST_FNAME
     if (got_testfn) {
       test_handle = -1;
       nw_debug    = -99;
     }
#endif
     return(completition);
   } else return(-0x81); /* no more File Handles */
}

static int do_delete_file(NW_PATH *nwpath, FUNC_SEARCH *fs)
{
  char           unname[256];
  strcpy(unname, build_unix_name(nwpath, 0));
  XDPRINTF((5,0,"DELETE FILE unname:%s:", unname));
  if (!unlink(unname)) return(0);
  return(-0x8a); /* NO Delete Privileges */
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

int nw_chmod_datei(int dir_handle, uint8 *data, int len, int modus)
{
  char          unname[256];
  struct stat   stbuff;
  int           completition=-0x9c;
  NW_PATH nwpath;
  build_path(&nwpath, data, len, 0);
  if (nwpath.fn[0] != '.') { /* Files with . at the beginning are not ok */
    completition = build_verz_name(&nwpath, dir_handle);
  }
  if (completition < 0) return(completition);
  strcpy(unname, build_unix_name(&nwpath, 2));
  XDPRINTF((5,0,"CHMOD DATEI unname:%s:", unname));
  if (!stat(unname, &stbuff)){
    return(0);
  }
  return(-0x9c); /* wrong path */
}

int nw_close_datei(int fhandle)
{
  if (fhandle > 0 && (fhandle <= anz_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle-1]);
    if (fh->fd > -1) {
      close(fh->fd);
      fh->fd = -1;
      if (fh->tmodi > 0L) {
        struct utimbuf ut;
        ut.actime = ut.modtime = fh->tmodi;
        utime(fh->name, &ut);
      }
#ifdef TEST_FNAME
      if (fhandle == test_handle) {
        test_handle = -1;
        nw_debug    = -99;
      }
#endif
      return(free_file_handle(fhandle));
    }
  }
  return(-0x88); /* wrong filehandle */
}


int nw_read_datei(int fhandle, uint8 *data, int size, uint32 offset)
{
  if (fhandle > 0 && (--fhandle < anz_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle]);
    if (fh->fd > -1) {
      if (fh->offd != (long)offset)
        fh->offd=lseek(fh->fd, offset, SEEK_SET);
      if (fh->offd > -1L) {
         size = read(fh->fd, data, size);
         fh->offd+=(long)size;
      } else size = -1;
      return(size);
    }
  }
  return(- 0x88); /* wrong filehandle */
}

int nw_seek_datei(int fhandle, int modus)
{
  if (fhandle > 0 && (--fhandle < anz_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle]);
    if (fh->fd > -1) {
      int size=-0xfb;
      if (!modus) {
        if ( (size=fh->offd=lseek(fh->fd, 0L, SEEK_END)) < 0L)
            size = -1;
      }
      return(size);
    }
  }
  return(-0x88); /* wrong filehandle */
}


int nw_write_datei(int fhandle, uint8 *data, int size, uint32 offset)
{
  if (fhandle > 0 && (--fhandle < anz_fhandles)) {
    FILE_HANDLE *fh=&(file_handles[fhandle]);
    if (fh->fd > -1) {
      if (fh->offd != (long)offset)
          fh->offd = lseek(fh->fd, offset, SEEK_SET);
      if (size) {
        if (fh->offd > -1L) {
          size = write(fh->fd, data, size);
          fh->offd+=(long)size;
        } else size = -1;
        return(size);
      } else {  /* strip FILE */
      /* TODO: for LINUX */
        struct flock flockd;
        int result=  /* -1 */        0;
        flockd.l_type   = 0;
        flockd.l_whence = SEEK_SET;
        flockd.l_start  = offset;
        flockd.l_len    = 0;
#if HAVE_TLI
        result = fcntl(fh->fd, F_FREESP, &flockd);
        XDPRINTF((5,0,"File %s is stripped, result=%d", fh->name, result));
#endif
        return(result);
      }
    }
  }
  return(- 0x88); /* wrong filehandle */
}

int nw_server_copy(int qfhandle, uint32 qoffset,
                   int zfhandle, uint32 zoffset,
                   uint32 size)
{
  if (qfhandle > 0 && (--qfhandle < anz_fhandles)
    && zfhandle > 0 && (--zfhandle < anz_fhandles) ) {
    FILE_HANDLE *fhq=&(file_handles[qfhandle]);
    FILE_HANDLE *fhz=&(file_handles[zfhandle]);
    int retsize = -1;
    if (fhq->fd > -1 && fhz->fd > -1) {
      char buff[2048];
      int  wsize;
      if (lseek(fhq->fd, qoffset, SEEK_SET) > -1L &&
          lseek(fhz->fd, zoffset, SEEK_SET) > -1L) {
        retsize = 0;
        while (size && !retsize) {
          int xsize = read(fhq->fd, buff, min(size, (uint32)sizeof(buff)));
          if (xsize > 0){
            if ((wsize =write(fhz->fd, buff, xsize)) != xsize) {
              retsize = -0x1;  /* out of Disk SPace */
              break;
            } else {
              size -= (uint32)xsize;
              retsize += wsize;
            }
          } else {
            if (xsize < 0) retsize=-0x93; /* no read privilegs */
            break;
          }
        }
      }
      fhq->offd = -1L;
      fhz->offd = -1L;
/*
      if (!retsize) (retsize=fhz->offd=lseek(fhz->fd, 0L, SEEK_END));
*/
      return(retsize);
    }
  }
  return(- 0x88); /* wrong filehandle */
}


int nw_lock_datei(int fhandle, int offset, int size, int do_lock)
{
  if (fhandle > 0 && (--fhandle < anz_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle]);
    if (fh->fd > -1) {
      struct flock flockd;
      int result;
      flockd.l_type   = (do_lock) ? F_WRLCK : F_UNLCK;
      flockd.l_whence = SEEK_SET;
      flockd.l_start  = offset;
      flockd.l_len    = size;
      result = fcntl(fh->fd, F_SETLK, &flockd);
      if (!result) return(0);
      else return(-0x21); /* LOCK Violation */
    }
  }
  return(-0x88); /* wrong filehandle */
}

int nw_mk_rd_dir(int dir_handle, uint8 *data, int len, int mode)
{
  NW_PATH nwpath;
  int completition = conn_get_kpl_path(&nwpath, dir_handle, data, len, !mode);

  if (completition > -1) {
    char unname[256];
    strcpy(unname, build_unix_name(&nwpath, 2));
#if 0
    if (unname[0] && unname[1]) {
      char *p=unname+strlen(unname)-1;
      if (*p=='/') *p = '\0';
    }
#endif

    if (mode) {
      XDPRINTF((5,0,"MKDIR dirname:%s:", unname));
      if (!mkdir(unname, 0777)) return(0);
      completition = -0x84; /* No Create Priv.*/  /* -0x9f Direktory Aktive */
    } else { /* rmdir */
      XDPRINTF((5,0,"RMDIR dirname:%s:", unname));
      if (!rmdir(unname)) {
        NW_DIR *d=&(dirs[0]);
        int  j = 0;
        while (j++ < (int)used_dirs){
          if (d->inode == completition) d->inode = 0;
          d++;
        }
        completition = 0;
      } else if (errno & EEXIST)
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
  if (!completition > -1){
    completition=conn_get_kpl_path(&zielpath,    zdirhandle, z, zlen, 0);
    if (completition > -1){
      char unquelle[256];
      char unziel[256];
      strcpy(unquelle, build_unix_name(&quellpath,0));
      strcpy(unziel,   build_unix_name(&zielpath,0));
      if (!link(unquelle, unziel)){
        if (unlink(unquelle)) {
          completition=-0x9c;
          /* TODO: here completition must be no pernmissions */
          unlink(unziel);
        }
      } else {
        if (errno & EEXIST)
          completition=-0x91;    /* allready exist */
        else if (errno & EXDEV)
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
    new_str(dir->path, path);
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

int nw_init_connect(void)
/* Cann be called when ever you want */
{
  uint8    *login   = (uint8*) "LOGIN/";
  NW_PATH  nwlogin;
  FILE *f= open_nw_ini();
  if (f != (FILE*) NULL){
    uint8  buff[256];
    struct stat stbuff;
    int    what;
    int k        = MAX_NW_DIRS;
    NW_DIR *d    = &(dirs[0]);
    strcpy(nwlogin.path, login);
    nwlogin.fn[0]   = '\0';
    nwlogin.volume  = 0;

    while (k--) {
      if (connect_is_init) xfree(d->path);
      else d->path = NULL;
      d->volume    = 0;
      d->inode     = 0;
      d->is_temp   = 0;
      d->drive     = 0;
      d++;
    }
    if (!connect_is_init) {
      k = -1;
      connect_is_init++;
      while (++k < MAX_NW_VOLS) {
        vols[k].unixname = NULL;
        vols[k].sysname  = NULL;
        vols[k].options  = 0;
      }
    } else {
      k = 0;
      while (k++ < anz_fhandles)   free_file_handle(k);
      k = 0;
      while (k++ < anz_dirhandles) free_dir_handle(k);
    }
    used_vols = 0;
    while (0 != (what = get_ini_entry(f, 0, (char*)buff, sizeof(buff)))) {
      if ( what == 1 && used_vols < MAX_NW_VOLS && strlen(buff) > 3){
        uint8 sysname[256];
        uint8 unixname[256];
        char  optionstr[256];
        char  *p;
        int   len;
        int   founds = sscanf((char*)buff, "%s %s %s",sysname, unixname, optionstr);
        if (founds > 1) {
          new_str(vols[used_vols].sysname,  sysname);
          len = strlen(unixname);
          if (unixname[len-1] != '/') {
            unixname[len++] = '/';
            unixname[len]   = '\0';
          }
          vols[used_vols].options = 0;
          new_str(vols[used_vols].unixname, unixname);
          if (founds > 2) {
            for (p=optionstr; *p; p++) {
              switch (*p) {
                case 'k' : vols[used_vols].options |= 1;
                default : break;
              }
            }
          }
          used_vols++;
        }
      } else if (what == 10) { /* GID */
        default_gid = atoi(buff);
      } else if (what == 11) { /* UID */
        default_uid = atoi(buff);
      } else if (what == 103) { /* Debug */
        nw_debug = atoi(buff);
      }
    }
    fclose(f);

    if (stat(build_unix_name(&nwlogin, 0), &stbuff)) {
      errorp(0, "Stat error LOGIN Directory, Abort !!",
            "UnixPath=`%s`", build_unix_name(&nwlogin, 0));
      return(-1);
    }

    (void)change_dir_entry(&(dirs[0]), 0, nwlogin.path, stbuff.st_ino,
                       0, 0, 1, 0);
    /* first Handle must be known und must not be temp */
    /* and has no Drive-Character                      */
    used_dirs      = 1;
    anz_fhandles   = 0;
    anz_dirhandles = 0;
    return(0);
  } else return(-1);
}

int nw_free_handles(int task)
/*
 * if task==0 then all is initialized
 * else the temp handles of the actuell task and greater
 * are deleted. I hope it is right. !??
 */
{
  if (!task) return(nw_init_connect());
  else {
    NW_DIR *d = &(dirs[0]);
    int     k = used_dirs;
    while (k--) {
      if (d->is_temp && d->task >= task) {
        xfree(d->path);
        d->volume    = 0;
        d->inode     = 0;
        d->is_temp   = 0;
        d->drive     = 0;
      }
      d++;
    }
  }
  return(0);
}

int insert_new_dir(NW_PATH *nwpath, int inode, int drive, int is_temp, int task)
{
  int j              = 0;
  time_t lowtime     = time(NULL);
  int    freehandle  = 0;
  int    timedhandle = 0;

  /* first look, wether drive is allready in use */
  for (j = 0; j < (int)used_dirs; j++) {
    NW_DIR *d = &(dirs[j]);
    if (d->inode && !is_temp && !d->is_temp && (int)d->drive == drive) {
      (void)change_dir_entry(d, nwpath->volume, nwpath->path, inode, drive, is_temp, 1, task);
      return(++j);
    } else if (!d->inode) freehandle = j+1;
    else if (d->is_temp && d->timestamp < lowtime) {
      timedhandle = j+1;
      lowtime     = d->timestamp;
    }
  }
  if (!freehandle && used_dirs < MAX_NW_DIRS) freehandle = ++used_dirs;
  if (!freehandle) freehandle = timedhandle;
  if (freehandle){
    (void)change_dir_entry(&(dirs[freehandle-1]),
          nwpath->volume, nwpath->path, inode,
          drive, is_temp, 1, task);
    while (used_dirs > freehandle && !dirs[used_dirs-1].inode) used_dirs--;
    return(freehandle);
  } else return(-0x9d);  /* no dir Handles */
}

int nw_search(uint8 *info,
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
#if 1
         if ( (stbuff.st_mode & S_IFMT) == S_IFDIR) {
#else
         if (search_attrib & 0x10) {
#endif
           get_dir_attrib((NW_DIR_INFO*)info, &stbuff,
                   &nwpath);
         } else {
           get_file_attrib((NW_FILE_INFO*)info, &stbuff,
                &nwpath);
         }
         return(searchsequence);
      } else return(-0xff); /* not found */
   } else return(completition); /* wrong path */
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
     if (get_dh_entry(dh,
                      nwpath.fn,
                      &searchsequence,
                      search_attrib,
                      &stbuff)){

#if 1
         if ( (stbuff.st_mode & S_IFMT) == S_IFDIR) {
#else
         if (search_attrib & 0x10) {
#endif
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

int nw_alloc_dir_handle( int    dir_handle,  /* Suche ab Pfad dirhandle   */
                         uint8  *data,       /* zusaetzl. Pfad             */
                         int    len,         /* L„nge DATA                */
                         int    driveletter, /* A .. Z normal             */
                         int    is_temphandle, /* temporaeres Handle 1     */
                                               /* spez. temp Handle  2    */
                         int    task)          /* Prozess Task            */
{
   NW_PATH nwpath;
   int inode=conn_get_kpl_path(&nwpath, dir_handle, data, len, 1);
   if (inode > -1)
     inode = insert_new_dir(&nwpath, inode, driveletter, is_temphandle, task);
   XDPRINTF((5,0,"Allocate %shandle:%s, Handle=%d, drive=%d, result=0x%x",
       (is_temphandle) ? "Temp" : "Perm", conn_get_nwpath_name(&nwpath),
               dir_handle, driveletter, inode));
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
 * RETURN=errcode ( <0 ) or ACCES Rights
*/

{
   NW_PATH nwpath;
   int completition = conn_get_kpl_path(&nwpath, dir_handle, data, len, 1);
   if (completition > -1) {
     XDPRINTF((5,0,"NW_OPEN_DIR: completition = 0x%x; nwpath= %s",
           (int)completition, conn_get_nwpath_name(&nwpath) ));

     completition = new_dir_handle((ino_t)completition, &nwpath);
     if (completition > -1) {
       DIR_HANDLE *fh  = &(dir_handles[completition-1]);
       *volume         = fh->volume;
       *dir_id         = completition;
       *searchsequence = MAX_U16;
       completition    = 0xff; /* Alle Rechte */
     }
     XDPRINTF((5,0,"NW_OPEN_DIR_2: completition = 0x%x",
               (int)completition));
   } else {
     XDPRINTF((4,0,"NW_OPEN_DIR failed: completition = 0x%x", (int)completition));
   }
   return(completition);
}

int nw_free_dir_handle(int dir_handle)
{
  if (dir_handle && --dir_handle < (int)used_dirs) {
    NW_DIR *d=&(dirs[dir_handle]);
    if (!d->inode) return(-0x9b); /* wrong handle */
    else {
      d->inode = 0;
      xfree(d->path);
    }
    return(0);
  } else return(-0x9b); /* wrong handle */
}

int nw_set_dir_handle(int targetdir, int dir_handle,
                          uint8 *data, int len, int task)
/* targetdirs gets path of dirhandle + data */
{
  NW_PATH nwpath;
  int inode = conn_get_kpl_path(&nwpath, dir_handle, data, len, 1);
  if (inode > -1){
    if (targetdir > 0 && --targetdir < used_dirs
      && dirs[targetdir].is_temp != 2) { /* not a spez. temphandle */
      XDPRINTF((5,0,"Change dhandle:%d -> `%s`", targetdir+1, conn_get_nwpath_name(&nwpath)));
      return(change_dir_entry(&dirs[targetdir], nwpath.volume, nwpath.path, inode, -1, -1, 0, task));
      /* here the existing handle is only modified */
    } else return(-0x9b); /* BAD DIR Handle */
  }
  return(inode);  /* invalid PATH */
}

int nw_get_directory_path(int dir_handle, uint8 *name)
{
  int     result   = -0x9b;
  name[0] = '\0';
  if (dir_handle > 0 && --dir_handle < (int)used_dirs) {
    int volume = dirs[dir_handle].volume;
    if (volume > -1 && volume < used_vols){
      result=sprintf((char*)name, "%s:%s", vols[volume].sysname, dirs[dir_handle].path);
      if (name[result-1] == '/') name[--result] = '\0';
    } else result = -0x98;
  }
  XDPRINTF((5,0,"nw_get_directory_path:%s: Handle=%d, result=0x%x", name, dir_handle+1, result));
  return(result);
}

int nw_get_vol_number(int dir_handle)
/* Get Volume Nummmer with Handle */
{
  int     result   = -0x9b; /* wrong handle */
  if (dir_handle > 0 && --dir_handle < (int)used_dirs) {
    result = dirs[dir_handle].volume;
    if (result < 0 || result >= used_vols) result = -0x98; /* wrong volume */
  }
  XDPRINTF((5,0,"nw_get_vol_number:0x%x: von Handle=%d", result, dir_handle+1));
  return(result);
}

int nw_get_volume_number(uint8 *volname, int namelen)
/* Get Volume Number with name */
/* returns Volume Nummer or if error < 0 */
{
  int result = -0x98; /* Volume not exist */
  uint8   vname[255];
  int j = used_vols;
  strmaxcpy((char*)vname, (char*)volname, namelen);
  upstr(vname);
  while (j--) {
    if (!strcmp(vols[j].sysname, vname)) {
      result = j;
      break;
    }
  }
  XDPRINTF((5,0,"GET_VOLUME_NUMBER of:%s: result = 0x%x", vname, result));
  return(result);
}

int nw_get_volume_name(int volnr, uint8 *volname)
/* returns < 0 if error, else len of volname  */
{
  int  result = -0x98; /* Volume not exist */;
  if (volnr < used_vols) {
    if (volname != NULL) {
      strcpy(volname, vols[volnr].sysname);
      result = strlen(volname);
    } else result= strlen(vols[volnr].sysname);
  } else {
    if (NULL != volname) *volname = '\0';
    if (volnr < MAX_NW_VOLS) result=0;
  }
  if (nw_debug > 4) {
    char xvolname[10];
    if (!volname) {
      volname = xvolname;
      *volname = '\0';
    }
    XDPRINTF((5,0,"GET_VOLUME_NAME von:%d = %s: ,result=0x%x", volnr, volname, result));
  }
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
  if (stat(unname, &stbuff) ||
    (!modus && (stbuff.st_mode & S_IFMT) != S_IFDIR) ) {
    completition = -0x9c;
  } else completition=0xff; /* all rights */
  return(completition);
}

int nw_set_fdate_time(uint32 fhandle, uint8 *datum, uint8 *zeit)
{
  if (fhandle > 0 && (--fhandle < anz_fhandles) ) {
    FILE_HANDLE  *fh=&(file_handles[fhandle]);
    if (fh->fd > -1) {
      fh->tmodi = nw_2_un_time(datum, zeit);
      return(0);
    }
  }
  return(-0x88); /* wrong filehandle */
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
    int    searchsequence = MAX_U16;
    uint16 dirsequenz     = GET_BE16(subnr);
    uint16 aktsequenz     = 0;
    uint8  dirname[256];
    if (!dirsequenz) dirsequenz++;

    strcpy(dirname, wild);
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
          strncpy(subname, dirname, 16);
          U32_TO_BE32(1L,  owner); /* erstmal */
          un_date_2_nw(stbuff.st_mtime, subdatetime);
          un_time_2_nw(stbuff.st_mtime, subdatetime+2);
          return(0xff);
        }
        strcpy(dirname, wild);
      } /* while */
    } else {
      strcpy(dh->kpath, ".");
      if (!stat(dh->unixname, &stbuff)) {
        U16_TO_BE16(1, subnr);
        memset(subname, 0, 16);
        U32_TO_BE32(1L,  owner);
        un_date_2_nw(stbuff.st_mtime, subdatetime);
        un_time_2_nw(stbuff.st_mtime, subdatetime+2);
        return(0xff);
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
  int  k     = len;
  char *p    = data+len;
  uint8 dirname[256];
  while (k--) {
    uint8 c = *--p;
    if (c == '/' || c == '\\' || c == ':') {
      p++;
      k++;
      break;
    }
  }
  if (len && k < len) {
    strmaxcpy(dirname, p, len-k);
    len = k;
  } else dirname[0] = '\0';
  return(s_nw_scan_dir_info(dir_handle, data, len, subnr,
                     subname, subdatetime, owner, dirname));
}

int nw_get_fs_usage(char *volname, struct fs_usage *fsu)
/* returns 0 if OK, else errocode < 0 */
{
  int volnr = nw_get_volume_number(volname, strlen(volname));
  return((volnr>-1 && !get_fs_usage(vols[volnr].unixname, fsu)) ? 0 : -1);
}

int nw_get_vol_info(int volnr)
/* returns >= 0 if OK, else errocode < 0 */
{
  int result = -0x98; /* Volume not exist */;
  if (volnr > -1 && volnr < used_vols) {
    result =0;
  }
  XDPRINTF((5,0,"NW_GET_VOL_INFO von VOLNR:%d, result=0x%x", volnr, result));
  return(result);
}

typedef struct {
  uint8   time[2];
  uint8   date[2];
  uint8   id[4];
} NW_FILE_DATES_INFO;

typedef struct {
  uint8   subdir[4];
  uint8   attributes[4]; /* 0x20,0,0,0   File  */
  uint8   uniqueid;      /* 0    */
  uint8   flags;         /* 0x18 */
  uint8   namespace;     /* 0    */
  uint8   namlen;
  uint8   name[12];
  NW_FILE_DATES_INFO created;
  NW_FILE_DATES_INFO archived;
  NW_FILE_DATES_INFO updated;
  uint8              size[4];
  uint8              reserved_1[44];
  uint8              inherited_rights_mask[2];
  uint8              last_access_date[2];
  uint8              reserved_2[28];
} NW_DOS_FILE_INFO;

static void xun_date_2_nw(time_t time, uint8 *d)
{
  uint16 i = un_date_2_nw(time, NULL);
  memcpy(d, &i, 2);
}

static void xun_time_2_nw(time_t time, uint8 *d)
{
  uint16 i = un_time_2_nw(time, NULL);
  memcpy(d, &i, 2);
}

static void get_dos_file_attrib(NW_DOS_FILE_INFO *f,
                               struct stat *stb,
                               NW_PATH *nwpath)
{
  f->namlen=min(strlen(nwpath->fn), 12);
  strncpy(f->name, nwpath->fn, f->namlen);
  /* Attribute */
  /* 0x20   Archive Flag */
  /* 0x80   Sharable     */
  f->attributes[0] = 0x20;
  xun_date_2_nw(stb->st_mtime, f->created.date);
  xun_time_2_nw(stb->st_mtime, f->created.time);
  U32_TO_BE32(1,               f->created.id);
  memcpy(&(f->updated), &(f->created), sizeof(NW_DOS_FILE_INFO));
  xun_date_2_nw(stb->st_atime, f->last_access_date);
  memcpy(f->size, &(stb->st_size), 4);
}

typedef struct {
  uint8   subdir[4];
  uint8   attributes[4]; /* 0x10,0,0,0   DIR   */
  uint8   uniqueid;      /* 0 */
  uint8   flags;         /* 0x14 or 0x1c */
  uint8   namespace;     /* 0 */
  uint8   namlen;
  uint8   name[12];
  NW_FILE_DATES_INFO created;
  NW_FILE_DATES_INFO archived;
  uint8   modify_time[2];
  uint8   modify_date[2];
  uint8   next_trustee[4];
  uint8   reserved_1[48];
  uint8   max_space[4];
  uint8   inherited_rights_mask[2];
  uint8   reserved_2[26];
} NW_DOS_DIR_INFO;


static void get_dos_dir_attrib(NW_DOS_DIR_INFO *f,
                                struct stat *stb,
                                NW_PATH *nwpath)
{
  f->namlen=min(strlen(nwpath->fn), 12);
  strncpy(f->name, nwpath->fn, f->namlen);
  f->attributes[0] = 0x10;    /* Dir  */
  xun_date_2_nw(stb->st_mtime, f->created.date);
  xun_time_2_nw(stb->st_mtime, f->created.time);
  U32_TO_BE32(1,              f->created.id);
  xun_date_2_nw(stb->st_mtime, f->modify_date);
  xun_time_2_nw(stb->st_mtime, f->modify_time);
  U32_TO_BE32(MAX_U32, f->max_space);
}

typedef struct {
  uint8   searchsequence[4];
  union {
    NW_DOS_DIR_INFO  d;
    NW_DOS_FILE_INFO f;
  } u;
} NW_SCAN_DIR_INFO;

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

       if ( (stbuff.st_mode & S_IFMT) == S_IFDIR) {
          get_dos_dir_attrib(&(scif->u.d), &stbuff,
                              &nwpath);
       } else {
          get_dos_file_attrib(&(scif->u.f), &stbuff,
               &nwpath);
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
  XDPRINTF((5,0,"nw_scan_a_directory_2 path:%s:, fn:%s:, completition:0x%x",
    nwpath.path, nwpath.fn, completition));
  if (completition > -1) {
    struct stat stbuff;
    if (!stat(build_unix_name(&nwpath, 2), &stbuff)) {
      NW_DOS_DIR_INFO  *d=(NW_DOS_DIR_INFO*)rdata;
      memset(rdata, 0, sizeof(NW_DOS_DIR_INFO));
      get_dos_dir_attrib(d, &stbuff, &nwpath);
      return(sizeof(NW_DOS_DIR_INFO));
    } else return(-0xff); /* not found */
  } else return(completition); /* wrong path */
}






/* <======================================================================> */
/* minimal queue handling to enable very simple printing */
/* qick and dirty  !!!!!!!!!!!!!!! */

#define MAX_JOBS    5   /*  max. open queue jobs for one connection */
static int anz_jobs=0;

typedef struct {
  uint32  fhandle;
  int     old_job; /* is old structure */
  union  {
    QUEUE_JOB      n;
    QUEUE_JOB_OLD  o;
  } q;
} INT_QUEUE_JOB;

INT_QUEUE_JOB *queue_jobs[MAX_JOBS];

static INT_QUEUE_JOB *give_new_queue_job(int old_job)
{
  int k=-1;
  while (++k < anz_jobs) {
    INT_QUEUE_JOB *p=queue_jobs[k];
    if (!p->fhandle) { /* free slot */
      memset(p, 0, sizeof(INT_QUEUE_JOB));
      p->old_job = old_job;
      if (old_job)
        p->q.o.job_id[0] = k+1;
      else
        p->q.n.job_id[0] = k+1;
      return(p);
    }
  }
  if (anz_jobs < MAX_JOBS) {
    INT_QUEUE_JOB **pp=&(queue_jobs[anz_jobs++]);
    *pp = (INT_QUEUE_JOB *) xmalloc(sizeof(INT_QUEUE_JOB));
    memset(*pp, 0, sizeof(INT_QUEUE_JOB));
    (*pp)->old_job = old_job;
    if (old_job)
      (*pp)->q.o.job_id[0] = anz_jobs;
    else
      (*pp)->q.n.job_id[0] = anz_jobs;
    return(*pp);
  }
  return(NULL);
}

static void free_queue_job(int q_id)
{
  if (q_id > 0 && q_id <= anz_jobs) {
    INT_QUEUE_JOB **pp=&(queue_jobs[q_id-1]);
    uint32 fhandle = (*pp)->fhandle;
    if (fhandle > 0) nw_close_datei(fhandle);
    if (q_id == anz_jobs) {
      xfree(*pp);
      --anz_jobs;
    } else (*pp)->fhandle=0L;
  }
}

static void set_entry_time(uint8 *entry_time)
{
  struct tm  *s_tm;
  time_t     timer;
  time(&timer);
  s_tm = localtime(&timer);
  entry_time[0]    = (uint8) s_tm->tm_year;
  entry_time[1]    = (uint8) s_tm->tm_mon+1;
  entry_time[2]    = (uint8) s_tm->tm_mday;
  entry_time[3]    = (uint8) s_tm->tm_hour;
  entry_time[4]    = (uint8) s_tm->tm_min;
  entry_time[5]    = (uint8) s_tm->tm_sec;
}

static int create_queue_file(char   *job_file_name,
                             uint32 q_id,
                             int    jo_id,
                             int    connection,
                             uint8  *dirname,
                             int    dir_nam_len,
                             uint8  *job_bez)

{
  int result;
  NW_FILE_INFO fnfo;
  *job_file_name
     = sprintf(job_file_name+1, "%07lX%d.%03d", q_id, jo_id, connection);

  result=nw_alloc_dir_handle(0, dirname, dir_nam_len, 99, 2, 1);
  if (result > -1)
    result = nw_creat_open_file(result, job_file_name+1,
                                       (int)  *job_file_name,
                                        &fnfo, 0x6, 0x6, 1);

  XDPRINTF((5,0,"creat queue file bez=`%s` handle=%d",
                                         job_bez, result));
  return(result);
}


int nw_creat_queue(int connection, uint8 *queue_id, uint8 *queue_job,
                           uint8 *dirname, int dir_nam_len, int old_call)
{
  INT_QUEUE_JOB *jo   = give_new_queue_job(old_call);
  uint32         q_id = GET_BE32(queue_id);
  int result = -0xff;
  XDPRINTF((5,0,"NW_CREAT_Q:dlen=%d, dirname=%s", dir_nam_len, dirname));

  if (NULL  != jo) {
    int jo_id = 0;
    if (jo->old_job) {
      jo_id = (int) jo->q.o.job_id[0];
      memcpy(&(jo->q.o), queue_job, sizeof(QUEUE_JOB_OLD));
      jo->q.o.job_id[0]         = (uint8) jo_id;
      jo->q.o.client_connection = (uint8)connection;
      jo->q.o.client_task       = (uint8)0xfe; /* ?? */
      U32_TO_BE32(1, jo->q.o.client_id); /* SU */
      set_entry_time(jo->q.o.job_entry_time);
      jo->q.o.job_typ[0]            = 0x0; /* 0xd0;*/
      jo->q.o.job_typ[1]            = 0x0;
      jo->q.o.job_position          = 0x1;
      jo->q.o.job_control_flags    |= 0x20;

      result = create_queue_file(jo->q.o.job_file_name,
                                 q_id, jo_id, connection,
                                 dirname, dir_nam_len,
                                 jo->q.o.job_bez);

      if (result > -1) {
        jo->fhandle = (uint32) result;
        U32_TO_BE32(jo->fhandle, jo->q.o.job_file_handle);
        U16_TO_BE16(0,           jo->q.o.job_file_handle+4);
        result = 0;
      }
      jo->q.o.server_station = 0;
      jo->q.o.server_task    = 0;
      U32_TO_BE32(0, jo->q.o.server_id);
      if (!result) memcpy(queue_job, &(jo->q.o), sizeof(QUEUE_JOB_OLD));
    } else {
      jo_id = (int) jo->q.n.job_id[0];
      memcpy(&(jo->q.n), queue_job, sizeof(QUEUE_JOB));
      jo->q.n.job_id[0]         = (uint8) jo_id;

      U16_TO_BE16(0xffff, jo->q.n.record_in_use);
      U32_TO_BE32(0x0,    jo->q.n.record_previous);
      U32_TO_BE32(0x0,    jo->q.n.record_next);
      memset(jo->q.n.client_connection, 0, 4);
      jo->q.n.client_connection[0] = (uint8)connection;
      memset(jo->q.n.client_task,  0, 4);
      jo->q.n.client_task[0]       = (uint8)0xfe; /* ?? */
      U32_TO_BE32(1, jo->q.n.client_id); /* SU */
      set_entry_time(jo->q.n.job_entry_time);

      jo->q.n.job_typ[0]            = 0x0; /* 0xd0;*/
      jo->q.n.job_typ[1]            = 0x0;
      jo->q.n.job_position[0]       = 0x1;
      jo->q.n.job_position[1]       = 0x0;
      jo->q.n.job_control_flags[0] |= 0x20;
      jo->q.n.job_control_flags[1]  = 0x0;

      result = create_queue_file(jo->q.n.job_file_name,
                                 q_id, jo_id, connection,
                                 dirname, dir_nam_len,
                                 jo->q.n.job_bez);

      if (result > -1) {
        jo->fhandle = (uint32) result;
        U16_TO_BE16((uint16)result, jo->q.n.job_file_handle);
        U16_TO_BE16(0,              jo->q.n.job_file_handle+2);
        result = 0;
      }
      U32_TO_BE32(0, jo->q.n.server_station);
      U32_TO_BE32(0, jo->q.n.server_task);
      U32_TO_BE32(0, jo->q.n.server_id);
      if (!result) memcpy(queue_job, &(jo->q.n), sizeof(QUEUE_JOB));
    }
    if (result) free_queue_job(jo_id);
  }
  return(result);
}

int nw_close_file_queue(uint8 *queue_id,
                        uint8 *job_id,
                        uint8 *prc, int prc_len)
{
  int result = -0xff;
  int jo_id  = (int) *job_id;  /* ever only the first byte */
  XDPRINTF((5,0,"nw_close_file_queue JOB=%d", jo_id));
  if (jo_id > 0 && jo_id <= anz_jobs){
    INT_QUEUE_JOB *jo=queue_jobs[jo_id-1];
    int fhandle = (int)jo->fhandle;
    XDPRINTF((5,0,"nw_close_file_queue fhandle=%d", fhandle));
    if (fhandle > 0 && fhandle <= anz_fhandles) {
      FILE_HANDLE  *fh=&(file_handles[fhandle-1]);
      char unixname[300];
      char printcommand[256];
      FILE *f=NULL;
      strmaxcpy(unixname,     fh->name, sizeof(unixname)-1);
      strmaxcpy(printcommand, prc, prc_len);
      nw_close_datei(fhandle);
      jo->fhandle = 0L;
      if (NULL != (f = fopen(unixname, "r"))) {
        int  is_ok = 0;
        FILE *fout = popen(printcommand, "w");
        if (fout) {
          char buff[1024];
          int  k;
          is_ok++;
          while ((k = fread(buff, 1, sizeof(buff), f)) > 0) {
            if (1 != fwrite(buff, k, 1, fout)) is_ok=0;
          }
          pclose(fout);
        } else
          XDPRINTF((1,0,"Cannot open pipe `%s`", "lpr"));

        fclose(f);
        if (is_ok) {
          unlink(unixname);
          result=0;
        }
      } else XDPRINTF((1,0,"Cannot open queue-file `%s`", unixname));
    } else
      XDPRINTF((2,0,"nw_close_file_queue fhandle=%d", fhandle));
    free_queue_job(jo_id);
  }
  return(result);
}


