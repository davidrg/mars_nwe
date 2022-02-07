/* nwvolume.c  08-Aug-96 */
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
#include <sys/vfs.h>

#ifndef LINUX
#include <sys/statvfs.h>
#define statfs statvfs
#endif

#include <utime.h>

#include "nwvolume.h"

NW_VOL    nw_volumes[MAX_NW_VOLS];
int       used_nw_volumes=0;

void nw_init_volumes(FILE *f)
/* f = inifile Pointer, must be opened !! */
{
  static int volumes_is_init=0;
  int    what;
  uint8  buff[256];
  int    k = -1;
  if (!volumes_is_init) {
    volumes_is_init++;
    while (++k < MAX_NW_VOLS) memset(&(nw_volumes[k]), 0, sizeof(NW_VOL));
  } else {
    while (++k < MAX_NW_VOLS) {
      int i = -1;
      while (++i < nw_volumes[k].maps_count)
        xfree(nw_volumes[k].dev_namespace_maps[i]);
      nw_volumes[k].maps_count = 0;
    }
  }
  rewind(f);
  used_nw_volumes = 0;
  while (0 != (what = get_ini_entry(f, 0, buff, sizeof(buff)))) {
    if ( what == 1 && used_nw_volumes < MAX_NW_VOLS && strlen((char*)buff) > 3){
      uint8 sysname[256];
      uint8 unixname[256];
      char  optionstr[256];
      char  *p;
      int   len;
      int   founds = sscanf((char*)buff, "%s %s %s",sysname, unixname, optionstr);
      if (founds > 1) {
        NW_VOL *vol=&(nw_volumes[used_nw_volumes]);
        vol->options    = VOL_NAMESPACE_DOS;
        upstr(sysname);
        new_str(vol->sysname, sysname);
        if (1 == (len = strlen((char*)unixname)) && unixname[0] == '~') {
          vol->options  |= VOL_OPTION_IS_HOME;
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
                         break;

              case 'N' : vol->options
                         |= VOL_NAMESPACE_NFS;
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

      }
    }
  } /* while */
}

void nw_setup_home_vol(int len, uint8 *fn)
{
  int k=used_nw_volumes;
  uint8 unixname[258];
  unixname[0] = '\0';
  if (len > 0) {
    strmaxcpy(unixname, fn, len);
    if (unixname[len-1] != '/') {
      unixname[len++] = '/';
      unixname[len]   = '\0';
    }
  }
  while (k--) {
    if (nw_volumes[k].options & VOL_OPTION_IS_HOME)  {
      nw_volumes[k].unixnamlen = len;
      new_str(nw_volumes[k].unixname, unixname);
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
  upstr(vname);
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
    if (volnr < MAX_NW_VOLS) result=0;
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


/* next is stolen from GNU-fileutils */
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

int get_volume_options(int volnr, int mode)
/* returns >= 0 (options) if OK, else errocode < 0 */
/* if mode > 0 and errcode then errorcode = 0  (nooptions) */
{
  int result = (mode) ? 0 : -0x98; /* Volume not exist */;
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

