/* nwvolume.h  15-Jan-96 */
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

#define MAX_DEV_NAMESPACE_MAPS  256

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
  uint8 *sysname;       /* VOL_NAME                       */
  uint8 *unixname;      /* UNIX-Verzeichnis               */
  int   unixnamlen;     /* len of unixname		  */
  DEV_NAMESPACE_MAP *dev_namespace_maps[MAX_DEV_NAMESPACE_MAPS];
  int   maps_count;     /* count of dev_namespace_maps    */
  uint8 options;        /* *_1_* alles in Kleinbuchstaben */
} NW_VOL;

#define VOL_OPTION_DOWNSHIFT    1
#define VOL_OPTION_IS_PIPE      2  /* Volume has only pipes */


/* stolen from GNU-fileutils */
/* Space usage statistics for a filesystem.  Blocks are 512-byte. */
struct fs_usage {
  long fsu_blocks;		/* Total blocks. */
  long fsu_bfree;		/* Free blocks available to superuser. */
  long fsu_bavail;		/* Free blocks available to non-superuser. */
  long fsu_files;		/* Total file nodes. */
  long fsu_ffree;		/* Free file nodes. */
};

extern NW_VOL    nw_volumes[MAX_NW_VOLS];
extern int       used_nw_volumes;

extern void nw_init_volumes(FILE *f);
extern int  nw_get_volume_number(uint8 *volname, int namelen);
extern int  nw_get_volume_name(int volnr, uint8 *volname);
extern int  nw_get_fs_usage(char *volname, struct fs_usage *fsu);
extern int  get_volume_options(int volnr, int mode);

extern uint32 nw_vol_inode_to_handle(int volume, ino_t inode,
                               DEV_NAMESPACE_MAP *dnm);

extern ino_t nw_vol_handle_to_inode(int volume, uint32 handle,
                               DEV_NAMESPACE_MAP *dnm);
