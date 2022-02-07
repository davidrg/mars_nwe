/* nwfile.c  23-Jan-96 */
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

#include <dirent.h>
#include <utime.h>

#include <sys/errno.h>

#include "nwvolume.h"
#include "nwfile.h"
#include "connect.h"


#define MAX_FILEHANDLES   80
static FILE_HANDLE  file_handles[MAX_FILEHANDLES];
static int anz_fhandles=0;


static int new_file_handle(uint8 *unixname)
{
  int rethandle = -1;
  FILE_HANDLE  *fh=NULL;
  while (++rethandle < anz_fhandles) {
    FILE_HANDLE  *fh=&(file_handles[rethandle]);
    if (fh->fd == -1 && !(fh->flags & 4)) { /* empty slot */
      rethandle++;
      break;
    } else fh=NULL;
  }
  if (fh == NULL) {
    if (anz_fhandles < MAX_FILEHANDLES) {
      fh=&(file_handles[anz_fhandles]);
      rethandle = ++anz_fhandles;
    } else return(0); /* no free handle anymore */
  }
  /* init handle  */
  fh->fd      = -2;
  fh->offd    = 0L;
  fh->tmodi   = 0L;
  strcpy((char*)fh->fname, (char*)unixname);
  fh->flags   = 0;
  fh->f       = NULL;
  XDPRINTF((5, 0, "new_file_handle=%d, anz_fhandles=%d, fn=%s",
       rethandle, anz_fhandles, unixname));
  return(rethandle);
}

static int free_file_handle(int fhandle)
{
  int result=-0x88;
  if (fhandle > 0 && (fhandle <= anz_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle-1]);
    if (fh->fd > -1) {
      if (fh->flags & 2) {
        if (fh->f) pclose(fh->f);
        fh->f = NULL;
      } else close(fh->fd);
      if (fh->tmodi > 0L && !(fh->flags & 2)) {
      /* now set date and time */
        struct utimbuf ut;
        ut.actime = ut.modtime = fh->tmodi;
        utime(fh->fname, &ut);
        fh->tmodi = 0L;
      }
    }
    fh->fd = -1;
    if (fhandle == anz_fhandles && !(fh->flags & 4)) {
      /* was last */
      anz_fhandles--;
      while (anz_fhandles && file_handles[anz_fhandles-1].fd == -1
        && !(file_handles[anz_fhandles-1].flags & 4) )
          anz_fhandles--;
    }
    result=0;
  }
  XDPRINTF((5, 0, "free_file_handle=%d, anz_fhandles=%d, result=%d",
          fhandle, anz_fhandles, result));
  return(result); /* wrong filehandle */
}

void init_file_module(void)
{
  int k = -1;
  while (k++ < anz_fhandles) free_file_handle(k);
  anz_fhandles = 0;
}

int file_creat_open(int volume, uint8 *unixname, struct stat *stbuff,
                     int attrib, int access, int creatmode)
/*
 * creatmode: 0 = open | 1 = creat | 2 = creatnew  & 4 == save handle
 * attrib ??
 * access: 0x1=read, 0x2=write
 */
{
   int fhandle=new_file_handle(unixname);
   if (fhandle > 0){
     FILE_HANDLE *fh=&(file_handles[fhandle-1]);
     int completition = -0xff;  /* no File  Found */
     if (get_volume_options(volume, 1) & VOL_OPTION_IS_PIPE) {
       /* this is a PIPE Dir */
       int statr = stat(fh->fname, stbuff);
       if (!statr && (stbuff->st_mode & S_IFMT) != S_IFDIR) {
         char pipecommand[300];
         char *pipeopen = (creatmode || (access & 2)) ? "w" : "r";
         char *topipe   = "READ";
         if (creatmode) topipe = "CREAT";
         else if (access & 2) topipe = "WRITE";
         sprintf(pipecommand, "%s %s", fh->fname, topipe);
         fh->f  = popen(pipecommand, pipeopen);
         fh->fd = (fh->f) ? fileno(fh->f) : -1;
         if (fh->fd > -1) {
           fh->flags |= 2;
           if (creatmode & 4) fh->flags |= 4;
           return(fhandle);
         }
       }
     } else {
       if (creatmode) {  /* creat File  */
         if (creatmode & 0x2) { /* creatnew */
           if (!stat(fh->fname, stbuff)) {
             XDPRINTF((5,0,"CREAT File exist!! :%s:", fh->fname));
             fh->fd       = -1;
             completition = -0x85; /* No Priv */
           } else {
             XDPRINTF((5,0,"CREAT FILE:%s: Handle=%d", fh->fname, fhandle));
             fh->fd       = creat(fh->fname, 0777);
             if (fh->fd < 0) completition = -0x84; /* no create Rights */
           }
         } else {
           XDPRINTF((5,0,"CREAT FILE, ever with attrib:0x%x, access:0x%x, fh->fname:%s: handle:%d",
             attrib,  access, fh->fname, fhandle));
           fh->fd = open(fh->fname, O_CREAT|O_TRUNC|O_RDWR, 0777);
           if (fh->fd < 0) completition = -0x85; /* no delete /create Rights */
         }
         if (fh->fd > -1) {
           close(fh->fd);
           fh->fd   = open(fh->fname, O_RDWR);
           fh->offd = 0L;
           stat(fh->fname, stbuff);
         }
       } else {
         int statr = stat(fh->fname, stbuff);
         int acm  = (access & 2) ? (int) O_RDWR /*|O_CREAT*/ : (int)O_RDONLY;
         if ( (!statr && (stbuff->st_mode & S_IFMT) != S_IFDIR)
              || (statr && (acm & O_CREAT))){
            XDPRINTF((5,0,"OPEN FILE with attrib:0x%x, access:0x%x, fh->fname:%s: fhandle=%d",attrib,access, fh->fname, fhandle));
            fh->fd = open(fh->fname, acm, 0777);
            fh->offd = 0L;
            if (fh->fd > -1) {
              if (statr) stat(fh->fname, stbuff);
            } else completition = -0x9a;
         }
       }
       if (fh->fd > -1) {
         if (creatmode & 4) fh->flags |= 4;
         return(fhandle);
       }
     } /* else (NOT DEVICE) */
     XDPRINTF((5,0,"OPEN FILE not OK ! fh->name:%s: fhandle=%d",fh->fname, fhandle));
     free_file_handle(fhandle);
     return(completition);
   } else return(-0x81); /* no more File Handles */
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

int nw_close_datei(int fhandle, int reset_reuse)
{
  XDPRINTF((5, 0, "nw_close_datei handle=%d", fhandle));
  if (fhandle > 0 && (fhandle <= anz_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle-1]);
    if (reset_reuse) fh->flags &= (~4);
    if (fh->fd > -1) {
      int result = 0;
      int result2;
      if (fh->flags & 2) {
        if (fh->f) {
          result=pclose(fh->f);
          if (result) result = -1;
        }
        fh->f = NULL;
      } else result=close(fh->fd);
      fh->fd = -1;
      if (fh->tmodi > 0L && !(fh->flags&2)) {
        struct utimbuf ut;
        ut.actime = ut.modtime = fh->tmodi;
        utime(fh->fname, &ut);
        fh->tmodi = 0L;
      }
#ifdef TEST_FNAME
      if (fhandle == test_handle) {
        test_handle = -1;
        nw_debug    = -99;
      }
#endif
      result2=free_file_handle(fhandle);
      return((result == -1) ? -0xff : result2);
    } else return(free_file_handle(fhandle));
  }
  return(-0x88); /* wrong filehandle */
}

uint8 *file_get_unix_name(int fhandle)
{
  if (fhandle > 0 && (--fhandle < anz_fhandles)) {
    return((uint8*)file_handles[fhandle].fname);
  }
  return(NULL);
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
        XDPRINTF((5,0,"File %s is stripped, result=%d", fh->fname, result));
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
      XDPRINTF((2, 0,  "nw_%s_datei result=%d, fh=%d, offset=%d, size=%d",
        (do_lock) ? "lock" : "unlock", result, fhandle, offset, size));

      if (!result) return(0);
      else return(-0x21); /* LOCK Violation */
    }
  }
  return(-0x88); /* wrong filehandle */
}




