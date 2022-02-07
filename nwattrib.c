/* nwattrib.c 01-Feb-98 */
/* (C)opyright (C) 1998  Martin Stover, Marburg, Germany
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
#include "unxfile.h"
#include "nwvolume.h"
#include "connect.h"
#include "nwattrib.h"

static void put_attr_to_disk(int dev, ino_t inode, uint32 attrib)
{
  char   buf[255];
  char   battrib[255];
  int    l;
  uint8  buf_uc[4];
  U32_TO_BE32(inode, buf_uc);
  l=sprintf(buf, "%s/%x/%x/%x/%x", path_attributes, 
            dev,
            (int) buf_uc[0],
            (int) buf_uc[1],
            (int) buf_uc[2]);
  seteuid(0);
  unx_xmkdir(buf, 0755);
  sprintf(buf+l, "/%x", (int) buf_uc[3]);
  unlink(buf);
  l=sprintf(battrib, "%08x", (unsigned int) attrib); 
  symlink(battrib, buf);
  reseteuid();
}

void free_attr_from_disk(int dev, ino_t inode)
{
  char   buf[255];
  uint8  buf_uc[4];
  U32_TO_BE32(inode, buf_uc);
  sprintf(buf, "%s/%x/%x/%x/%x/%x", path_attributes, 
            dev,
            (int) buf_uc[0],
            (int) buf_uc[1],
            (int) buf_uc[2],
            (int) buf_uc[3]);
  seteuid(0);
  unlink(buf);
  reseteuid();
}

static int get_attr_from_disk(int dev, ino_t inode, uint32 *attrib)
/* returns 0 if all ok */
{
  char   buf[255];
  char   battrib[255];
  int    l;
  uint8  buf_uc[4];
  U32_TO_BE32(inode, buf_uc);
  sprintf(buf, "%s/%x/%x/%x/%x/%x", path_attributes,
            dev,
            (int) buf_uc[0],
            (int) buf_uc[1],
            (int) buf_uc[2],
            (int) buf_uc[3]);
  seteuid(0);
  l=readlink(buf, battrib, 224);
  reseteuid();
  if (l > 0) { 
    unsigned int uattrib=0;
    battrib[l]='\0';
    if (1 == sscanf(battrib, "%x", &uattrib)) {
      *attrib = uattrib;
      return(0);
    }
  } 
  *attrib=0;
  return(-1);
}


uint32 get_nw_attrib_dword(struct stat *stb, int voloptions)
/* returns full attrib_dword */
{
  uint32 attrib=0;
  int is_dir=S_ISDIR(stb->st_mode);
  
  if (!is_dir && (voloptions & VOL_OPTION_IS_PIPE)) 
    return(FILE_ATTR_SHARE|FILE_ATTR_A);
  
  if (voloptions & VOL_OPTION_READONLY)
    return((is_dir)?FILE_ATTR_DIR|FILE_ATTR_R:FILE_ATTR_R);

  if (!get_attr_from_disk(stb->st_dev, stb->st_ino, &attrib)) {
    if (is_dir) attrib |= FILE_ATTR_DIR;
    else attrib &= (~FILE_ATTR_DIR);
  } else { 
    if (is_dir) attrib |= FILE_ATTR_DIR;
    else attrib &= (~FILE_ATTR_DIR);
  }

  if (act_uid) {
    /* if not root */
    int acc=get_real_access(stb);
    if (!(acc & W_OK)) {
      attrib |= FILE_ATTR_R;    /* RO    */
    }
    if (!(acc & R_OK)) {
      attrib |= FILE_ATTR_H;   /* We say hidden here */
    }
  }
  return(attrib);
}

int set_nw_attrib_dword(struct stat *stb, int voloptions, uint32 attrib)
{
  int is_dir=S_ISDIR(stb->st_mode);
  if (voloptions & VOL_OPTION_READONLY) 
     return(-0x8c); /* no modify rights */
  if (voloptions & VOL_OPTION_IS_PIPE) 
     return(0); 
  
  if (!(get_real_access(stb) & W_OK))
     return(-0x8c); /* no modify rights */

  if (is_dir) attrib |= 0x10;
  else attrib &= ~0x10;
  put_attr_to_disk(stb->st_dev, stb->st_ino, attrib);
  return(0);
}

int set_nw_attrib_byte(struct stat *stb, int voloptions, int battrib)
{
  int is_dir=S_ISDIR(stb->st_mode);
  uint32 attrib,oldattrib;
  if (voloptions & VOL_OPTION_READONLY) 
     return(-0x8c); /* no modify rights */
  if (voloptions & VOL_OPTION_IS_PIPE) 
     return(0); 
  if (!(get_real_access(stb) & W_OK))
     return(-0x8c); /* no modify rights */
  attrib=oldattrib=get_nw_attrib_dword(stb, voloptions) & ~0xff;
  attrib |= battrib;
  if (is_dir) attrib |= 0x10;
  else attrib &= ~0x10;
  if (attrib != oldattrib)
    put_attr_to_disk(stb->st_dev, stb->st_ino, attrib);
  return(0);
}

