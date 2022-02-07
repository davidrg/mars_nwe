/* nwfile.c  23-May-99 */
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
#include <sys/time.h>

#include "nwvolume.h"
#include "nwshare.h"
#include "nwfile.h"
#include "connect.h"
#include "nwattrib.h"
#include "trustee.h"
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
  fh->access  = 0;
  fh->inuse   = 0;
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
        seteuid(0);
        utime(fh->fname, &ut);
        reseteuid();
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
        MDEBUG(D_FH_OPEN, {
          char fname[200];
          int r=fd_2_fname(k, fname, sizeof(fname));
          xdprintf(1,0,"init_file_m fd=%3d, task=%d, fn=`%s`,r=%d", 
                   k, task, fname, r);
        })
        free_file_handle(k);
      }
    }
  }
}

static int open_with_root_access(char *path, int mode)
/* open existing files */
{
  int fd=open(path, mode);
  if (fd < 0 && errno == EACCES) {
    seteuid(0);
    fd=open(path, mode);
    reseteuid();
  }
  return(fd);
}

#if 0 /* not used */
static int reopen_file(int volume, uint8 *unixname, struct stat *stbuff, 
                   int access, int task)
/* look for file already open and try to use it */
/* do not know whether this is real ok */
{
  int fhandle=-1;
  int result=0;
  while (++fhandle < count_fhandles) {
    FILE_HANDLE *fh=&(file_handles[fhandle]);
    if (fh->fd > -1 && fh->task == task && fh->volume == volume
       && fh->st_dev == stbuff->st_dev && fh->st_ino == stbuff->st_ino) {
      if ((fh->access&4) && (access&4)) 
         return(-0x80); /* share error */
      if ((fh->access&8) && (access&8)) 
         return(-0x80); /* share error */
      if (access & 4) 
        result=share_file(stbuff->st_dev, stbuff->st_ino, 0x5);
      if ((access & 0x8) && !result)
        result=share_file(stbuff->st_dev, stbuff->st_ino, 0x2);
      if (result)
         return(-0x80); /* share error */
      if ((fh->access&2) || (access&2))
        return(0);
      fh->inuse++;
      return(++fhandle);
    }
  }
  return(0);
}
#endif

int file_creat_open(int volume, uint8 *unixname, struct stat *stbuff,
                     int attrib, int access, int creatmode, int task)
/*
 * creatmode: 0 = open     ( do not creat )
 *          | 1 = creat    ( creat always )
 *          | 2 = creatnew ( creat if not exist else error )
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
  int dowrite       = ((access & 2) || (creatmode & 3) ) ? 1 : 0;
  int completition  = 0;  /* first ok */
  int fhandle       = -1;
  int voloptions    = get_volume_options(volume);
  int volnamlen     = get_volume_unixname(volume, NULL);
  int exist         = stat(unixname, stbuff) ? 0 : 1;
  int eff_rights    = 0;
  uint32 dwattrib;

  /* first we test accesses 
   * if something is wrong completition will become != 0
   */

  if (!exist) {
   /* we do it again as root to get always the correct information */
    seteuid(0);
    exist = stat(unixname, stbuff) ? 0 : 1;
    reseteuid();
  }

  if (exist) { /* file exists */
    if (S_ISDIR(stbuff->st_mode)) 
      completition = -0xff; /* directory is total wrong here */
    else {
#if 0  /* deaktivated in 0.99.pl16, 23-May-99 */
       /* because reopen_file do not handle share conditions correct */
      if (!(creatmode&1) && !(voloptions & VOL_OPTION_IS_PIPE)) {
        int fdx=reopen_file(volume, unixname, stbuff, access, task);
        if (fdx != 0) 
            return(fdx);
      }
#endif
      eff_rights = tru_get_eff_rights(volume, unixname, stbuff);
      dwattrib   = get_nw_attrib_dword(volume, unixname, stbuff);
      if (creatmode&0x2) { /* creat if not exist */
        if (S_ISFIFO(stbuff->st_mode)||(voloptions&VOL_OPTION_IS_PIPE)) { 
          /* fifo or pipe command always must exist */
          if ((dwattrib&FILE_ATTR_R)||!(eff_rights & TRUSTEE_W)){         
            completition = -0x94; /* No write rights */
          }
        } else {
          XDPRINTF((5,0,"CREAT File exist!! :%s:", unixname));
          completition = -0x85; /* No Priv */
        } 
      } else if (creatmode&0x1) { /* creat always*/ 
        if (!(creatmode&0x8)){
          if ((dwattrib&FILE_ATTR_R)
            || !(eff_rights & TRUSTEE_W))  
            completition = -0x86; /* creat file exists ro */
          else if (!(eff_rights & TRUSTEE_E))         
            completition = -0x85; /* creat file no delete rights */
          else if (!(eff_rights & TRUSTEE_C)){         
            completition = -0x84; /* No creat rights */
          }
        }
      } else { /* open */ 
        if (dowrite) {
          if (!(creatmode&0x8)) {
            if ((dwattrib&FILE_ATTR_R)||!(eff_rights & TRUSTEE_W)){         
              if ((entry8_flags&2) && (eff_rights & TRUSTEE_R) ) {
                /* we use strange compatibility modus if file is readable */
                dowrite=0;
                XDPRINTF((1, 0, "Uses strange open comp. mode for file `%s`",
                       unixname));
              } else
                completition = -0x94; /* No write rights */
            }
          }
        } else {  /* open to read */
          if (!(creatmode&0x8)) {
            if (!(eff_rights & TRUSTEE_R)){         
              completition = -0x93; /* No read rights */
            }
          }
        }
      }
      if ( (!completition) && dowrite && !S_ISFIFO(stbuff->st_mode) && 
          !(voloptions&VOL_OPTION_IS_PIPE)) {
        /* is this file already opened write deny by other process */
        if (-1 == share_file(stbuff->st_dev, stbuff->st_ino, 0x12|0x8))
               completition=-0x80; /* lock fail */
      }
    }  
  } else { /* do not exist, must be created */
    if (voloptions&VOL_OPTION_IS_PIPE)
      completition=-0xff;  /* pipecommands always must exist */
    else if (creatmode&0x3) {  /* do creat */
      uint8 *p=(uint8*)strrchr(unixname, '/');
      if (NULL != p && ((p - unixname)+1) >= volnamlen ) { /* parent dir */
        *p='\0';
        seteuid(0);
        completition=stat(unixname, stbuff);
        reseteuid();
        if (!completition) {
          eff_rights = tru_get_eff_rights(volume, unixname, stbuff);
          dwattrib   = get_nw_attrib_dword(volume, unixname, stbuff);
          if (creatmode&0x8) 
            eff_rights |= TRUSTEE_C|TRUSTEE_W|TRUSTEE_R;
          if (!(eff_rights & TRUSTEE_C)){ /* no creat rights */        
            completition=-0x84;
          }
        } else 
          completition=-0x9c;
        *p='/';
      } else 
        completition=-0x9c;
      if (completition==-0x9c) 
        errorp(0, "nwfile.c", "LINE=%d, unixname='%s', p-unx=%d, volnamlen=%d", 
                  __LINE__, unixname, (p) ? (int)(p - unixname): 0, volnamlen );
    } else
     completition=-0xff; /* should, but do not exist */
  }
  
  /* 
   * Here all access tests are made and we can do the open as 
   * root (open_with_root_access).
   */
  if (!completition) {   
    fhandle = new_file_handle(volume, unixname, task);
    if (fhandle > HOFFS) {
      FILE_HANDLE *fh=&(file_handles[fhandle-1]);
      if (exist) {
        if (S_ISFIFO(stbuff->st_mode)) { /* named pipes */
          fh->fh_flags |= FH_IS_PIPE;
          fh->fd = open_with_root_access(fh->fname, O_NONBLOCK  
                                                       | (dowrite 
                                                          ? O_WRONLY 
                                                          : O_RDONLY));
          if (fh->fd < 0) 
            completition=-0x9c;
        } else if (voloptions & VOL_OPTION_IS_PIPE) {  /* 'pipe' volume */
          fh->fh_flags |= FH_IS_PIPE;
          fh->fh_flags |= FH_IS_PIPE_COMMAND;
          fh->fd=-3;
#if 0
          stbuff->st_mtime = (time(NULL)-255)+(rand()&0xff);
          if (!dowrite)
              stbuff->st_size = 0x7fff0000 | (rand() & 0xffff);
#else   /* 03-Aug-98 better  ? */
          stbuff->st_mtime = time(NULL)+1000;
          stbuff->st_size  = 0x70000000|(stbuff->st_mtime&0xfffffff);
#endif
          stbuff->st_atime = stbuff->st_mtime;
        } else { /* 'normal' file */
          int acm = (dowrite) ? O_RDWR : O_RDONLY;
          if (dowrite && (creatmode&0x3))
            acm |= O_TRUNC;
          fh->fd  = open_with_root_access(fh->fname, acm);
          if (fh->fd != -1){
            if (acm&O_TRUNC) {
              seteuid(0);
              stat(fh->fname, stbuff);
              reseteuid();
            }
          } else
            completition=-0x9c;
        }
      } else { /* needs to be created */
        if (nw_creat_node(volume, fh->fname, 2|8))
          completition=-0x9c;
        else {
          fh->fd   = open_with_root_access(fh->fname, O_RDWR);
          fh->offd = 0L;
          if (fh->fd==-1)
            completition=-0x9c;
          else {
            seteuid(0);
            stat(fh->fname, stbuff);
            reseteuid();
          }
        }
      }
      if (!completition && !(fh->fh_flags & FH_IS_PIPE)) {
        /* We try sharing and mmaping if not a pipe */
        int result=0;
        fh->st_dev=stbuff->st_dev;
        fh->st_ino=stbuff->st_ino;
        if (((access & 0x4) || (access & 0x8)) ) {
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
      if (!completition) {
        fh->access = access;
        if (fh->fd != -1) {
          fh->inuse++;
          if (!dowrite)
            fh->fh_flags |= FH_OPENED_RO;
          if (voloptions & VOL_OPTION_READONLY)
            fh->fh_flags |= FH_IS_READONLY;
          if (creatmode & 4)
            fh->fh_flags |= FH_DO_NOT_REUSE;
        }
      } else {
        XDPRINTF((5,0,"OPEN FILE not OK (-0x%x), fh->name:%s: fhandle=%d",
                   -completition, fh->fname, fhandle));
        free_file_handle(fhandle);
        fhandle=completition;
      }
      if (completition==-0x9c) 
        errorp(0, "nwfile.c", "LINE=%d, unixname='%s'", __LINE__, unixname);
    } else fhandle=-0x81; /* no more File Handles */
  } else fhandle=completition;
  
  MDEBUG(D_FH_OPEN, {
    char fname[200];
    if (!fd_2_fname(fhandle, fname, sizeof(fname))){
      FILE_HANDLE *fh=fd_2_fh(fhandle);
      xdprintf(1,0,"Open/creat fd=%3d, task=%d, fn=`%s`, openmode=%s, access=0x%x, no reuse=%d",
          fhandle, task, fname, (fh && (fh->fh_flags &FH_OPENED_RO)) ? "RO" : "RW", 
          access, fh->fh_flags & FH_DO_NOT_REUSE ? 1 :0 );
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

int nw_close_file(int fhandle, int reset_reuse, int task)
{
  XDPRINTF((5, 0, "nw_close_file handle=%d, count_fhandles",
     fhandle, count_fhandles));

  MDEBUG(D_FH_OPEN, {
    char fname[200];
    int r=fd_2_fname(fhandle, fname, sizeof(fname));
    xdprintf(1,0,"nw_close_f fd=%3d, task=%d, fn=`%s`,r=%d, rreuse=%d", 
             fhandle, task, fname, r, reset_reuse);
  })

  if (fhandle > HOFFS && (fhandle <= count_fhandles)) {
    FILE_HANDLE  *fh=&(file_handles[fhandle-1]);
    if (reset_reuse) fh->fh_flags &= (~FH_DO_NOT_REUSE);
    else if (fh->task != task) {
    /* if close file's task is wrong the file will not be closed.   
     * I hope this is right !?
     */
       char fname[200];
       int r=fd_2_fname(fhandle, fname, sizeof(fname));
       xdprintf(2, 0,"%s close_file fd=%3d, task=%d differs fh->task=%d, fn=`%s`", 
             (task == 0 || fh->task == 0) ? "do" : "not",
             fhandle, task, fh->task, fname);
    
    /*  
     * return(0); 24-May-98 , 0.99.pl9
     */

    /*  21-Oct-98: I think file must be closed always,
     *  got problem with pserver which opens the file with task =0
     *  and closes it with task <> 0.
     */

    /*  23-May-99: 0.99.pl16 we only close file if task = 0 or 
     *  file open task = 0.
     */
     
     if ( task && fh->task )
        return(0); 
    }
    
    if (--fh->inuse > 0)  /* 03-Dec-98 */
       return(0);

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
        seteuid(0);
        utime(fh->fname, &ut);
        reseteuid();
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
                seteuid(0);
                utime(fh->fname, &ut);
                reseteuid();
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
            seteuid(0);
            utime(fh->fname, &ut);
            reseteuid();
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
    fh->f  = ext_popen(pipecommand, geteuid(), getegid(), 0);
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
        fd_set fdin;
        struct timeval t;
        int result;
        FD_ZERO(&fdin);
        FD_SET(fh->fd, &fdin);
        t.tv_sec  = 5;  /* should be enough */
        t.tv_usec = 0;
        size = select(fh->fd+1, &fdin, NULL, NULL, &t);
        if (size > 0)
          size = read(fh->fd, data, readsize);
        if (size == -1) size=0;
#elif 1
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
        if ((fh->fh_flags & FH_IS_PIPE_COMMAND) && !size) {
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
      XDPRINTF((4, 0,  "nw_%s_datei result=%d, fh=%d, offset=%d, size=%d",
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

void log_file_module(FILE *f)
{
  if (f) {
    int k=HOFFS-1;
    int handles=0;
    while (++k < count_fhandles) {
      FILE_HANDLE  *fh=&(file_handles[k]);
      if (fh && fh->fd != -1) {
        fprintf(f,"%4d %2d %d %4d 0x%04x 0x%04x %2d '%s'\n",
               k+1, fh->inuse, fh->modified, fh->task,
                  fh->fh_flags, fh->access, fh->volume,
                  fh->fname);
        handles++;
      }
    }
    fprintf(f, "count-open-files:%4d\n" , handles);
    fflush(f);
  }
}

