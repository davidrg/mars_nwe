/* nwvolume.c  02-Aug-97 */
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
#include <errno.h>
#include <sys/vfs.h>

#ifndef LINUX
#include <sys/statvfs.h>
#define statfs statvfs
#endif

#include <utime.h>

#include "nwfname.h"
#include "nwvolume.h"

NW_VOL     *nw_volumes=NULL;
int        used_nw_volumes=0;
int        loaded_namespaces=0;
uint8      *home_dir=NULL;
int        home_dir_len=0;
char       *path_vol_inodes_cache=NULL;

static int max_nw_vols=MAX_NW_VOLS;

static void volume_to_namespace_map(int volume, NW_VOL *vol)
{
  struct stat statb;
  DEV_NAMESPACE_MAP dnm;
  if (stat(vol->unixname, &statb)) {
    XDPRINTF((1, 0, "cannot stat vol=%d, `%s`", volume, vol->unixname));
    return;
  }
  dnm.dev       = statb.st_dev;
  dnm.namespace = 0; /* NAMESPACE DOS */
  (void) nw_vol_inode_to_handle(volume, statb.st_ino, &dnm);
}

void nw_init_volumes(FILE *f)
/* f = inifile Pointer, must be opened !! */
{
  static int volumes_is_init=0;
  int    what;
  uint8  buff[256];
  int    k = -1;
  if (!volumes_is_init) {
    volumes_is_init++;
    rewind(f);
    if (get_ini_entry(f, 61, buff, sizeof(buff))) {
      max_nw_vols=atoi(buff);
      if (max_nw_vols < 2)
        max_nw_vols=MAX_NW_VOLS;
    }
    nw_volumes=(NW_VOL*)xcmalloc(max_nw_vols*sizeof(NW_VOL));
  } else {
    while (++k < max_nw_vols) {
      int i = -1;
      while (++i < nw_volumes[k].maps_count)
        xfree(nw_volumes[k].dev_namespace_maps[i]);
      nw_volumes[k].maps_count = 0;
    }
  }
  rewind(f);
  used_nw_volumes   = 0;
  loaded_namespaces = 0;
  new_str(path_vol_inodes_cache, "/var/spool/nwserv/.volcache");
  while (0 != (what = get_ini_entry(f, 0, buff, sizeof(buff)))) {
    if ( what == 1 && used_nw_volumes < max_nw_vols && strlen((char*)buff) > 3){
      uint8 sysname[256];
      uint8 unixname[256];
      char  optionstr[256];
      char  *p;
      int   len;
      int   founds = sscanf((char*)buff, "%s %s %s",sysname, unixname, optionstr);
      if (founds > 1) {
        NW_VOL *vol=&(nw_volumes[used_nw_volumes]);
        vol->options    = VOL_NAMESPACE_DOS;
        loaded_namespaces |= VOL_NAMESPACE_DOS;
        up_fn(sysname);
        new_str(vol->sysname, sysname);
        if (1 == (len = strlen((char*)unixname)) && unixname[0] == '~') {
          vol->options  |= VOL_OPTION_IS_HOME;
          vol->options  |= VOL_OPTION_REMOUNT;
          unixname[0] = '\0';
          len = 0;
        } else if (unixname[len-1] != '/') {
          unixname[len++] = '/';
          unixname[len]   = '\0';
        }
        vol->unixnamlen = len;
        new_str(vol->unixname, unixname);
        if (founds > 2) {
          for (p=optionstr; *p; p++) {
            switch (*p) {
              case 'i' : vol->options
                         |= VOL_OPTION_IGNCASE;
                         break;

              case 'k' : vol->options
                         |= VOL_OPTION_DOWNSHIFT;
                         break;

              case 'm' : vol->options
                         |= VOL_OPTION_REMOUNT;
                         break;

              case 'o' : vol->options
                         |= VOL_OPTION_ONE_DEV;
                         break;

              case 'p' : vol->options
                         |= VOL_OPTION_IS_PIPE;
                         break;

              case 'r' : vol->options
                         |= VOL_OPTION_READONLY;
                         break;

              case 'O' : vol->options
                         |= VOL_NAMESPACE_OS2;
                         loaded_namespaces |= VOL_NAMESPACE_OS2;
                         break;

              case 'N' : vol->options
                         |= VOL_NAMESPACE_NFS;
                         loaded_namespaces |= VOL_NAMESPACE_NFS;
                         break;

              default : break;
            }
          }
        }
        used_nw_volumes++;
        if (vol->options & VOL_OPTION_ONE_DEV) {
          vol->max_maps_count = 1;
          vol->high_inode     = 0xffffffff;
        } else {
          vol->max_maps_count = MAX_DEV_NAMESPACE_MAPS;
          vol->high_inode     = 0xfffffff;
        }
        if (vol->unixnamlen)
          volume_to_namespace_map(used_nw_volumes-1, vol);
      }
    } else if (what==40) {  /* path for vol/dev/inode->path cache */
      new_str(path_vol_inodes_cache, buff);
    }
  } /* while */
}

void nw_setup_home_vol(int len, uint8 *fn)
{
  int k=used_nw_volumes;
  uint8 unixname[258];
  unixname[0] = '\0';
  xfree(home_dir);
  home_dir_len=0;
  if (len > 0) {
    strmaxcpy(unixname, fn, len);
    if (unixname[len-1] != '/') {
      unixname[len++] = '/';
      unixname[len]   = '\0';
    }
    new_str(home_dir, unixname);
    home_dir_len=len;
  }
  while (k--) { /* now set all HOME volumes */
    if (nw_volumes[k].options & VOL_OPTION_IS_HOME)  {
      int i = -1;
      while (++i < nw_volumes[k].maps_count)
        xfree(nw_volumes[k].dev_namespace_maps[i]);
      nw_volumes[k].maps_count = 0;
      nw_volumes[k].unixnamlen = len;
      new_str(nw_volumes[k].unixname, unixname);
      if (len>0)
        volume_to_namespace_map(k, &(nw_volumes[k]));
    }
  }
}

static int look_name_space_map(NW_VOL *v, DEV_NAMESPACE_MAP *dnm,
                               int  do_insert)
{
    DEV_NAMESPACE_MAP *mp;
    int k=-1;
    while (++k < v->maps_count) {
      mp=v->dev_namespace_maps[k];
      if (mp->dev == dnm->dev && mp->namespace == dnm->namespace)
        return(k);
    }
    if (do_insert && v->maps_count < v->max_maps_count) {
      /* now do insert the new map */
      mp = v->dev_namespace_maps[v->maps_count++] =
         (DEV_NAMESPACE_MAP*) xmalloc(sizeof(DEV_NAMESPACE_MAP));
      memcpy(mp, dnm, sizeof(DEV_NAMESPACE_MAP));
      return(k);
    }
  return(-1);
}

uint32 nw_vol_inode_to_handle(int volume, ino_t inode,
                             DEV_NAMESPACE_MAP *dnm)
{
  if (volume > -1 && volume < used_nw_volumes) {
    NW_VOL *v= &(nw_volumes[volume]);
    if (inode > 0 && inode <= v->high_inode) {
      int result = look_name_space_map(v, dnm, 1);
      if (result > -1) {
        uint32 handle = (v->options & VOL_OPTION_ONE_DEV)
                          ? (uint32)inode
                          :  (((uint32)result) << 28) | (uint32) inode;
        XDPRINTF((3,0, "Handle map inode=%d, dev=%d, namespace=%d to handle 0x%x",
              inode, dnm->dev, dnm->namespace, handle));
        return(handle);
      }
    }
  }
  XDPRINTF((1,0, "Cannot map inode=%d, dev=%d, namespace=%d to vol=%d handle",
              inode, dnm->dev, dnm->namespace, volume));
  return(0L);
}

ino_t nw_vol_handle_to_inode(int volume, uint32 handle,
                               DEV_NAMESPACE_MAP *dnm)
/* converts volume, handle to dev->inode->namespace */
{
  if (handle > 0 && volume > -1 && volume < used_nw_volumes) {
    NW_VOL *v= &(nw_volumes[volume]);
    int entry = (v->options & VOL_OPTION_ONE_DEV)
                      ? 0
                      : (int) ((handle >> 28) & 0xF);
    if (entry > -1 && entry < v->maps_count) {
      if (dnm) memcpy(dnm, v->dev_namespace_maps[entry],
                            sizeof(DEV_NAMESPACE_MAP));
      XDPRINTF((3, 0, "vol=%d, handle=0x%x to ino=%d, dev=%d, namespace=%d",
                 volume, handle, (int)(handle & v->high_inode),
                 v->dev_namespace_maps[entry]->dev,
                 v->dev_namespace_maps[entry]->namespace));
      return((ino_t) (handle & v->high_inode));
    }
  }
  XDPRINTF((1, 0, "Can't vol=%d, handle=0x%x to inode", volume, handle));
  return(-1);
}

int nw_get_volume_number(uint8 *volname, int namelen)
/* Get Volume Number with name */
/* returns Volume Nummer or if error < 0 */
{
  int result = -0x98; /* Volume not exist */
  uint8   vname[255];
  int j = used_nw_volumes;
  strmaxcpy(vname, volname, namelen);
  up_fn(vname);
  while (j--) {
    if (!strcmp((char*)nw_volumes[j].sysname, (char*)vname)) {
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
  if (volnr > -1 && volnr < used_nw_volumes) {
    if (volname != NULL) {
      strcpy((char*)volname, (char*)nw_volumes[volnr].sysname);
      result = strlen((char*)volname);
    } else result= strlen((char*)nw_volumes[volnr].sysname);
  } else {
    if (NULL != volname) *volname = '\0';
    if (volnr < max_nw_vols) result=0;
  }
  if (nw_debug > 4) {
    uint8 xvolname[10];
    if (!volname) {
      volname = xvolname;
      *volname = '\0';
    }
    XDPRINTF((5,0,"GET_VOLUME_NAME von:%d = %s: ,result=0x%x", volnr, volname, result));
  }
  return(result);
}


/* stolen from GNU-fileutils */
static long adjust_blocks (long blocks, int fromsize, int tosize)
{
  if (fromsize == tosize)       /* E.g., from 512 to 512.  */
    return blocks;
  else if (fromsize > tosize)   /* E.g., from 2048 to 512.  */
    return blocks * (fromsize / tosize);
  else                          /* E.g., from 256 to 512.  */
    return (blocks + (blocks < 0 ? -1 : 1)) / (tosize / fromsize);
}

static int get_fs_usage(char *path, struct fs_usage *fsp)
{
  struct statfs fsd;
  if (statfs (path, &fsd) < 0) return (-1);
#if 0
/* test for a 'big' volume */
fsd.f_blocks = 3733075;
fsd.f_bfree  = 1531638;
fsd.f_bavail = 1338518;
fsd.f_files  = 966656;
fsd.f_ffree  = 916066;
fsd.f_bsize  = 1024;
#elif 0
/* test for other 'big' volume */
fsd.f_blocks = 1783108;
fsd.f_bfree  = 892839;
fsd.f_bavail = 800680;
fsd.f_files  = 460800;
fsd.f_ffree  = 415474;
fsd.f_bsize  = 1024;
#endif
  XDPRINTF((3, 0,
    "blocks=%d, bfree=%d, bavail=%d, files=%d, ffree=%d, bsize=%d",
    fsd.f_blocks, fsd.f_bfree, fsd.f_bavail,
    fsd.f_files, fsd.f_ffree,  fsd.f_bsize));
#define convert_blocks(b) adjust_blocks ((b), fsd.f_bsize, 512)
  fsp->fsu_blocks = convert_blocks (fsd.f_blocks);
  fsp->fsu_bfree  = convert_blocks (fsd.f_bfree);
  fsp->fsu_bavail = convert_blocks (fsd.f_bavail);
  fsp->fsu_files  = fsd.f_files;
  fsp->fsu_ffree  = fsd.f_ffree;

  return(0);
}

int nw_get_fs_usage(uint8 *volname, struct fs_usage *fsu)
/* returns 0 if OK, else errocode < 0 */
{
  int volnr = nw_get_volume_number(volname, strlen((char*)volname));
  if (volnr > -1) {
    NW_VOL *v=&(nw_volumes[volnr]);
    if (0 == (volnr=get_fs_usage((char*)v->unixname, fsu))) {
      if (v->options & VOL_OPTION_READONLY) {
        fsu->fsu_bfree  = 0;
        fsu->fsu_bavail = 0;
        fsu->fsu_ffree  = 0;
      }
    }
  }
  return(volnr);
}

int get_volume_options(int volnr)
{
  int result = 0;
  if (volnr > -1 && volnr < used_nw_volumes)
    result = nw_volumes[volnr].options;
  XDPRINTF((5,0,"get_volume_options of VOLNR:%d, result=0x%x", volnr, result));
  return(result);
}

int get_volume_inode(int volnr, struct stat *stb)
/* returns inode if OK, else errocode < 0 */
{
  int result = -0x98; /* Volume not exist */;
  if (volnr > -1 && volnr < used_nw_volumes) {
    struct stat statb;
    if (!stb) stb=&statb;
    result = stat(nw_volumes[volnr].unixname, stb);
    if (result == -1) result=-0x98;
    else result=stb->st_ino;
  }
  XDPRINTF((5,0,"get_volume_inode of VOLNR:%d, result=0x%x", volnr, result));
  return(result);
}


#if QUOTA_SUPPORT

/*  QUOTA support from: Matt Paley  */

#include <sys/stat.h>
#include <unistd.h>
#include <mntent.h>

/* Return the device special file that the specified path uses */
const char *find_device_file(const char *path)
{
  struct stat   s;
  dev_t         dev;
  struct mntent *mntent;
  FILE          *fp;
  const char    *mount_device;

  if (path == (char *) NULL || *path == '\0' || stat(path, &s) != 0)
    return((char *) NULL);
  dev = s.st_dev;
  if ((fp=setmntent(MOUNTED, "r")) == (FILE *) NULL)
    return((char *) NULL);
  mount_device = (char *) NULL;
  while (!ferror(fp)) {
    /* mntent will be a static struct mntent */
    mntent = getmntent(fp);
    if (mntent == (struct mntent *) NULL)
      break;
    if (stat(mntent->mnt_fsname, &s) == 0) {
      if (S_ISCHR(s.st_mode) || S_ISBLK(s.st_mode)) {
	if (s.st_rdev == dev) {
	  /* Found it */
	  mount_device = mntent->mnt_fsname;
	  break;
	}
      }
    }
  }
  endmntent(fp);
  return(mount_device);
}

#include <time.h>
#include <sys/types.h>

#ifdef LINUX
# include <linux/quota.h>
# if defined(__alpha__)
#  include <errno.h>
#  include <syscall.h>
#  include <asm/unistd.h>
int quotactl(int cmd, const char * special, int id, caddr_t addr)
{
  return syscall(__NR_quotactl, cmd, special, id, addr);
}
# else /* not __alpha__ */
#  define __LIBRARY__
#  include <linux/unistd.h>
_syscall4(int, quotactl, int, cmd, const char *, special,
	  int, id, caddr_t, addr);
# endif /* __alpha__ */
#endif /* LINUX */

static int su_quotactl(int cmd, const char * special, int id, caddr_t addr)
{
  int result;
  int euid=geteuid();
  seteuid(0);
  result=quotactl(cmd, special, id, addr);
  if (seteuid(euid)) {
    errorp(1, "seteuid", "cannot change to uid=%d\n", euid);
    exit(1);
  }
  return(result);
}


/* NOTE: The error numbers in here are probably wrong */
int nw_set_vol_restrictions(uint8 volnr, int uid, uint32 quota)
{
  const char   *device;
  struct dqblk dqblk;
  int          res;

  XDPRINTF((2,0, "nw_set_vol_restrictions vol=%d uid=%d quota=%d blocks",
	    volnr, uid, quota));

  /* Convert from blocks to K */
  quota *= 4;

  if (volnr >= used_nw_volumes || nw_volumes == (NW_VOL *) NULL)
    return(-0x98);
  device=find_device_file(nw_volumes[volnr].unixname);
  if (device == (char *) NULL)
    return(-0x98);

  /* If this call fails then it it probable that quotas are not enabled
   * on the specified device.  Someone needs to set the error number
   * to whatever will make most sense to netware.
   */
  res=su_quotactl(QCMD(Q_GETQUOTA, USRQUOTA), device, uid, (caddr_t) &dqblk);

  if (res != 0)
    return(0);
  dqblk.dqb_bhardlimit = quota;
  dqblk.dqb_bsoftlimit = quota;
  if (quota == 0)
    dqblk.dqb_ihardlimit = dqblk.dqb_isoftlimit = 0;
  XDPRINTF((2,0, "Set quota device=%s uid=%d %d(%d)K %d(%d) files",
	    device, uid,
	    dqblk.dqb_bhardlimit,
	    dqblk.dqb_curblocks,
	    dqblk.dqb_ihardlimit,
	    dqblk.dqb_curinodes));

  (void)su_quotactl(QCMD(Q_SETQLIM, USRQUOTA), device, uid, (caddr_t) &dqblk);



  return(0);
}

int nw_get_vol_restrictions(uint8 volnr, int uid, uint32 *quota, uint32 *inuse)
{
  const char   *device;
  struct dqblk dqblk;
  int          res;
  *quota = 0x40000000;
  *inuse = 0;
  if (volnr >= used_nw_volumes || nw_volumes == (NW_VOL *) NULL)
    return(-0x98);

  device=find_device_file(nw_volumes[volnr].unixname);
  if (device == (char *) NULL)
    return(-0x98);

  XDPRINTF((2,0, "Get quota for uid %d on device %s",
	    uid, device));

  res=su_quotactl(QCMD(Q_GETQUOTA, USRQUOTA), device, uid, (caddr_t) &dqblk);

  if (res != 0)
    return(0);  /* Quotas are probably not enabled */
  if (dqblk.dqb_bhardlimit == 0) {
    *quota = 0x40000000;
    *inuse = 0;
  } else {
    *quota = dqblk.dqb_bhardlimit / 4; /* Convert from K to blocks */
    *inuse = dqblk.dqb_curblocks / 4;
  }
  return(0);
}

#else
int nw_set_vol_restrictions(uint8 volnr, int uid, uint32 quota)
{
  return(-0xfb);
}

int nw_get_vol_restrictions(uint8 volnr, int uid, uint32 *quota, uint32 *inuse)
{
  *quota = 0x40000000;
  *inuse = 0;
  return(0);
}
#endif
