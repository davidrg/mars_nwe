/* nwshare.c  21-Jul-97 */

/* (C)opyright (C) 1993,1997  Martin Stover, Marburg, Germany
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

#define MAX_SH_OP_DEV  20

typedef struct {
  int fd_r;
  int fd_wr;
  int in_use;
  int dev;
} SH_OP_DEV;

static SH_OP_DEV sh_op_devs[MAX_SH_OP_DEV];
static int count_sh_op_dev=0;

static char *path_share_lock_files=NULL;

int share_file(int dev, int inode, int sh_mode)
/*  sh_mode
 *    0  : remove share
 *    1  : add_ro_r_share
 *    4  : add_ro_w_share
 *
 *    2  : add_wr_r_share
 *    8  : add_wr_w_share
 * ----
 * 0x10  : test
 */
{
  int k      = count_sh_op_dev;
  int fr     = -1;
  int fo     = -1;
  int result = -1;
  SH_OP_DEV *sod;
  struct flock flockd;
  
  while (k--) {
    sod=&(sh_op_devs[k]);
    if (sod->fd_r < 0) {
      fr=k;
    } else if (sod->dev == dev) {
      fo=k;
      break;
    }
  }
  
  if ((!sh_mode) && fo==-1) {
    XDPRINTF((1, 0, "Could not found share to remove"));
    return(-1);
  }
  
  if (fo==-1 && fr==-1) {
    if (count_sh_op_dev < MAX_SH_OP_DEV)
      fr=count_sh_op_dev++;
    else {
      XDPRINTF((1, 0, "Too much 'share devs'"));
      return(-1);
    }
  }
  
  flockd.l_whence = SEEK_SET;
  flockd.l_start  = inode;
  flockd.l_len    = 1;

  if (!sh_mode) {
    sod=&(sh_op_devs[fo]);
    flockd.l_type = F_UNLCK;
    (void)fcntl(sod->fd_r,  F_SETLK, &flockd);
    (void)fcntl(sod->fd_wr, F_SETLK, &flockd);
    if (--sod->in_use < 1) {
      close(sod->fd_r);
      close(sod->fd_wr);
      sod->fd_r  = -1;
      sod->fd_wr = -1;
      sod->dev   = -1;
    }
  } else {
    if (fo == -1)  {
      char buff[300];
      int l;
      sod=&(sh_op_devs[fr]);
      if (NULL==path_share_lock_files) {
        if (get_ini_entry(NULL, 41, buff, sizeof(buff)) && *buff) 
          new_str(path_share_lock_files, buff);
        else
          new_str(path_share_lock_files, "/var/spool/nwserv/.locks");
        seteuid(0);
        unx_xmkdir(path_share_lock_files, 0755);
        reseteuid();
      }
      
      l=sprintf(buff, "%s/%x.r", path_share_lock_files, dev);
      seteuid(0);
      if (-1 < (sod->fd_r=open(buff, O_RDWR|O_CREAT, 0600)) ) {
        buff[l-1]='w';
        if (0 > (sod->fd_wr=open(buff, O_RDWR|O_CREAT, 0600) )){
          close(sod->fd_r);
          sod->fd_r=-1;
        }
      }
      reseteuid();
      
      if (sod->fd_r < 0) {
        XDPRINTF((1, 0, "Cannot open sharefile=`%s`", buff));
        return(-1);
      }
      sod->dev = dev;
    } else
      sod=&(sh_op_devs[fo]);
    
    if (!(sh_mode&0x10)){
      if (sh_mode & 1) {
        flockd.l_type = (sh_mode & 4) ?  F_WRLCK : F_RDLCK;
        result=fcntl(sod->fd_r, F_SETLK, &flockd);
      } else result=0;

      if ((!result) && (sh_mode & 2) ) {
        flockd.l_type = (sh_mode & 8) ?  F_WRLCK : F_RDLCK;
        result=fcntl(sod->fd_wr, F_SETLK, &flockd);
      }
      
      if (result) {
        XDPRINTF((3, 0, "Cannot lock share sh_mode=%d", sh_mode));
      } else {
        sod->in_use++;
      }
    } else {  /* only testing */
      if (sh_mode & 1) {
        flockd.l_type = (sh_mode & 4) ?  F_WRLCK : F_RDLCK;
        result=fcntl(sod->fd_r, F_GETLK, &flockd);
      } else if (sh_mode & 2) {
        flockd.l_type = (sh_mode & 8) ?  F_WRLCK : F_RDLCK;
        result=fcntl(sod->fd_wr, F_GETLK, &flockd);
      } else result=-1;
      return(flockd.l_type == F_UNLCK) ? 0 : -1;
    }
  }
  return(result);
}

