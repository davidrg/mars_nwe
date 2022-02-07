/* unxfile.h:  23-Jul-98 */
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
#ifndef _UNXFILE_H_
#define _UNXFILE_H_ 1
extern int unx_mvdir(uint8  *oldname, uint8 *newname);
extern int unx_mvfile(uint8 *oldname, uint8 *newname);
extern int unx_mvfile_or_dir(uint8 *oldname, uint8 *newname);
extern int unx_xmkdir(char *unixname, int mode);
extern int unx_add_x_rights(char *unixname, int mode);
extern int unx_xrmdir(char *unixname);

extern int unx_ftruncate(int fd, uint32 size);

#ifdef FREEBSD
# define SEEKDIR(dh,to)        {\
  rewinddir(dh);\
  while(TELLDIR(dh)!=(to)) \
    if(readdir(dh)==NULL) break;\
  }
# define TELLDIR(dh)   ((dh)->dd_loc+(dh)->dd_seek)
#else
# define SEEKDIR(dh,to)	seekdir((dh),(to))
# define TELLDIR(dh)	telldir((dh))
#endif

#endif

