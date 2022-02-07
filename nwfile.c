/* nwfile.c  16-Jul-96 */
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
#include "nwconn.h"
#if USE_MMAP
# include <sys/mman.h>
static got_sig_bus=0;
void sig_bus_mmap(int rsig)
{
  got_sig_bus++;
  XDPRINTF((2,0, "Got sig_bus"));
  signal(SIGBUS, sig_bus_mmap);
}
#endif

static FILE_HANDLE  file_handles[MAX_FILE_HANDLES_CONN];
static int anz_fhandles=0;

static int new_file_handle(uint8 *unixname)
{
  int rethandle = -1;
  FILE_HANDLE  *fh=NULL;
  while (++rethandle < anz_fhandles) {
    FILE_HANDLE  *fh=&(file_handles[rethandle]);
    if (fh->fd == -1 && !(fh->fh_flags & FH_DO_NOT_REUSE)) { /* empty slot */
      rethandle++;
      break;
    } else fh=NULL;
  }
  if (fh == NULL) {
    if (anz_fhandles < MAX_FILE_HANDLES_CONN) {
      fh=&(file_handles[anz_fhandles]);
      rethandle = ++anz_fhandles;
    } else return(0); /* no free handle anymore */
  }
  /* init handle  */
  fh->fd      = -2;
  fh->offd    = 0L;
  fh->tmodi   = 0L;
  strcpy((char*)fh->fname, (char*)unixname);
  fh->fh_flags   = 0;
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
      if (fh->fh_flags & FH_IS_PIPE_COMMAND) {
        if (fh->f) ext_pclose(fh->f);
        fh->f = NULL;
      } else {
#if USE_MMAP
        if (fh->p_mmap) {
          munmap(fh->p_mmap, fh->size_mmap);
          fh->p_mmap = NULL;
          fh->size_mmap = 0;
        }
#endif
        close(fh->fd);
      }
      if (fh->tmodi > 0L && !(FH_IS_PIPE_COMMAND & fh->fh_flags)
                         && !(FH_IS_READONLY     & fh->fh_flags) ) {
      /* now set date and time */
        struct utimbuf ut;
        ut.actime = ut.modtime = fh->tmodi;
        utime(fh->fname, &ut);
        fh->tmodi = 0L;
      }
    }
    fh->fd = -1;
    if (fhandle == anz_fhandles && !(fh->fh_flags & FH_DO_NOT_REUSE)) {
      /* was last */
      anz_fhandles--;
      while (anz_fhandles && file_handles[anz_fhandles-1].fd == -1
        && !(file_handles[anz_fhandles-1].fh_flags & FH_DO_NOT_REUSE) )
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
 * creatmode: 0 = open | 1 = creat (ever) | 2 = creatnew ( creat if not exist )
 *            & 4 == save handle    (creat)
 *            & 8 == ignore rights  (create ever)
 * attrib ??
 *
 * access: 0x1=read,
 *         0x2=write,
 *         0x4=deny read,   -> F_WRLCK
 *         0x8=deny write   -> F_RDLCK
 *        0x10=SH_COMPAT
 *
 * 0x09    (O_RDONLY | O_DENYWRITE);
 * 0x05    (O_RDONLY | O_DENYREAD);
 *
 * 0x0b    (O_RDWR   | O_DENYWRITE);
 * 0x07    (O_RDWR   | O_DENYREAD);
 *
 * 0x05    (O_RDONLY | O_DENYREAD | O_DENYWRITE);
 * 0x07    (O_RDWR   | O_DENYREAD | O_DENYWRITE);
 *
 */
{
   int fhandle=new_file_handle(unixname);
   int dowrite      = (access & 2) || creatmode ;
   if (fhandle > 0){
     FILE_HANDLE *fh=&(file_handles[fhandle-1]);
     int completition = -0xff;  /* no File  Found */
     int voloptions   = get_volume_options(volume, 1);
     if (dowrite && (voloptions & VOL_OPTION_READONLY)) {
       completition = (creatmode) ? -0x84 : -0x94;
     } else if (voloptions & VOL_OPTION_IS_PIPE) {
       /* this is a PIPE Volume */
       int statr = stat(fh->fname, stbuff);
       if (!statr && (stbuff->st_mode & S_IFMT) != S_IFDIR) {
         char pipecommand[300];
         char *topipe             = "READ";
         if (creatmode) topipe    = "CREAT";
         else if (dowrite) topipe = "WRITE";
         sprintf(pipecommand, "%s %s %d %d",
                               fh->fname, topipe,
                               act_connection, act_pid);
         fh->f  = ext_popen(pipecommand, geteuid(), getegid());
         fh->fd = (fh->f) ? fileno(fh->f->fildes[1]) : -1;
         if (fh->fd > -1) {
           fh->fh_flags |= FH_IS_PIPE;
           fh->fh_flags |= FH_IS_PIPE_COMMAND;
           if (!dowrite) stbuff->st_size = 0x7fffffff;
           if (creatmode & 4) fh->fh_flags |= FH_DO_NOT_REUSE;
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
           if (fh->fd < 0) {
             if (creatmode & 0x8) {
               if ( (!seteuid(0)) && (-1 < (fh->fd =
                    open(fh->fname, O_CREAT|O_TRUNC|O_RDWR, 0777)))) {
                 chown(fh->fname, act_uid, act_gid);
               }
               set_guid(act_gid, act_uid);
             }
             if (fh->fd < 0)
               completition = -0x85; /* no delete /create Rights */
           }
         }
         if (fh->fd > -1) {
           close(fh->fd);
           fh->fd   = open(fh->fname, O_RDWR);
           fh->offd = 0L;
           stat(fh->fname, stbuff);
         }
       } else {
         int statr = stat(fh->fname, stbuff);
         int acm  = (access & 2) ? (int) O_RDWR : (int)O_RDONLY;
         if ( (!statr && !S_ISDIR(stbuff->st_mode))
              || (statr && (acm & O_CREAT))){
            if ((!statr) && S_ISFIFO(stbuff->st_mode)){
              acm |= O_NONBLOCK;
              fh->fh_flags |= FH_IS_PIPE;
            }
            fh->fd = open(fh->fname, acm, 0777);
            XDPRINTF((5,0,"OPEN FILE with attrib:0x%x, access:0x%x, fh->fname:%s: fhandle=%d",attrib,access, fh->fname, fhandle));
            fh->offd = 0L;
            if (fh->fd > -1) {
              if (statr) stat(fh->fname, stbuff);
            } else completition = dowrite ? -0x94 : -0x93;
         }
       }
       if (fh->fd > -1) {
         if (!(fh->fh_flags & FH_IS_PIPE)) {
           /* Not a PIPE */
           if ((access & 0x4) || (access & 0x8)) {
             struct flock flockd;
             int result;
             flockd.l_type   = (access & 0x8) ? F_RDLCK : F_WRLCK;
             flockd.l_whence = SEEK_SET;
             flockd.l_start  = 0;
             flockd.l_len    = 0;
             result = fcntl(fh->fd, F_SETLK, &flockd);
             XDPRINTF((5, 0,  "open shared lock:result=%d", result));
             if (result == -1) {
               close(fh->fd);
               fh->fd = -1;
               completition=-0xfe;
             }
           }
#if USE_MMAP
           if (fh->fd > -1 && !dowrite) {
             fh->size_mmap = fh->offd=lseek(fh->fd, 0L, SEEK_END);
             if (fh->size_mmap > 0) {
               fh->p_mmap = mmap(NULL,
                                 fh->size_mmap,
                                 PROT_READ,
                                 MAP_SHARED,
                                 fh->fd, 0);
               if (fh->p_mmap == (uint8*) -1) {
                 fh->p_mmap = NULL;
                 fh->size_mmap=0;
               }
             }
           }
#endif
         }
       }
       if (fh->fd > -1) {
         if (!dowrite)      fh->fh_flags |= FH_IS_READONLY;
         if (creatmode & 4) fh->fh_flags |= FH_DO_NOT_REUSE;
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
      if (!(fh->fh_flags & FH_IS_READONLY)) {
        fh->tmodi = nw_2_un_time(datum, zeit);
        return(0);
      } else return(-0x8c);
    }
  }
  return(-0x88); /* wrong filehandle */
}

int nw_close_datei(int fhandle, int reset_reuse)
{
  XDPRINTF((5, 0, "nw_close_datei handle=%d, anz_fhandles",
     fhandle, anz_fhandles));
  if (fhandle > 0 && (fhandle <= anz_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle-1]);
    if (reset_reuse) fh->fh_flags &= (~FH_DO_NOT_REUSE);
    if (fh->fd > -1) {
      int result = 0;
      int result2;
      if (fh->fh_flags & FH_IS_PIPE_COMMAND) {
        if (fh->f) {
          result=ext_pclose(fh->f);
          if (result > 0) result = 0;
        }
        fh->f = NULL;
      } else {
#if USE_MMAP
        if (fh->p_mmap) {
          munmap(fh->p_mmap, fh->size_mmap);
          fh->p_mmap = NULL;
          fh->size_mmap = 0;
        }
#endif
        result=close(fh->fd);
      }
      fh->fd = -1;
      if (fh->tmodi > 0L && !(fh->fh_flags & FH_IS_PIPE)
                         && !(fh->fh_flags & FH_IS_READONLY)) {
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
      if (fh->fh_flags & FH_IS_PIPE) { /* PIPE */
        if (fh->fh_flags & FH_IS_PIPE_COMMAND)
          size = fread(data, 1, size, fh->f->fildes[1]);
        else {
          size = read(fh->fd, data, size);
          if (size < 0) {
            int k=5;
            while (size < 0 && --k /* && errno == EAGAIN */)
               size = read(fh->fd, data, size);
          }
        }
      } else {
#if USE_MMAP
        if (fh->p_mmap) {
          while (1) {
            if (offset < fh->size_mmap) {
              if (size + offset > fh->size_mmap)
                   size =  fh->size_mmap - offset;
              memcpy(data, fh->p_mmap+offset, size);
              if (got_sig_bus) {
                fh->size_mmap = lseek(fh->fd, 0L, SEEK_END);
                got_sig_bus   = 0;
              } else
                break;
            } else {
              size=-1;
              break;
            }
          } /* while */
        } else {
#endif
          if (fh->offd != (long)offset) {
            fh->offd=lseek(fh->fd, offset, SEEK_SET);
            if (fh->offd < 0) {
              XDPRINTF((5,0,"read-file failed in lseek"));
            }
          }
          if (fh->offd > -1L) {
            if ((size = read(fh->fd, data, size)) > -1)
              fh->offd+=(long)size;
            else {
              XDPRINTF((5,0,"read-file failed in read"));
            }
          } else size = -1;
#if USE_MMAP
        }
#endif
      }
      if (size == -1) size=0;
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
      if (fh->fh_flags & FH_IS_PIPE) { /* PIPE */
        return(0x7fffffff);
      } else {
        int size=-0xfb;
        if (!modus) {
          if ( (size=fh->offd=lseek(fh->fd, 0L, SEEK_END)) < 0L)
              size = -1;
        }
        return(size);
      }
    }
  }
  return(-0x88); /* wrong filehandle */
}


int nw_write_datei(int fhandle, uint8 *data, int size, uint32 offset)
{
  if (fhandle > 0 && (--fhandle < anz_fhandles)) {
    FILE_HANDLE *fh=&(file_handles[fhandle]);
    if (fh->fd > -1) {
      if (fh->fh_flags & FH_IS_READONLY) return(-0x94);
      if (fh->fh_flags & FH_IS_PIPE) { /* PIPE */
        if (size) {
          if (fh->fh_flags & FH_IS_PIPE_COMMAND)
            return(fwrite(data, 1, size, fh->f->fildes[0]));
          return(write(fh->fd, data, size));
        } return(0);
      } else {
        if (fh->offd != (long)offset)
            fh->offd = lseek(fh->fd, offset, SEEK_SET);
        if (size) {
          if (fh->offd > -1L) {
            size = write(fh->fd, data, size);
            fh->offd+=(long)size;
          } else size = -1;
          return(size);
        } else {  /* truncate FILE */
          int result;
#ifdef LINUX
          result = ftruncate(fh->fd, offset);
#else
          struct flock flockd;
          flockd.l_type   = 0;
          flockd.l_whence = SEEK_SET;
          flockd.l_start  = offset;
          flockd.l_len    = 0;
          result = fcntl(fh->fd, F_FREESP, &flockd);
#endif
          XDPRINTF((5,0,"File %s is truncated, result=%d", fh->fname, result));
          fh->offd = -1L;
          return(result);
        }
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
      if (fhz->fh_flags & FH_IS_READONLY) return(-0x94);
      if (lseek(fhq->fd, qoffset, SEEK_SET) > -1L &&
          lseek(fhz->fd, zoffset, SEEK_SET) > -1L) {
        retsize = 0;
        while (size && !retsize) {
          int xsize = read(fhq->fd, buff, min(size, (uint32)sizeof(buff)));
          if (xsize > 0){
            if ((wsize =write(fhz->fd, buff, xsize)) != xsize) {
              retsize = -0x1;  /* out of Disk Space */
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
      if (fh->fh_flags & FH_IS_PIPE) return(0);
      flockd.l_type   = (do_lock)
                         ? ((fh->fh_flags & FH_IS_READONLY) ?  F_RDLCK
                                                            :  F_WRLCK)
                         : F_UNLCK;
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




