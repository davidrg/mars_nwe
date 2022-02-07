/* nwfile.c  01-Feb-98 */
/* (C)opyright (C) 1993,1998  Martin Stover, Marburg, Germany
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
#include "nwshare.h"
#include "nwfile.h"
#include "connect.h"
#include "nwattrib.h"
#include "nwconn.h"
#include "unxfile.h"

# include <sys/mman.h>

static got_sig_bus=0;
void sig_bus_mmap(int rsig)
{
  got_sig_bus++;
  XDPRINTF((2,0, "Got sig_bus"));
  signal(SIGBUS, sig_bus_mmap);
}

static FILE_HANDLE  file_handles[MAX_FILE_HANDLES_CONN];
#define    HOFFS        0
#define    USE_NEW_FD   1
static int count_fhandles=0;

#if USE_NEW_FD
static int last_fhandle=HOFFS;
#endif

static int new_file_handle(int volume, uint8 *unixname, int task)
{
#if USE_NEW_FD
  int fhandle= -1 + last_fhandle++;
#else
  int fhandle=HOFFS-1;
#endif
  FILE_HANDLE  *fh=NULL;
  while (++fhandle < count_fhandles) {
    fh=&(file_handles[fhandle]);
    if (fh->fd == -1 && !(fh->fh_flags & FH_DO_NOT_REUSE)) { /* empty slot */
      fhandle++;
      break;
    } else fh=NULL;
  }

  if (fh == NULL) {
    if (count_fhandles < MAX_FILE_HANDLES_CONN) {
      fh=&(file_handles[count_fhandles]);
      fhandle = ++count_fhandles;
    } else {
#if USE_NEW_FD
      last_fhandle=HOFFS+1;
      fhandle=HOFFS-1;
      while (++fhandle < count_fhandles) {
        fh=&(file_handles[fhandle]);
        if (fh->fd == -1 && !(fh->fh_flags & FH_DO_NOT_REUSE)) { /* empty slot */
          fhandle++;
          break;
        } else fh=NULL;
      }
#endif
      if (fh == NULL) {
        XDPRINTF((1, 0, "No more free file handles"));
        return(0); /* no free handle anymore */
      }
    }
  }
  /* init handle  */
  fh->task    = task;
  fh->fd      = -2;
  fh->offd    = 0L;
  fh->tmodi   = 0L;
  fh->modified = 0;
  fh->st_ino  = 0;
  strcpy((char*)fh->fname, (char*)unixname);
  fh->fh_flags   = 0;
  fh->f       = NULL;
  fh->volume  = volume;
  XDPRINTF((5, 0, "new_file_handle=%d, count_fhandles=%d, fn=%s",
       fhandle, count_fhandles, unixname));
  return(fhandle);
}

static int free_file_handle(int fhandle)
{
  int result=-0x88;
  if (fhandle > HOFFS && (fhandle <= count_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle-1]);
    if (fh->fd > -1) {
      if (fh->fh_flags & FH_IS_PIPE_COMMAND) {
        if (fh->f) ext_pclose(fh->f);
        fh->f = NULL;
      } else {
        if (use_mmap && fh->p_mmap) {
          munmap(fh->p_mmap, fh->size_mmap);
          fh->p_mmap = NULL;
          fh->size_mmap = 0;
        }
        close(fh->fd);
        if (fh->st_ino) {
          share_file(fh->st_dev, fh->st_ino, 0);
          if (fh->modified) {
            fh->modified=0;
#if NEW_ATTRIB_HANDLING
            set_nw_archive_bit(fh->volume, fh->fname, fh->st_dev, fh->st_ino);
#endif
          }
        }
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
#if !USE_NEW_FD
    while (count_fhandles > fhandle
          && file_handles[count_fhandles-1].fd == -1
          && !(file_handles[count_fhandles-1].fh_flags & FH_DO_NOT_REUSE) ) {
      count_fhandles--;
    }
#endif
    result=0;
  }
  XDPRINTF((5, 0, "free_file_handle=%d, count_fhandles=%d, result=%d",
          fhandle, count_fhandles, result));
  return(result); /* wrong filehandle */
}

void init_file_module(int task)
/*
 * if task == -1 all handles will be free'd
 * else only handles of the task will be free'd
 */
{
  int k = HOFFS;
  if (task < 0) {
    while (k++ < count_fhandles)
      free_file_handle(k);
    count_fhandles = HOFFS;
#if USE_NEW_FD
    last_fhandle   = HOFFS;
#endif
  } else {
    /* I hope next is ok, added 20-Oct-96 ( 0.98.pl5 ) */
    while (k++ < count_fhandles) {
      FILE_HANDLE  *fh=&(file_handles[k-1]);
      if (fh->task == task && fh->fd>-1) {
        free_file_handle(k);
      }
    }
  }
}

static int xsetegid(gid_t gid)
{
  int result = -1;
  if (!seteuid(0)) {
    if (setegid(gid)) {
      XDPRINTF((2, 0, "Cannot change eff Group ID to %d", gid));
    } else
      result=0;
    if (seteuid(act_uid)) {
      reset_guid();
      result = -1;
    }
  }
  return(result);
}

int file_creat_open(int volume, uint8 *unixname, struct stat *stbuff,
                     int attrib, int access, int creatmode, int task)
/*
 * creatmode: 0 = open
 *          | 1 = creat (ever)
 *          | 2 = creatnew ( creat if not exist )
 *          ---------
 *          & 4 == save handle    (not reuse)
 *          & 8 == ignore rights  (try to open as root)
 * attrib ??
 *
 * access: 0x1=read,
 *         0x2=write,
 *         0x4=deny read, -> F_WRLCK, no other process can make
 *                  a read or write lock
 *                  can only be used if file is open for writing
 *         0x8=deny write -> F_RDLCK, no other process can make a writelock
 *                  can only be used if file is open for reading
 *         0x10=SH_COMPAT
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
   int fhandle  = new_file_handle(volume, unixname, task);
   int dowrite  = ((access & 2) || (creatmode & 3) ) ? 1 : 0;
   if (fhandle > HOFFS){
     FILE_HANDLE *fh=&(file_handles[fhandle-1]);
     int completition = 0;  /* first ok */
     int voloptions   = get_volume_options(volume);
     int acc          = (!stat(fh->fname, stbuff))
                           ? get_real_access(stbuff) : -1;

     int did_grpchange = 0;

     if (dowrite && (acc > -1) && (acc & W_OK) && !(creatmode&0x8) &&
        (get_nw_attrib_dword(volume, fh->fname, stbuff) & FILE_ATTR_R))
       completition = -0x94;
     else if (dowrite && (voloptions & VOL_OPTION_READONLY)) {
       completition = (creatmode&3) ? -0x84 : -0x94;
     } else if (acc > -1) {
       /* do exist */
       if (!S_ISDIR(stbuff->st_mode)) {
         if (!(voloptions & VOL_OPTION_IS_PIPE)
           || S_ISFIFO(stbuff->st_mode) ) {
           /* We look for normal file accesses */
           if (creatmode & 2) {
             XDPRINTF((5,0,"CREAT File exist!! :%s:", fh->fname));
             completition = -0x85; /* No Priv */
           } else if (dowrite && !(acc & W_OK) && !(creatmode & 0x8) ) {
             if (!S_ISFIFO(stbuff->st_mode)) {
               if (entry8_flags&2 && (acc & R_OK)) {
                 /* we use strange compatibility modus */
                 dowrite=0;
                 XDPRINTF((1, 0, "Uses strange open comp. mode for file `%s`",
                     fh->fname));
               } else
                 completition = (creatmode&3) ? -0x84 : -0x94;
             } else
              completition = (creatmode&3) ? -0x84 : -0x94;
           } else if (!(acc & R_OK) && !(creatmode & 0x8) )
             completition = -0x93;

           if ((!completition) && !S_ISFIFO(stbuff->st_mode) && dowrite){
             /* is this file already opened write deny by other process */
             if (-1 == share_file(stbuff->st_dev, stbuff->st_ino, 0x12|0x8))
               completition=-0x80;
           }

         } else if (acc & X_OK) {
           /* special Handling for PIPE commands */
           if (!(acc & W_OK)) {
             if (acc & 0x10)      /* access owner */
               stbuff->st_mode  |= S_IWUSR;
             else if (acc & 0x20) /* access group */
               stbuff->st_mode  |= S_IWGRP;
             else
               stbuff->st_mode  |= S_IWOTH;
           }
         } else {
           XDPRINTF((4, 0, "No PIPE command rights st_mode=0x%x uid=%d, gid=%d",
                     stbuff->st_uid, stbuff->st_gid));
           completition = -0xff;
         }
       } else
         completition= -0xff;
     } else if ( (voloptions & VOL_OPTION_IS_PIPE) || !(creatmode&3) ) {
       /* must exist, but don't */
       completition=-0xff;
     } else {
       /* File do not exist yet, but must be created */
       char *p=strrchr(unixname, '/');
       /* first we say: not OK */
       completition = -0xff;
       if (p) {
         *p='\0';
         acc = (!stat(unixname, stbuff))
                  ? get_real_access(stbuff) : -1;
         if (acc > 0) {
           if (acc & W_OK) /* we need write access for this directory */
             completition=0;
           else
             completition=-0x84; /* no creat rights */
         }
         *p='/';
       }
       if (completition && (creatmode & 8)) {
         acc=0;
         completition=0;
       }
     }

     if ( (!completition) && (acc & 0x20) && (stbuff->st_gid != act_gid)) {
       /* here we try a change egid */
       if (xsetegid(stbuff->st_gid)) {
         completition = -0x85; /* no privillegs */
       } else {
         did_grpchange++;
       }
     }

     if (!completition) {
       if (voloptions & VOL_OPTION_IS_PIPE) {
         /* <========= this is a PIPE Volume ====================> */
         fh->fh_flags |= FH_IS_PIPE;
         if (S_ISFIFO(stbuff->st_mode)){
           fh->fd = open(fh->fname,
               O_NONBLOCK | dowrite ? O_RDWR : O_RDONLY);
         } else {
           fh->fh_flags |= FH_IS_PIPE_COMMAND;
           fh->fd=-3;
         }
         if (fh->fd != -1) {
           if (!dowrite)
             stbuff->st_size = 0x7fff0000 | (rand() & 0xffff);
           (void)time(&(stbuff->st_mtime));
           stbuff->st_atime = stbuff->st_mtime;
           if (creatmode & 4)
             fh->fh_flags |= FH_DO_NOT_REUSE;
           if (did_grpchange)
             xsetegid(act_gid);
           goto file_creat_open_ret;
         }
       } else {
         /* <========= this is NOT a PIPE Volume ====================> */
         if (creatmode&0x3) {  /* creat File  */
           int was_ok=0;
           fh->fd=-1;

           if (creatmode & 0x2) { /* creatnew */
             XDPRINTF((5,0,"CREAT FILE:%s: Handle=%d", fh->fname, fhandle));
             if (!nw_creat_node(volume, fh->fname, 0))
               was_ok++;
             else
               completition = -0x84; /* no create Rights */
           } else {
             XDPRINTF((5,0,"CREAT FILE, ever with attrib:0x%x, access:0x%x, fh->fname:%s: handle:%d",
               attrib,  access, fh->fname, fhandle));
             if (!nw_creat_node(volume, fh->fname,
                    (creatmode & 0x8) ? (2|8) : 2))
               was_ok++;
             else
              completition = -0x85; /* no delete /create Rights */
           }
           if (was_ok) {
             fh->fd   = open(fh->fname, O_RDWR);
             fh->offd = 0L;
             stat(fh->fname, stbuff);
           }
         } else {
           /* ======== 'normal' open of file ================ */
           int acm  = (dowrite) ? (int) O_RDWR : (int)O_RDONLY;
           if (S_ISFIFO(stbuff->st_mode)){
             acm |= O_NONBLOCK;
             fh->fh_flags |= FH_IS_PIPE;
             if (!dowrite) stbuff->st_size = 0x7fffffff;
             (void)time(&(stbuff->st_mtime));
             stbuff->st_atime = stbuff->st_mtime;
           }
           fh->fd = open(fh->fname, acm);
           if (fh->fd < 0 && (creatmode&8)) {
             if (did_grpchange) {
               xsetegid(act_gid);
               did_grpchange=0;
             }
             seteuid(0);
             fh->fd = open(fh->fname, acm);
             reset_guid();
           }
           XDPRINTF((5,0, "OPEN FILE:fd=%d, attrib:0x%x, access:0x%x, fh->fname:%s:fhandle=%d",
                 fh->fd, attrib, access, fh->fname, fhandle));
           if (fh->fd < 0)
             completition = dowrite ? -0x94 : -0x93;
         }

         if (fh->fd > -1) {
           if (did_grpchange) {
             xsetegid(act_gid);
             did_grpchange=0;
           }
           if (!(fh->fh_flags & FH_IS_PIPE)) {
             /* Not a PIPE */
             int result=0;
             fh->st_dev=stbuff->st_dev;
             fh->st_ino=stbuff->st_ino;
             if ( (!result) && ((access & 0x4) || (access & 0x8)) ) {
               if (access & 0x4) /* deny read */
                 result=share_file(stbuff->st_dev, stbuff->st_ino, 0x5);
               if ((access & 0x8) && !result)
                 result=share_file(stbuff->st_dev, stbuff->st_ino, 0x2);
               XDPRINTF(((result==-1)?2:5, 0,  "open shared lock:result=%d,fn='%s'",
                         result, fh->fname));
             } else {
               result=share_file(stbuff->st_dev, stbuff->st_ino, 1);
               if (result==-1) {
                 XDPRINTF((2, 0, "open share failed,fn='%s'", fh->fname));
               }
             }
             if (result==-1) {
               close(fh->fd);
               fh->fd       = -1;
               completition = -0x80;
               /* 0.99.pl0 changed -0xfe -> -0x80 */
             }

             if (use_mmap && fh->fd > -1 && !dowrite) {
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
           }
         }
         if (fh->fd > -1) {
           if (did_grpchange)
             xsetegid(act_gid);
           if (!dowrite)
              fh->fh_flags |= FH_OPENED_RO;
           if (voloptions & VOL_OPTION_READONLY)
              fh->fh_flags |= FH_IS_READONLY;
           if (creatmode & 4)
             fh->fh_flags |= FH_DO_NOT_REUSE;
           goto file_creat_open_ret;
         }
       } /* else (note pipecommand) */
     } /* if !completition */
     if (did_grpchange)
        xsetegid(act_gid);
     XDPRINTF((5,0,"OPEN FILE not OK (-0x%x), fh->name:%s: fhandle=%d",
         -completition, fh->fname, fhandle));
     free_file_handle(fhandle);
     fhandle=completition;
   } else fhandle=-0x81; /* no more File Handles */

file_creat_open_ret:
   MDEBUG(D_FH_OPEN, {
     char fname[200];
     if (!fd_2_fname(fhandle, fname, sizeof(fname))){
       FILE_HANDLE *fh=fd_2_fh(fhandle);
       xdprintf(1,0,"Open/creat fd=%d, fn=`%s`, openmode=%s",
          fhandle, fname, (fh && (fh->fh_flags &FH_OPENED_RO)) ? "RO" : "RW" );
     }
   })
   return(fhandle);
}

int nw_set_fdate_time(uint32 fhandle, uint8 *datum, uint8 *zeit)
{
  if (fhandle > HOFFS && (--fhandle < count_fhandles) ) {
    FILE_HANDLE  *fh=&(file_handles[fhandle]);
    if (fh->fd == -3 || (fh->fh_flags & FH_IS_PIPE)) return(0);
    if (!(fh->fh_flags & FH_IS_READONLY)) {
      fh->tmodi = nw_2_un_time(datum, zeit);
      if (fh->fd == -1) { /* already closed */
#if 0
      /* don't know whether we should do it in this way */
        struct utimbuf ut;
        if (!*(fh->fname))
          return(-0x88); /* wrong filehandle */
        ut.actime = ut.modtime = fh->tmodi;
        utime(fh->fname, &ut);
#else
        return(-0x88); /* wrong filehandle */
#endif
      }
      return(0);
    } else return(-0x8c); /* no modify privileges */
  }
  return(-0x88); /* wrong filehandle */
}

int nw_close_file(int fhandle, int reset_reuse)
{
  XDPRINTF((5, 0, "nw_close_file handle=%d, count_fhandles",
     fhandle, count_fhandles));

  MDEBUG(D_FH_OPEN, {
    char fname[200];
    int r=fd_2_fname(fhandle, fname, sizeof(fname));
    xdprintf(1,0,"nw_close_file: fd=%d, fn=`%s`,r=%d", fhandle, fname, r);
  })

  if (fhandle > HOFFS && (fhandle <= count_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle-1]);
    if (reset_reuse) fh->fh_flags &= (~FH_DO_NOT_REUSE);
    if (fh->fd > -1 || (fh->fd == -3 && fh->fh_flags & FH_IS_PIPE_COMMAND)) {
      int result = 0;
      int result2;
      if (fh->fh_flags & FH_IS_PIPE_COMMAND) {
        if (fh->f) {
          result=ext_pclose(fh->f);
          if (result > 0) result = 0;
        }
        fh->f = NULL;
      } else {
        if (use_mmap && fh->p_mmap) {
          munmap(fh->p_mmap, fh->size_mmap);
          fh->p_mmap = NULL;
          fh->size_mmap = 0;
        }
        result=close(fh->fd);
        if (fh->st_ino) {
          share_file(fh->st_dev, fh->st_ino, 0);
          if (fh->modified) {
            fh->modified=0;
#if NEW_ATTRIB_HANDLING
            set_nw_archive_bit(fh->volume, fh->fname, fh->st_dev, fh->st_ino);
#endif
          }
        }
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


int nw_commit_file(int fhandle)
{
  MDEBUG(D_FH_FLUSH, {
    char fname[200];
    int r=fd_2_fname(fhandle, fname, sizeof(fname));
    xdprintf(1,0,"nw_commit_file: fd=%d, fn=`%s`,r=%d", fhandle, fname, r);
  })
  if (fhandle > HOFFS && (fhandle <= count_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle-1]);
    if (fh->fd > -1 || (fh->fd == -3 && fh->fh_flags & FH_IS_PIPE_COMMAND)) {
      if (!(fh->fh_flags & FH_IS_READONLY)) {
        int result=0;
        if (!(fh->fh_flags & FH_IS_PIPE) && fh->fd > -1) {
#if 0
          /* 0.99.pl0
           * this is not allowed because lockings will be removed
           * hint from: Przemyslaw Czerpak
           */
          int fd=dup(fh->fd);
          if (fd > -1) {
            if (!close(fh->fd)) {
              if (fh->tmodi > 0L) {
                struct utimbuf ut;
                ut.actime = ut.modtime = fh->tmodi;
                utime(fh->fname, &ut);
                fh->tmodi = 0L;
              }
              fh->fd=dup2(fd, fh->fd);
            } else
              result=-0x8c;
            close(fd);
          }
#else
          if (fh->tmodi > 0L) {
            struct utimbuf ut;
            ut.actime = ut.modtime = fh->tmodi;
            utime(fh->fname, &ut);
          }
#endif
        }
        return(result);
      } else
        return(-0x8c);
    }
  }
  return(-0x88); /* wrong filehandle */
}


uint8 *file_get_unix_name(int fhandle)
{
  if (fhandle > HOFFS && (--fhandle < count_fhandles)) {
    return((uint8*)file_handles[fhandle].fname);
  }
  return(NULL);
}

static void open_pipe_command(FILE_HANDLE *fh, int dowrite)
{
  if (NULL == fh->f) {
    char pipecommand[512];
    sprintf(pipecommand, "%s %s %d %d",
                        fh->fname,
                        dowrite ? "WRITE" : "READ",
                        act_connection, act_pid);
    fh->f  = ext_popen(pipecommand, geteuid(), getegid());
  }
  fh->fd = (fh->f) ? fh->f->fds[dowrite ? 0 : 1] : -3;
}

int nw_read_file(int fhandle, uint8 *data, int size, uint32 offset)
{
  if (fhandle > HOFFS && (--fhandle < count_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle]);
    if (fh->fh_flags & FH_IS_PIPE_COMMAND)
        open_pipe_command(fh, 0);
    if (fh->fd > -1) {
      if (fh->fh_flags & FH_IS_PIPE) { /* PIPE */
        int readsize=size;
#if 1
        if (-1 == (size = read(fh->fd, data, readsize)) )  {
          int k=2;
          do {
            sleep(1);
          } while(k-- &&  -1 == (size = read(fh->fd, data, readsize)));
          if (size == -1) size=0;
        }
#else
        int offset=0;
        int k=2;
        while ((size = read(fh->fd, data+offset, readsize)) < readsize) {
          if (size>0) {
            readsize-=size;
            offset+=size;
          } else if (!k-- || ((!size)&&readsize)) break;
          else sleep(1);
        }
        size=offset;
#endif
#if 1
        if (!size) {
          if (fh->f->flags & 1) return(-0x57);
          fh->f->flags |= 1;
        }
#endif
      } else if (use_mmap && fh->p_mmap) {
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
        if (fh->offd != (long)offset) {
          fh->offd=lseek(fh->fd, offset, SEEK_SET);
          if (fh->offd < 0) {
            XDPRINTF((5,0,"read-file failed in lseek"));
          }
        }
        if (fh->offd > -1L) {
          if ((size = read(fh->fd, data, size)) > -1) {
            fh->offd+=(long)size;
          } else {
            XDPRINTF((5,0,"read-file failed in read"));
          }
        } else size = -1;
      }
      if (size == -1) size=0;
      return(size);
    } else if (fh->fd == -3) return(0);
  }
  return(-0x88); /* wrong filehandle */
}

int nw_seek_file(int fhandle, int modus)
{
  if (fhandle > HOFFS && (--fhandle < count_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle]);
    if (fh->fd > -1) {
      if (fh->fh_flags & FH_IS_PIPE) { /* PIPE */
        return(0x7fff0000 | (rand() & 0xffff) );
      } else {
        int size=-0xfb;
        if (!modus) {
          if ( (size=fh->offd=lseek(fh->fd, 0L, SEEK_END)) < 0L)
              size = -1;
        }
        return(size);
      }
    } else if (fh->fd == -3) return(0x7fff0000 | (rand() & 0xffff) );
          /* PIPE COMMAND */
  }
  return(-0x88); /* wrong filehandle */
}


int nw_write_file(int fhandle, uint8 *data, int size, uint32 offset)
{
  if (fhandle > HOFFS && (--fhandle < count_fhandles)) {
    FILE_HANDLE *fh=&(file_handles[fhandle]);
    if (fh->fh_flags & FH_IS_PIPE_COMMAND)
        open_pipe_command(fh, 1);
    if (fh->fd > -1) {
      if (fh->fh_flags & FH_OPENED_RO) return(-0x94);
      if (fh->fh_flags & FH_IS_PIPE) { /* PIPE */
        return(size ? write(fh->fd, data, size) : 0);
      } else {
        if (fh->offd != (long)offset)
            fh->offd = lseek(fh->fd, offset, SEEK_SET);
        if (size) {
          if (fh->offd > -1L) {
            size = write(fh->fd, data, size);
            fh->offd+=(long)size;
            if (!fh->modified)
              fh->modified++;
          } else size = -1;
          return(size);
        } else {  /* truncate FILE */
          int result = unx_ftruncate(fh->fd, offset);
          XDPRINTF((5,0,"File %s is truncated, result=%d", fh->fname, result));
          fh->offd = -1L;
          if (!fh->modified)
            fh->modified++;
          return(result);
        }
      }
    } else if (fh->fd == -3) return(0);
  }
  return(- 0x88); /* wrong filehandle */
}

int nw_server_copy(int qfhandle, uint32 qoffset,
                   int zfhandle, uint32 zoffset,
                   uint32 size)
{
  if (qfhandle > HOFFS && (--qfhandle < count_fhandles)
    && zfhandle > HOFFS && (--zfhandle < count_fhandles) ) {
    FILE_HANDLE *fhq=&(file_handles[qfhandle]);
    FILE_HANDLE *fhz=&(file_handles[zfhandle]);
    int retsize = -1;
    if (fhq->fd > -1 && fhz->fd > -1) {
      char buff[4096];
      int  wsize;
      if (fhz->fh_flags & FH_OPENED_RO) return(-0x94);
      if (lseek(fhq->fd, qoffset, SEEK_SET) > -1L &&
          lseek(fhz->fd, zoffset, SEEK_SET) > -1L) {
        retsize = 0;
        if (size && !fhz->modified)
            fhz->modified++;
        while (size) {
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
      return(retsize);
    }
  }
  return(-0x88); /* wrong filehandle */
}

int nw_lock_file(int fhandle, uint32 offset, uint32 size, int do_lock)
{
  int result=-0x88;  /* wrong filehandle */
  if (fhandle > HOFFS && (fhandle <= count_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle-1]);
    if (fh->fd > -1) {
      struct flock flockd;
      if (fh->fh_flags & FH_IS_PIPE) {
        result=0;
        goto leave;
      }
      flockd.l_type   = (do_lock)
                         ? ((fh->fh_flags & FH_OPENED_RO) ?  F_RDLCK
                                                          :  F_WRLCK)
                         : F_UNLCK;
      flockd.l_whence = SEEK_SET;
#if 0
      flockd.l_start  = offset;
#else
      /* Hint from:Morio Taneda <morio@sozio.geist-soz.uni-karlsruhe.de>
       * dBase needs it
       * 03-Dec-96
       */
      flockd.l_start  = (offset & 0x7fffffff);
#endif

      if (size == MAX_U32) {
       /*  This is only a guess, but a size of 0xffffffff means to lock
        *  the rest of the file, starting from the offset, to do this with
        *  linux, a size of 0 has to be passed to the fcntl function.
        *  ( Peter Gerhard )
        *
        */
        flockd.l_len  = 0;
      } else
        flockd.l_len  = (size & 0x7fffffff);

      result = fcntl(fh->fd, F_SETLK, &flockd);
      XDPRINTF((2, 0,  "nw_%s_datei result=%d, fh=%d, offset=%d, size=%d",
        (do_lock) ? "lock" : "unlock", result, fhandle, offset, size));
      if (result)
         result= (do_lock) ? -0xfe : -0xff;
       /* 0.99.pl0: changed -0xfd -> -0xfe, hint from Przemyslaw Czerpak */
    } else if (fh->fd == -3) result=0;
  }
leave:
  MDEBUG(D_FH_LOCK, {
    char fname[200];
    (void)fd_2_fname(fhandle, fname, sizeof(fname));
    xdprintf(1,0,"nw_%s_datei: fd=%d, fn=`%s`,r=0x%x, offs=%d, len=%d",
              (do_lock) ? "lock" : "unlock",
              fhandle, fname, -result, offset, size);
  })

  return(result);
}

int fd_2_fname(int fhandle, char *buf, int bufsize)
{
  if (fhandle > HOFFS && (--fhandle < count_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle]);
    strmaxcpy(buf, fh->fname, bufsize-1);
    return(0);
  }
  if (bufsize)
    *buf='\0';
  return(-0x88);
}

FILE_HANDLE *fd_2_fh(int fhandle)
{
  if (fhandle > HOFFS && (--fhandle < count_fhandles))
    return(&(file_handles[fhandle]));
  return(NULL);
}

int get_nwfd(int fhandle)
{
  if (fhandle > HOFFS && (--fhandle < count_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle]);
    return(fh ? fh->fd : -1);
  }
  return(-1);
}

int nw_unlink(int volume, char *name)
{
  struct stat stbuff;
  int voloptions=get_volume_options(volume);
  if (voloptions & VOL_OPTION_IS_PIPE)
    return(0); /* don't delete 'pipe commands' */
  else if (get_volume_options(volume) & VOL_OPTION_READONLY)
    return(-0x8a); /* don't delete 'readonly' */
  if (stat(name, &stbuff))
    return(-0x9c); /* wrong path */
  if (get_nw_attrib_dword(volume, name, &stbuff) & FILE_ATTR_R)
    return(-0x8a); /* don't delete 'readonly' */

  if (  -1 == share_file(stbuff.st_dev, stbuff.st_ino, 0x12|0x8)
     || ( !(entry8_flags&0x10) &&
       -1 == share_file(stbuff.st_dev, stbuff.st_ino, 0x11|0x4)) )
    return(-0x8a); /* NO Delete Privileges, file is shared open */
  if (!unlink(name)) {
    free_nw_ext_inode(volume, name, stbuff.st_dev, stbuff.st_ino);
    return(0);
  }
  return(-0x8a); /* NO Delete Privileges */
}



