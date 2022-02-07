/* nwattrib.c 10-May-98 */
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

 /* Attrib routines for mars_nwe */

#include "net.h"
#include <dirent.h>
#include "unxfile.h"
#include "nwvolume.h"
#include "connect.h"
#include "trustee.h"
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

static void free_attr_from_disk(int dev, ino_t inode)
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
  return(-1);
}


uint32 get_nw_attrib_dword(int volume, char *unixname, struct stat *stb)
/* returns full attrib_dword */
{
  uint32 attrib = S_ISDIR(stb->st_mode) ? FILE_ATTR_DIR
                                        : FILE_ATTR_A;
  
  int voloptions = get_volume_options(volume);
  
  if (voloptions & VOL_OPTION_IS_PIPE) {
    attrib |= (FILE_ATTR_DELETE_INH|FILE_ATTR_RENAME_INH);
    if (!S_ISDIR(stb->st_mode)){
      attrib|=FILE_ATTR_SHARE;
    }
    return(attrib);
  }
  
  if (  (voloptions & VOL_OPTION_ATTRIBUTES) &&
       !get_attr_from_disk(stb->st_dev, stb->st_ino, &attrib)) {
  
    if (S_ISDIR(stb->st_mode)) attrib |= FILE_ATTR_DIR;
    else attrib &= (~FILE_ATTR_DIR);
  } 
  
  if (voloptions & VOL_OPTION_READONLY){
    attrib |= (FILE_ATTR_DELETE_INH|FILE_ATTR_RENAME_INH|FILE_ATTR_R);
  }

  if (!(voloptions & VOL_OPTION_ATTRIBUTES)) { 
    /* only for volumes without attribute handling */
    if (act_uid) {
      /* if not root */
      int acc=get_unix_eff_rights(stb);
      if (!(acc & W_OK)) {
        attrib |= FILE_ATTR_R;    /* RO    */
      }
      if (!(acc & R_OK)) {
        attrib |= FILE_ATTR_H;   /* We say hidden here */
      }
    }
  }
  return(attrib);
}

static int set_nw_attrib(
  int volume, char *unixname, struct stat *stb, uint32 attrib, uint32 mode)
  /* mode == 0 : set_nw_attrib_dword
             1 : set_nw_attrib_byte
             2 : set_nw_attrib_word
   */
{
  int is_dir=S_ISDIR(stb->st_mode);
  uint32 oldattrib,newattrib;
  int voloptions=get_volume_options(volume);
  if (voloptions & VOL_OPTION_IS_PIPE)
     return(0); /* we return with no error */
  if (tru_eff_rights_exists(volume, unixname, stb, TRUSTEE_M))
     return(-0x8c); /* no modify rights */
  if (!(voloptions & VOL_OPTION_ATTRIBUTES)) {
    if (!(get_unix_eff_rights(stb) & W_OK)) {
      if (!(attrib & FILE_ATTR_R)) { /* if not setting RO */
        if (!chmod(unixname, stb->st_mode | S_IWUSR)) {
          stb->st_mode |= S_IWUSR;
        } else
         return(-0x8c); /* no modify rights */
      }
    }
  }
  if (mode || !(voloptions & VOL_OPTION_ATTRIBUTES)) {
    oldattrib=get_nw_attrib_dword(volume, unixname, stb);
    if (mode==1) {  /* byte */
      newattrib  = (oldattrib & ~0xff);
      newattrib |= (attrib    &  0xff);
    } else if (mode==2) {  /* word */
      newattrib  = (oldattrib & ~0xffff);
      newattrib |= (attrib    &  0xffff);
    } else newattrib=attrib;
  } else {
    newattrib=attrib;
  }
  
  if ((!mode) || (newattrib != oldattrib) )  {
    if (!(voloptions & VOL_OPTION_ATTRIBUTES)) {
      if ((!is_dir) && ((newattrib&FILE_ATTR_R)!=(oldattrib&FILE_ATTR_R))) {
        int oldmode=stb->st_mode;
        if (newattrib & FILE_ATTR_R)   /* R/O */
          stb->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
        else
          stb->st_mode |= (S_IWUSR | S_IWGRP);
        if (chmod(unixname, stb->st_mode)) {
          stb->st_mode = oldmode;
          return(-0x8c); /* no modify rights */
        }
      }
    } else {
      if (is_dir) newattrib |=  0x10;
      else newattrib        &= ~0x10;
      put_attr_to_disk(stb->st_dev, stb->st_ino, newattrib);
    }
  }
  return(0);
}

int set_nw_attrib_dword(int volume, char *unixname, struct stat *stb, uint32 attrib)
/* wattrib = ext_attrib, attrib */
{
  return(set_nw_attrib(volume,unixname,stb,attrib, 0));
}

int set_nw_attrib_byte(int volume, char *unixname, struct stat *stb, int battrib)
{
  return(set_nw_attrib(volume,unixname,stb,(uint32)battrib, 1));
}

int set_nw_attrib_word(int volume, char *unixname, struct stat *stb, int wattrib)
/* wattrib = ext_attrib, attrib */
{
  return(set_nw_attrib(volume,unixname,stb,(uint32)wattrib, 2));
}

void set_nw_archive_bit(int volume, char *unixname, int dev, ino_t inode)
/* sets archive bit on files, is called when file is closed */
{
  uint32 attrib=0L;
  int voloptions=get_volume_options(volume);
  if (voloptions & VOL_OPTION_ATTRIBUTES) {
    if (  (!get_attr_from_disk(dev, inode, &attrib))
       && !(attrib & FILE_ATTR_A)) {
      attrib|=FILE_ATTR_A;
      put_attr_to_disk(dev, inode, attrib);
    }
  }
}

void free_nw_ext_inode(int volume, char *unixname, int dev, ino_t inode)
/* removes all attrib or trustees entries for files or dirs */
/* must be called after deleting nw file or dir             */
{
  int voloptions=get_volume_options(volume);
  if (voloptions & VOL_OPTION_ATTRIBUTES) 
    free_attr_from_disk(dev, inode);
  if (voloptions & VOL_OPTION_TRUSTEES) {
    int i=used_nw_volumes;
    while(i--) 
     tru_free_file_trustees_from_disk(i, dev, inode);
  }
}

