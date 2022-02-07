/* nwfname.c 17-Jun-97 */
/* (C)opyright (C) 1997  Martin Stover, Marburg, Germany
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

/*
 * some code and ideas from Victor Khimenko <khim@mccme.ru>
 */

#include "net.h"
#include "unxfile.h"
#include "nwvolume.h"
#include <utime.h>
#include "nwfname.h"
#include <limits.h>

typedef uint8 CHARTABLE[256];

static uint8       *last_filename=NULL;
static ino_t       last_st_ino=0;

static CHARTABLE *conversiontable=NULL;
/* there exists 5 tables
 * 0 = dos2unix
 * 1 = unix2dos
 * 2 = down2up 'dosname'
 * 3 = up2down 'dosname'
 * 4 = up2down 'unixname'
 */

void init_nwfname(char *convfile)
{
  FILE *f;
  new_str(last_filename, NULL);
  last_st_ino=0;
  if (conversiontable) return;
  if (NULL != (f=fopen(convfile, "rb")))
    conversiontable=(CHARTABLE*)xcmalloc(sizeof(CHARTABLE)*5);
  if (conversiontable) {
    int i=fread(conversiontable, sizeof(CHARTABLE), 5, f);
    if (i < 4)
      xfree(conversiontable);
    else if (i==4) {
      /* now we get the last table from the other */
      int i=0;
      while(i++ < 255) {
        uint8 dc=conversiontable[1][i];
        uint8 lo=conversiontable[3][dc];
        conversiontable[4][i]=
            conversiontable[0][lo];
      }
    }
  }
  XDPRINTF((2,0, "conversiontable: `%s` %s loaded",
       convfile, (conversiontable==NULL) ? "not" : ""));
  if (f) fclose(f);
}

uint8 *up_fn(uint8 *ss)
{
  uint8 *s=ss;
  if (!s) return((uint8*)NULL);
  if (conversiontable)
    for (;*s;s++) *s=conversiontable[2][*s];
  else
    for (;*s;s++) *s=up_char(*s);
  return(ss);
}

uint8 *down_fn(uint8 *ss)
{
  uint8 *s=ss;
  if (!s) return((uint8*)NULL);
  if (conversiontable)
    for (;*s;s++) *s=conversiontable[3][*s];
  else
    for (;*s;s++) *s=down_char(*s);
  return(ss);
}

uint8 *dos2unixcharset(uint8 *ss)
{
  uint8 *s=ss;
  if (!conversiontable) return ss;
  if (!s) return((uint8*)NULL);
  for (;*s;s++) *s=conversiontable[0][*s];
  return(ss);
}

uint8 *unix2doscharset(uint8 *ss)
{
  uint8 *s=ss;
  if (!conversiontable) return ss;
  if (!s) return((uint8*)NULL);
  for (;*s;s++) *s=conversiontable[1][*s];
  return(ss);
}

int dfn_imatch(uint8 a, uint8 b)
/* returns 1 if a matched b ignoring case for 'dos/os2' filename chars */
{
  if (a==b) return(1);
  if (!conversiontable)
    return(down_char(a) == down_char(b));
  return(conversiontable[3][a] == conversiontable[3][b]);
}

int ufn_imatch(uint8 a, uint8 b)
/* returns 1 if a matched b ignoring case for unix filename chars */
{
  if (a==b) return(1);
  if (!conversiontable)
    return(down_char(a) == down_char(b));
  return(conversiontable[4][a] == conversiontable[4][b]);
}

#if PERSISTENT_SYMLINKS
static dev_t       last_st_dev=0;
static int         last_islink=0;

typedef struct {
  dev_t  st_dev;
  ino_t  st_ino;
  char   *filename;
} S_PATH_INODE;


static int get_full_path(char *path)
/* sets last_filename  */
{
   char newpath[PATH_MAX];
   char *npath=newpath;
   char *maxpath=newpath+PATH_MAX-1;
   char aktpath[PATH_MAX];
   char *pp=path;
   struct stat statb_buf;
   struct stat *statbuf=&statb_buf;
   int  countlinks = 10;  /* max. 10 links */
   *npath++='/';   /* we begin at '/' */
   last_islink = 0;
   last_st_ino = 0;

   while (*pp) {
     char *save_npath;
     if (pp[0] == '.'){
       if (pp[1] == '.' && (pp[2] == '/' || pp[2] == '\0') ) {
         pp+=2;
         while(npath > newpath && *(npath-1) != '/')
           --npath;
         continue;
       } else if (pp[1] == '/') {
         pp++;
         continue;
       }
     } else if (pp[0] == '/') {
       pp++;
       continue;
     }
     save_npath=npath;
     while (*pp && *pp != '/')  {
       if (npath == maxpath) {  /* path too long */
         last_st_ino = 0;
         return(-99);
       }
       *npath++ = *pp++;
     }
     *npath='\0';
     if (lstat(newpath, statbuf)){
       *save_npath='\0';
       new_str(last_filename, newpath);
       return(-1);
     }
     if (S_ISLNK(statbuf->st_mode)) {
       int len=readlink(newpath, aktpath, PATH_MAX-1);
       if (len < 0 || !countlinks-- ) { /* new links */
         last_st_ino = 0;
         return(-98);
       }
       aktpath[len]='\0';
       pp=aktpath;
       if (aktpath[0] == '/') {
         npath=newpath+1;
         ++pp;
       } else {
         npath=save_npath;
       }
       last_islink++;
     } else {
       last_st_dev = statbuf->st_dev;
       last_st_ino = statbuf->st_ino;
       last_islink = 0;
     }
   }
   new_str(last_filename, newpath);
   return(0);
}

int s_stat(char *path, struct stat *statbuf, S_STATB *stb)
{

  int result=0;
  if (lstat(path, statbuf)) {
    if (get_full_path(path) || lstat(last_filename, statbuf)) {
      result=-1;
    } else if (stb) {
      stb->st_dev=last_st_dev;
      stb->st_ino=last_st_ino;
      stb->islink=last_islink;
    }
  } else {
    if (stb) {
      stb->st_dev=statbuf->st_dev;
      stb->st_ino=statbuf->st_ino;
      stb->islink=S_ISLNK(statbuf->st_mode);
    }
    if (S_ISLNK(statbuf->st_mode)) {
      if (statbuf->st_ino == last_st_ino && statbuf->st_dev == last_st_dev) {
        if (lstat(last_filename, statbuf)) {
          last_st_ino=0;
          result=-1;
          goto s_stat_ret;
        }
        if (!S_ISLNK(statbuf->st_mode))
          goto s_stat_ret;
      }

      if (get_full_path(path) || lstat(last_filename, statbuf)) {
        last_st_ino=0;
        result=-1;
      }

    }
  }

s_stat_ret:
  MDEBUG(D_FN_NAMES, {
   xdprintf(1,0,"s_stat_ret: result=%d, path=`%s`, last_fname=`%s`",
         result, path, last_filename);
  })
  return(result);
}

static int get_linked_name(char *path, S_STATB *stb,
                           char **fname, int mode)
{
  S_STATB stbbuf;
  struct stat statbuf_buf;
  struct stat *statbuf=&statbuf_buf;
  int result=0;
  if (!stb) {
    stb=&stbbuf;
    stb->st_ino=0;
  }
  if (!stb->st_ino || (stb->islink
                   && (stb->st_ino != last_st_ino
                   || stb->st_dev != last_st_dev))) {

    if (lstat(path, statbuf)) {
      if (get_full_path(path) || lstat(last_filename, statbuf)) {
        result=-1;
        goto get_linked_name_ret;
      } else if (stb) {
        stb->st_dev=last_st_dev;
        stb->st_ino=last_st_ino;
        stb->islink=last_islink;
      }
      *fname = last_filename;
      goto get_linked_name_ret;
    }
    stb->st_dev=statbuf->st_dev;
    stb->st_ino=statbuf->st_ino;
    stb->islink=S_ISLNK(statbuf->st_mode);
  }
  if (stb->islink) {
    if (stb->st_ino == last_st_ino && stb->st_dev == last_st_dev) {
      if (lstat((char *)last_filename, statbuf)) {
        last_st_ino=0;
        result=-1;
        goto get_linked_name_ret;
      }
      if (!S_ISLNK(statbuf->st_mode)) {
        *fname = last_filename;
        goto get_linked_name_ret;
      }
    }
    if (get_full_path(path) || lstat(last_filename, statbuf)) {
      last_st_ino=0;
      result=-1;
      goto get_linked_name_ret;
    }
    *fname=last_filename;
  } else
   *fname=path;

get_linked_name_ret:
  MDEBUG(D_FN_NAMES, {
   xdprintf(1,0,"get_l_name: result=%d, path=`%s`, fname=`%s`",
         result, path, *fname);
  })
  return(result);
}

int s_utime(char *fn, struct utimbuf *ut, S_STATB *stb)
{
  char *fnbuf=fn;
  int result=get_linked_name(fn, stb, &fnbuf, 0);
  if (!result) {
    result=utime(fnbuf, ut);
  }
  return(result);
}

int s_chmod(char *fn, umode_t mode, S_STATB *stb)
{
  char *fnbuf=fn;
  int result=get_linked_name(fn, stb, &fnbuf, 0);
  if (!result) {
    result=chmod(fnbuf, mode);
  }
  return(result);
}

#endif
