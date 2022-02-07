/* nwvolume.h  17-Jun-97 */
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
#ifndef _NWVOLUME_H_
#define _NWVOLUME_H_

#define MAX_DEV_NAMESPACE_MAPS  16

typedef struct {
  int dev;
  int namespace;
} DEV_NAMESPACE_MAP;

/*
 * This is inserted for namespace calls, to can map an
 * volume->dev->inode->namespace to an uint32 basehandle
 * without loosing too many places.
 */

typedef struct {
  uint8  *sysname;       /* VOL_NAME                          */
  uint8  *unixname;      /* UNIX-DIR  with ending '/'         */
  int    unixnamlen;     /* len of unixname		      */
  DEV_NAMESPACE_MAP *dev_namespace_maps[MAX_DEV_NAMESPACE_MAPS];
  int    max_maps_count; /* may be less than MAX_DEV_NAMESPACE_MAPS */
  int    maps_count;     /* count of dev_namespace_maps       */
  uint32 high_inode;     /* hight inode to can handle correct */
  int    options;        /* see defines below                 */
  uint8  *os2buf;        /* special stuff for os2 namspace    */
} NW_VOL;

/* vol options */
#define VOL_OPTION_DOWNSHIFT 0x0001  /* downshift                   */
#define VOL_OPTION_IS_PIPE   0x0002  /* Volume contains pipes       */
#define VOL_OPTION_REMOUNT   0x0004  /* Volume can be remounted (cdroms) */
#define VOL_OPTION_IS_HOME   0x0008  /* Volume is USERS HOME        */
#define VOL_OPTION_ONE_DEV   0x0010  /* Volume has only one filesys */
#define VOL_OPTION_READONLY  0x0020  /* Volume is readonly          */
#define VOL_OPTION_IGNCASE   0x0040  /* Do ignore up/downshift      */

/* namespaces */
#define VOL_NAMESPACE_DOS    0x1000
#define VOL_NAMESPACE_OS2    0x2000
#define VOL_NAMESPACE_NFS    0x4000

/* stolen from GNU-fileutils */
/* Space usage statistics for a filesystem.  Blocks are 512-byte. */
struct fs_usage {
  long fsu_blocks;		/* Total blocks. */
  long fsu_bfree;		/* Free blocks available to superuser. */
  long fsu_bavail;		/* Free blocks available to non-superuser. */
  long fsu_files;		/* Total file nodes. */
  long fsu_ffree;		/* Free file nodes. */
};

extern NW_VOL    *nw_volumes;
extern int       used_nw_volumes;
extern uint8     *home_dir;
extern int       home_dir_len;

extern void nw_init_volumes(FILE *f);
extern void nw_setup_home_vol(int len, uint8 *fn);
extern int  nw_get_volume_number(uint8 *volname, int namelen);
extern int  nw_get_volume_name(int volnr, uint8 *volname);
extern int  nw_get_fs_usage(uint8 *volname, struct fs_usage *fsu);
extern int  get_volume_options(int volnr, int mode);
extern int  get_volume_inode(int volnr, struct stat *stb);
extern int  nw_set_vol_restrictions(uint8 volnr, int uid, uint32 quota);
extern int  nw_get_vol_restrictions(uint8 volnr, int uid, uint32 *quota, uint32 *inuse);

extern uint32 nw_vol_inode_to_handle(int volume, ino_t inode,
                               DEV_NAMESPACE_MAP *dnm);

extern ino_t nw_vol_handle_to_inode(int volume, uint32 handle,
                               DEV_NAMESPACE_MAP *dnm);
#endif
