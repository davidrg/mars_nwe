
/* nwshare.c, 13-Apr-00 */
/* (C)opyright (C) 1993-2000  Martin Stover, Marburg, Germany
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

#include "nwvolume.h"
#include "nwfile.h"
#include "connect.h"

#include "unxfile.h"
#include "nwshare.h"

/* changed by: Ingmar Thiemann <ingmar@gefas.com>
 * share_file() is now always called, when a file is opened.
 * The open_mode is the access mode of the file open function.
 */

typedef struct _tagShareLock {
  int l_start;
  int l_len;
  int fd;
  int exclusive; /* exclusive lock */
  struct _tagShareLock *next;
} ShareLock;

typedef struct _tagShareINode {
  int inode;
  int or;      /* open read  */
  int ow;      /* open write */
  int dr;      /* deny read  */
  int dw;      /* deny write */
#if 0
  int cm;      /* compatible mode */
#endif
  /* ------------ */
  int fl;      /* file lock  */
  int ex;      /* exlusive file lock  */
  ShareLock *first_lock;
  struct _tagShareINode *next;
} ShareINode;

typedef struct _tagShareDev {
  int dev;

  int fd_sm;   /* semaphor for locking operation */
  int fd_or;   /* open read  */
  int fd_ow;   /* open write */
  int fd_dr;   /* deny read  */
  int fd_dw;   /* deny write */
#if 0  
  int fd_cm;   /* compatible mode */
#endif  
  int fd_fl;   /* file lock  */
  ShareINode *first_inode;
  struct _tagShareDev *next;
} ShareDev;

static ShareDev *first_dev = NULL;

static char *path_share_lock_files=NULL;

int share_file(int dev, int inode, int open_mode, int action)
/*  open_mode is the same as 'access' in file_creat_open():
 *    0x001 = open for read
 *    0x002 = open for write
 *    0x004 = deny read
 *    0x008 = deny write

 *********    0x010 = compatible mode

 *    next are 'open_modes' by nw_log_file() routine.
 *    0x100 = file lock ( normally exclusive )
 *    0x200 = file lock shared ro flag
 *  action:
 *    0 = remove
 *    1 = add
 *    2 = only test
 */
{
  ShareDev *sd = NULL, **psd;
  ShareINode *si = NULL, **psi;
  int result = 0, act_mode = 0;
  struct flock flockd;
  char tbuf[200];
  sprintf(tbuf,"dev=0x%x,inode=%d,open_mode=0x%x,action=%d",
                 dev, inode, open_mode, action);
  
  if (open_mode==0) {
    XDPRINTF((1, 0, "Wrong openmode in share_file %s", tbuf));
    return(-1);
  }

#if 0
  /* mst: 06-Apr-00, map compatible modes */
  /* mst: 13-Apr-00 removed */
  if (access & 0x10) {
    if (!(acces&2)) { /* readonly */
      if (entry31_flags&1)
        access &= ~0x10;
    } else {
      if (entry31_flags&2) {
        access &= ~0x10;
        access |= 0x8;
      }
    }
  }
#endif

  /* look for open device */
  for (psd=&first_dev; *psd; psd=&(*psd)->next) {
    if ((*psd)->dev == dev) {
      sd = *psd;
      break;
    }
  }
  if (action==0 && !sd) {
    XDPRINTF((1, 0, "Could not find share device to remove %s", tbuf));
    return(-1);
  }
  if (!sd) {
    /* new device */
    sd = (ShareDev*) xcmalloc( sizeof(ShareDev) );
    if (!sd) {
      XDPRINTF((1, 0, "Could not allocate new share device %s", tbuf));
      return(-1);
    }
    sd->next = *psd;
    *psd     = sd;
    sd->first_inode = NULL;
    sd->fd_sm = -1;
    sd->dev = dev;
  }
  
  /* look for open inode */
  for (psi=&sd->first_inode; *psi; psi=&(*psi)->next) {
    if ((*psi)->inode == inode) {
      si = *psi;
      break;
    }
  }

  if (action==0 && !si) {
    XDPRINTF((1, 0, "Could not find share inode to remove %s", tbuf));
    if (!sd->first_inode) {
      *psd = sd->next;
      xfree( sd );
    }
    return(-1);
  }

  if (!si) {
    /* new inode */
    si = (ShareINode*) xcmalloc( sizeof(ShareINode) );
    if (!si) {
      XDPRINTF((1, 0, "Could not allocate new share inode %s", tbuf));
      if (!sd->first_inode) {
        *psd = sd->next;
        xfree( sd );
      }
      return(-1);
    }
    si->next = *psi;
    *psi = si;
    si->first_lock = NULL;
    si->inode = inode;
  }

  if (sd->fd_sm == -1) {
    /* open share files */
      char buff[300];
      int l;
      if (NULL==path_share_lock_files) {
      /* get path for share files */
        if (get_ini_entry(NULL, 41, buff, sizeof(buff)) && *buff) 
          new_str(path_share_lock_files, buff);
        else
          new_str(path_share_lock_files, "/var/spool/nwserv/.locks");
        seteuid(0);
        unx_xmkdir(path_share_lock_files, 0755);
    } else
      seteuid(0);
    l=sprintf(buff, "%s/%x.sm", path_share_lock_files, dev);
    sd->fd_sm = open(buff, O_RDWR|O_CREAT, 0600);
#if 0
    buff[l-2]='c';
    sd->fd_cm = open(buff, O_RDWR|O_CREAT, 0600);
#endif    
    buff[l-2]='o'; buff[l-1]='r';
    sd->fd_or = open(buff, O_RDWR|O_CREAT, 0600);
    buff[l-2]='d';
    sd->fd_dr = open(buff, O_RDWR|O_CREAT, 0600);
    buff[l-1]='w';
    sd->fd_dw = open(buff, O_RDWR|O_CREAT, 0600);
    buff[l-2]='o';
    sd->fd_ow = open(buff, O_RDWR|O_CREAT, 0600);
    /* lock file */
    buff[l-2]='f'; buff[l-1]='l';
    sd->fd_fl = open(buff, O_RDWR|O_CREAT, 0600);
    reseteuid();

    if (sd->fd_sm<0 || sd->fd_or<0 || sd->fd_dr<0 ||
                       sd->fd_ow<0 || sd->fd_dw<0 
#if 0                       
                       || sd->fd_cm<0 
#endif                       
                       || sd->fd_fl<0 
                       )
    {
      if (sd->fd_sm>-1) close(sd->fd_sm);
      if (sd->fd_or>-1) close(sd->fd_or);
      if (sd->fd_dr>-1) close(sd->fd_dr);
      if (sd->fd_ow>-1) close(sd->fd_ow);
      if (sd->fd_dw>-1) close(sd->fd_dw);
#if 0      
      if (sd->fd_cm>-1) close(sd->fd_cm);
#endif      
      if (sd->fd_fl>-1) close(sd->fd_fl);
      xfree( si );
      *psd = sd->next;
      xfree( sd );
      buff[l-2]='\0';
      XDPRINTF((1, 0, "Cannot open sharefile=`%s`", buff));
      return(-1);
    }
  }

  flockd.l_whence = SEEK_SET;
  flockd.l_start  = inode;
  flockd.l_len    = 1;

  flockd.l_type = F_WRLCK;
  fcntl(sd->fd_sm, F_SETLKW, &flockd);   /* set semaphor */

  if (action==1 || action==2) {          /* TEST */

    if (si->or ) act_mode |= 0x01;
    if (si->ow ) act_mode |= 0x02;
    if (si->dr ) act_mode |= 0x04;
    if (si->dw ) act_mode |= 0x08;
#if 0
    if (si->cm ) act_mode |= 0x10;
#endif    
    if (si->fl ) act_mode |= 0x100;
    if (si->ex ) act_mode |= 0x200;

    if (open_mode & 0xff) {
      if (!(act_mode & 0x01)) {
	/* do not set flockd.l_whence because after F_GETLK kernel
	 * set it as SEEK_SET */
        flockd.l_type = F_WRLCK;
	flockd.l_start  = inode;
	flockd.l_len    = 1;
        fcntl(sd->fd_or, F_GETLK, &flockd);  /* read */
        if (flockd.l_type != F_UNLCK)
          act_mode |= 0x01;
      }
      if (!(act_mode & 0x04)) {
        flockd.l_type = F_WRLCK;
	flockd.l_start  = inode;
	flockd.l_len    = 1;
        fcntl(sd->fd_dr, F_GETLK, &flockd);  /* deny read */
        if (flockd.l_type != F_UNLCK)
          act_mode |= 0x04;
      }
      if (!(act_mode & 0x02)) {
        flockd.l_type = F_WRLCK;
	flockd.l_start  = inode;
	flockd.l_len    = 1;
        fcntl(sd->fd_ow, F_GETLK, &flockd);  /* write */
        if (flockd.l_type != F_UNLCK)
          act_mode |= 0x02;
      }
      if (!(act_mode & 0x08)) {
        flockd.l_type = F_WRLCK;
	flockd.l_start  = inode;
	flockd.l_len    = 1;
        fcntl(sd->fd_dw, F_GETLK, &flockd);  /* deny write */
        if (flockd.l_type != F_UNLCK)
          act_mode |= 0x08;
      }
#if 0
      if (!(act_mode & 0x10)) {
        flockd.l_type = F_WRLCK;
	flockd.l_start  = inode;
	flockd.l_len    = 1;
        fcntl(sd->fd_cm, F_GETLK, &flockd);  /* compatible mode */
        if (flockd.l_type != F_UNLCK)
          act_mode |= 0x10;
      }
#endif
    }

    if ((open_mode & 0x300) &&  !(act_mode & 0x100)) {
      flockd.l_type = F_WRLCK;
      flockd.l_start  = inode;
      flockd.l_len    = 1;
      fcntl(sd->fd_fl, F_GETLK, &flockd);  /* lock file */
      if (flockd.l_type != F_UNLCK)
        act_mode |= (flockd.l_type == F_WRLCK) ? 0x100|0x200 : 0x100;
    }

    if (act_mode & 0xff) { // already opened by other

#if 0  /* mst:13-Apr-00, I think this is all NOT needed. */
      if (entry8_flags & 0x100) { /* dos ? mode  */
        if ( (open_mode & 0x10) ? !(act_mode & 0x10) && (act_mode & 0x06) :
                                   (act_mode & 0x10) && (open_mode & 0x06))
          result=-1;
      } else { /* Standard Novell mode mode, i hope */
        if ((open_mode & 0x10) != (act_mode & 0x10))
          result = -1;  /* if one file opened compatible then all files
                           must be opened compatible */
#if 0        
        else if ( (!(act_mode & 0xc)) && (open_mode & 0xc) )
          result = -1;  /* already opened DENYNO but now DENYXY wanted */
#endif      
      }
#endif

      if (!result && (((open_mode & 0x01) && (act_mode & 0x04)) ||
                      ((open_mode & 0x02) && (act_mode & 0x08)) ||
                      ((open_mode & 0x04) && (act_mode & 0x01)) ||
                      ((open_mode & 0x08) && (act_mode & 0x02))))
        result=-1;
    }

    if (!result &&  /* lock file */
                   (((open_mode & 0x100) && (act_mode & 0x200)) || 
                    ((open_mode & 0x200) && (act_mode & 0x100))))
      result = -1;

    if (action==1 && !result) {         /* ADD */
      flockd.l_type = F_RDLCK;
      flockd.l_start  = inode;
      flockd.l_len    = 1;
      if (open_mode & 0x01) {           /* read */
        if (!si->or) {
          fcntl(sd->fd_or, F_SETLK, &flockd);
        }
        si->or ++;
      }
      if (open_mode & 0x04) {           /* deny read */
        if (!si->dr) {
          fcntl(sd->fd_dr, F_SETLK, &flockd);
        }
        si->dr ++;
      }
      if (open_mode & 0x02) {           /* write */
        if (!si->ow) {
          fcntl(sd->fd_ow, F_SETLK, &flockd);
        }
        si->ow ++;
      }
      if (open_mode & 0x08) {           /* deny write */
        if (!si->dw) {
          fcntl(sd->fd_dw, F_SETLK, &flockd);
        }
        si->dw ++;
      }
#if 0
      if (open_mode & 0x10) {           /* compatible mode */
        if (!si->cm) {
          fcntl(sd->fd_cm, F_SETLK, &flockd);
        }
        si->cm ++;
      }
#endif
      if (open_mode & 0x100) {          /* lock file */
        if (!si->fl) {
          flockd.l_type = (open_mode & 0x200) ? F_RDLCK : F_WRLCK;
          fcntl(sd->fd_fl, F_SETLK, &flockd);
        }
        si->fl ++;
        if (!(open_mode & 0x200))       /* exclusive */
          si->ex ++;
      }
    }
  } else if (action==0) {               /* REMOVE */
    flockd.l_start  = inode;
    flockd.l_len    = 1;
    flockd.l_type = F_UNLCK;
    if (open_mode & 0x01)               /* read */
      if (si->or && !(--si->or))
        fcntl(sd->fd_or, F_SETLK, &flockd);
    if (open_mode & 0x04)               /* deny read */
      if (si->dr && !(--si->dr))
        fcntl(sd->fd_dr, F_SETLK, &flockd);
    if (open_mode & 0x02)               /* write */
      if (si->ow && !(--si->ow))
        fcntl(sd->fd_ow, F_SETLK, &flockd);
    if (open_mode & 0x08)               /* deny write */
      if (si->dw && !(--si->dw))
        fcntl(sd->fd_dw, F_SETLK, &flockd);
#if 0
    if (open_mode & 0x10)               /* compatible mode */
      if (si->cm && !(--si->cm))
        fcntl(sd->fd_cm, F_SETLK, &flockd);
#endif    
    if (open_mode & 0x100)              /* file lock */
      if (si->fl && !(--si->fl)) {
        fcntl(sd->fd_fl, F_SETLK, &flockd);
        si->ex = 0;
      }
  }

  flockd.l_type = F_UNLCK;
  flockd.l_start  = inode;
  flockd.l_len    = 1;
  fcntl(sd->fd_sm, F_SETLK, &flockd);   /* realise semaphor */

  if (!si->or && !si->ow && !si->dr && !si->dw 
#if 0                
                && !si->cm 
#endif
                && !si->fl ) {
    /* release inode */
    while (si->first_lock) {
      ShareLock *p = si->first_lock;
      si->first_lock = p->next;
      xfree( p );
    }
    *psi = si->next;
    xfree( si );
  }

  if (!sd->first_inode) {
    /* release device */
    close(sd->fd_sm);
    close(sd->fd_or);
    close(sd->fd_ow);
    close(sd->fd_dr);
    close(sd->fd_dw);
#if 0
    close(sd->fd_cm);
#endif    
    close(sd->fd_fl);
    *psd = sd->next;
    xfree( sd );
  }
  XDPRINTF((3, 0, "share_file result=%d %s,act_mode=%x", result, tbuf, act_mode));
  return(result);
}

static int _get_inode( int dev, int inode, ShareDev **psd, ShareINode **psi )
{
  for (*psd=first_dev; *psd; *psd=(*psd)->next)
    if ((*psd)->dev == dev)
      break;
  if (!*psd)
    return 0;
  for (*psi=(*psd)->first_inode; *psi; *psi=(*psi)->next)
    if ((*psi)->inode == inode)
      return 1;
  return 0;
}

void catch_alarm (int sig)
{
  signal(sig, SIG_IGN);
}

#define OFFSET_MAX      0x7fffffff

int share_lock( int dev, int inode, int fd, int action, 
                int lock_flag, int l_start, int l_len, int timeout )
/* 
 * action:
 *   0 = unlock
 *   1 = lock
 *   2 = testonly
 *
 * lock_flag:
 *  <0 = unlock
 *   1 = exclusive
 *   3 = shared ro
 */

{
  ShareDev   *sd;
  ShareINode *si;
  ShareLock  *sl = NULL, **psl;
  int result = 0;
  struct flock flockd;
  char tbuf[200];
  sprintf(tbuf,"dev=0x%x,inode=%d,fd=%d,action=%d,lock_flag=%d",
                dev, inode, fd, action, lock_flag);

  if (!_get_inode( dev, inode, &sd, &si )) {
    XDPRINTF((1, 0, "Could not find share for lock %s", tbuf));
    return -1;
  }

  flockd.l_whence = SEEK_SET;
  flockd.l_start  = l_start;
  flockd.l_len    = l_len;

  /* find lock */
  for (psl=&si->first_lock; *psl; psl=&(*psl)->next) {
    if ((*psl)->l_start < l_start + l_len
          ||  (!l_len && (*psl)->l_start <= l_start)) {
      sl = *psl;
      break;
    }
  }
      
  if (!action) {
    /* unlock */
    if (sl && sl->fd == fd && sl->l_start == l_start && sl->l_len == l_len) {
      flockd.l_type = F_UNLCK;
      fcntl( fd, F_SETLK, &flockd );
      *psl = sl->next;
      xfree( sl );
    } else {
      XDPRINTF((2, 0, "unlock: can't find proper lock pid=%d uid=%d fd=%d %d, %d", getpid(), geteuid(), fd, l_start, l_len));
      result = -0xff;
    }
  } else {
    /* lock or test */
    if (sl && (l_start < sl->l_start + sl->l_len || !sl->l_len)
           && (sl->exclusive || lock_flag == 1) )
      result = -0xfd; /* collision */
    else {
      flockd.l_type = (lock_flag == 1) ? F_WRLCK : F_RDLCK;
      if (action==1 && timeout > 17) {/* if timeout is relatively short
                                       * do not set the alarm. /lenz */
        signal( SIGALRM, catch_alarm );
        alarm( timeout / 18 );
        result = fcntl( fd, F_SETLKW, &flockd );
        alarm( 0 );
        signal( SIGALRM, SIG_IGN );
      } else {
      result = fcntl( fd, (action==1) ? F_SETLK : F_GETLK, &flockd );
      }
      if (result) {
	if (!timeout) /* my NW 3.12 returns 0xff if timeout == 0. /lenz */
	  result = -0xff;
	else
	  result = -0xfe;
      }
      if (!result) {
        if (action == 1) {
          /* add to list */
          sl = (ShareLock*) xmalloc( sizeof(ShareLock) );
          sl->next = *psl;
          *psl        = sl;
          sl->l_start = l_start;
          sl->l_len   = l_len;
          sl->fd      = fd;
          sl->exclusive = (lock_flag == 1) ? 1 : 0;
        } else
          if (flockd.l_type != F_UNLCK)
            result = -1;
      }
    }
  }
  XDPRINTF((3, 0, "share_lock result=%d %s", result, tbuf));
  return result;
}

int share_unlock_all( int dev, int inode, int fd )
{
  ShareDev *sd;
  ShareINode *si;
  ShareLock **psl;
  int result = 0;
  struct flock flockd;
  char tbuf[200];
  sprintf(tbuf,"dev=0x%x,inode=%d,fd=%d", dev, inode, fd);

  if (!_get_inode( dev, inode, &sd, &si )) {
    XDPRINTF((1, 0, "Could not find share for unlock_all %s", tbuf));
    return -1;
  }

  flockd.l_type   = F_UNLCK;
  flockd.l_whence = SEEK_SET;

  for (psl=&si->first_lock; *psl; ) {
    if ((*psl)->fd == fd) {
      ShareLock *sl = *psl;
      flockd.l_start = sl->l_start;
      flockd.l_len   = sl->l_len;
      fcntl( fd, F_SETLK, &flockd );
      *psl = sl->next;
      xfree( sl );
    } else
      psl=&(*psl)->next;
  }
  XDPRINTF((3, 0, "share_unlock_all,result=%d %s", result, tbuf));
  return result;
}

void dump_locks( int dev, int inode, int fd, FILE* f)
{
  ShareDev *sd;
  ShareINode *si;
  ShareLock *psl;
  char tbuf[200];
  sprintf(tbuf,"dev=0x%x,inode=%d,fd=%d", dev, inode, fd);

  if (!_get_inode( dev, inode, &sd, &si )) {
    XDPRINTF((1, 0, "Could not find share for unlock_all %s", tbuf));
  }

  for (psl=si->first_lock; psl; ) {
    fprintf(f, "fd=%d %d-%d ", psl->fd, psl->l_start, psl->l_len);
    psl = psl->next;
  }
  fprintf(f, "\n");
}


typedef struct S_SHARESET{
  int   type;
  int   lock_flag;
  int   timeout;  /* not used yet    */
  int   locked;   /* is entry locked */
  
  int   dev;
  int   inode;

  int   datalen;
  char  *data;      /* used for Logical Records */
  struct S_SHARESET *next;
} SHARESET;

static SHARESET *first_set = NULL;

static int lock_unlock_pset(SHARESET *ps, int lock_flag)
{
  int result;
  switch (ps->type)
  {
    case 1:  /* file share */
      if (lock_flag>-1) 
        result = share_file(ps->dev, ps->inode, 
                         lock_flag ? lock_flag 
                                   : (ps->lock_flag
                                        ? ps->lock_flag
                                        : 1),
                                    1);
      else
        result = share_file(ps->dev, ps->inode, 0x300, 0);
    break;

    case 2:  /* logical records */
      if (lock_flag>-1) 
        result = nw_log_logical_record(
                         lock_flag ? lock_flag 
                                   : (ps->lock_flag
                                        ? ps->lock_flag
                                        : 1),
                         ps->timeout, /* timeout not used yet */
                         ps->datalen,
                         ps->data);
      else
        result = nw_log_logical_record(
                         -1,
                         ps->timeout, /* timeout not used yet */
                         ps->datalen,
                         ps->data);
    break;

    default : 
      result = -1;
  }
  return(result);
}

int share_set_file_add_rm(int lock_flag, int dev, int inode)
{
  if (lock_flag > -1) {
    SHARESET *ps  = (SHARESET*)xcmalloc(sizeof(SHARESET));
    ps->next      = first_set;
    first_set     = ps;
    ps->dev       = dev;
    ps->inode     = inode;
    ps->lock_flag = lock_flag;
  } else if (lock_flag == -2) {
    SHARESET **pset = &first_set;
    while (*pset) {
      SHARESET *ps = *pset;
      *pset = (*pset)->next;
      if (1 == ps->type) {
        xfree(ps);
      } 
    }
  }
  return(0);
}

int share_set_logrec_add_rm(int lock_flag, int timeout, int len, char *data)
{
  if (lock_flag > -1) {
    SHARESET *ps = (SHARESET*)xcmalloc(sizeof(SHARESET));
    ps->next  = first_set;
    first_set = ps;
    ps->datalen = len;
    ps->data    = xcmalloc(len+1);
    memcpy(ps->data, data, len);
    ps->lock_flag = lock_flag;
  } else if (lock_flag == -2) {
    SHARESET **pset = &first_set;
    while (*pset) {
      SHARESET *ps = *pset;
      *pset = (*pset)->next;
      if (2 == ps->type) {
        xfree(ps->data);
        xfree(ps);
      } 
    }
  }
  return(0);
}

int share_handle_lock_sets(int type, int lock_flag, int timeout)
/* type:
 *   1 = file share
 *   2 = logical record
 *
 * lock_flag:
 *  -2      = clear (delete) set
 *  -1      = release (unshare/unlock) set
 *   0/1/3  = lock/share set
 *  
 * timeout:  not used yet. !!
 */
{
  SHARESET **pset = &first_set;
  while (*pset) {
    SHARESET *ps = *pset;
    *pset = (*pset)->next;
    if (type & ps->type) {
      if (ps->locked && (lock_flag < 0)) {
        if (!lock_unlock_pset(ps, -1))
          ps->locked = 0;
      } else if ((!ps->locked) && lock_flag > -1){
        if (lock_unlock_pset(ps, lock_flag)){
          /* remove all locks */
          share_handle_lock_sets(type, -1, 0);
          return(-1);
        } else
          ps->locked = 1;
      }
      if (lock_flag == -2) {  /* remove node */
        xfree(ps->data);
        xfree(ps);
      } 
    } 
  }
  return(0);
}



