/* nwattrib.c 14-Feb-98 */
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
#include <dirent.h>
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
  *attrib=0;
  return(-1);
}


uint32 get_nw_attrib_dword(int volume, char *unixname, struct stat *stb)
/* returns full attrib_dword */
{
  uint32 attrib=0;
  int is_dir=S_ISDIR(stb->st_mode);
  int voloptions=get_volume_options(volume);

  if (!is_dir && (voloptions & VOL_OPTION_IS_PIPE))
    return(FILE_ATTR_SHARE|FILE_ATTR_A);

  if (voloptions & VOL_OPTION_READONLY)
    return((is_dir)?FILE_ATTR_DIR|FILE_ATTR_R:FILE_ATTR_R);

  if ( !(voloptions & VOL_OPTION_NO_INODES) &&
       !get_attr_from_disk(stb->st_dev, stb->st_ino, &attrib)) {
    if (is_dir) attrib |= FILE_ATTR_DIR;
    else attrib &= (~FILE_ATTR_DIR);
  } else {
    if (is_dir)
      attrib = FILE_ATTR_DIR;
    else
      attrib = FILE_ATTR_A;   /* default archive flag */
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

int set_nw_attrib_dword(int volume, char *unixname, struct stat *stb, uint32 attrib)
{
  int is_dir=S_ISDIR(stb->st_mode);
  int voloptions=get_volume_options(volume);
  if (voloptions & VOL_OPTION_READONLY)
     return(-0x8c); /* no modify rights */
  if (voloptions & VOL_OPTION_IS_PIPE)
     return(0);

  if (!(get_real_access(stb) & W_OK)) {
    if (!(attrib & FILE_ATTR_R)) { /* if not setting RO */
      if (!chmod(unixname, stb->st_mode | S_IWUSR)) {
        stb->st_mode |= S_IWUSR;
      } else
       return(-0x8c); /* no modify rights */
    }
  }

  if (voloptions & VOL_OPTION_NO_INODES) {
    int oldmode=stb->st_mode;
    if ((!is_dir) && (attrib & FILE_ATTR_R))   /* R/O */
      /* we do not set directories to readonly */
      stb->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
    else
      stb->st_mode |= (S_IWUSR | S_IWGRP);
    if (chmod(unixname, stb->st_mode)) {
      stb->st_mode = oldmode;
      return(-0x8c); /* no modify rights */
    }
  } else {
    if (is_dir) attrib |= 0x10;
    else attrib &= ~0x10;
    put_attr_to_disk(stb->st_dev, stb->st_ino, attrib);
  }
  return(0);
}

int set_nw_attrib_byte(int volume, char *unixname, struct stat *stb, int battrib)
{
  int is_dir=S_ISDIR(stb->st_mode);
  uint32 attrib,oldattrib;
  int voloptions=get_volume_options(volume);

  if (voloptions & VOL_OPTION_READONLY)
     return(-0x8c); /* no modify rights */
  if (voloptions & VOL_OPTION_IS_PIPE)
     return(0);

  if (!(get_real_access(stb) & W_OK)) {
    if (!(battrib & FILE_ATTR_R)) { /* if not setting RO */
      if (!chmod(unixname, stb->st_mode | S_IWUSR)) {
        stb->st_mode |= S_IWUSR;
      } else
       return(-0x8c); /* no modify rights */
    }
  }
  oldattrib=get_nw_attrib_dword(volume, unixname, stb);
  attrib  = (oldattrib & ~0xff);
  attrib |= battrib;

  if (attrib != oldattrib)  {
    if (voloptions & VOL_OPTION_NO_INODES) {
      int oldmode=stb->st_mode;
      if ((!is_dir) && (attrib & FILE_ATTR_R))   /* R/O */
        /* we do not set directories to readonly */
        stb->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
      else
        stb->st_mode |= (S_IWUSR | S_IWGRP);
      if (chmod(unixname, stb->st_mode)) {
        stb->st_mode = oldmode;
        return(-0x8c); /* no modify rights */
      }
    } else {
      if (is_dir) attrib |= 0x10;
      else attrib &= ~0x10;
      put_attr_to_disk(stb->st_dev, stb->st_ino, attrib);
    }
  }
  return(0);
}

void set_nw_archive_bit(int volume, char *unixname, int dev, ino_t inode)
/* sets archive bit on files, called when file is closed */
{
  uint32 attrib;
  int voloptions=get_volume_options(volume);
  if (voloptions & VOL_OPTION_NO_INODES) return;
  if (  (!get_attr_from_disk(dev, inode, &attrib))
      && !(attrib & FILE_ATTR_A)) {
    attrib|=FILE_ATTR_A;
    put_attr_to_disk(dev, inode, attrib);
  }
}


/* trustees */
static void put_trustee_to_disk(int dev, ino_t inode, uint32 id, int trustee)
{
  char   buf[255];
  char   btrustee[255];
  int    l;
  uint8  buf_uc[4];
  U32_TO_BE32(inode, buf_uc);
  l=sprintf(buf, "%s/%x/%x/%x/%x/%x.t", path_attributes,
            dev,
            (int) buf_uc[0],
            (int) buf_uc[1],
            (int) buf_uc[2],
            (int) buf_uc[3]);
  seteuid(0);
  unx_xmkdir(buf, 0755);
  sprintf(buf+l, "/%x", (unsigned int) id);
  unlink(buf);
  l=sprintf(btrustee, "%04x", (unsigned int) trustee);
  symlink(btrustee, buf);
  reseteuid();
}

static void free_trustees_from_disk(int dev, ino_t inode)
{
  char   buf[255];
  uint8  buf_uc[4];
  U32_TO_BE32(inode, buf_uc);
  sprintf(buf, "%s/%x/%x/%x/%x/%x.t", path_attributes,
            dev,
            (int) buf_uc[0],
            (int) buf_uc[1],
            (int) buf_uc[2],
            (int) buf_uc[3]);
  seteuid(0);
  unx_xrmdir(buf);
  reseteuid();
}

static int get_trustee_from_disk(int dev, ino_t inode, uint32 id)
{
  char   buf[255];
  char   btrustee[255];
  int    l;
  uint8  buf_uc[4];
  U32_TO_BE32(inode, buf_uc);
  sprintf(buf, "%s/%x/%x/%x/%x/%x.t/%x", path_attributes,
            dev,
            (int) buf_uc[0],
            (int) buf_uc[1],
            (int) buf_uc[2],
            (int) buf_uc[3],
            (unsigned int) id);
  seteuid(0);
  l=readlink(buf, btrustee, 254);
  reseteuid();
  if (l > 0) {
    unsigned int utrustee=0;
    btrustee[l]='\0';
    if (1 == sscanf(btrustee, "%x", &utrustee)) {
      return((int)utrustee);
    }
  }
  return(-1);
}

static int scan_trustees_from_disk(int dev, ino_t inode, int *offset,
       int max_trustees, uint32 *trustee_ids, int *trustees)
/* returns count of trustees if all ok */
{
  char   buf[255];
  int    l;
  int    count=0;
  uint8  buf_uc[4];
  DIR    *d;
  U32_TO_BE32(inode, buf_uc);
  l=sprintf(buf, "%s/%x/%x/%x/%x/%x.t", path_attributes,
            dev,
            (int) buf_uc[0],
            (int) buf_uc[1],
            (int) buf_uc[2],
            (int) buf_uc[3]);
  seteuid(0);
  d = opendir(buf);
  if (NULL != d) {
    uint8 *p=buf+l;
    struct dirent *dirbuff;
    *p++ = '/';
    l=0;
    if ((unsigned int)(*offset) >= MAX_U16)
      *offset=0;
    while (count < max_trustees &&
          (dirbuff = readdir(d)) != (struct dirent*)NULL){
      if (l++ > *offset) {
        if (dirbuff->d_ino) {
          char         btrustee[255];
          int          len;
          unsigned int id;
          if (1 == sscanf(dirbuff->d_name, "%x", &id) && id > 0) {
            strcpy(p, dirbuff->d_name);
            len=readlink(buf, btrustee, 254);
            if (len > 0) {
              unsigned int utrustee=0;
              btrustee[len]='\0';
              if (1 == sscanf(btrustee, "%x", &utrustee)) {
                *trustee_ids++ = (uint32) id;
                *trustees++    = (int) utrustee;
                count++;
              }
            }
          }
        }
      }
    }
    *offset=l;
    closedir(d);
  }
  reseteuid();
  return(count);
}

int get_own_trustee(int dev, ino_t inode, int look_for_trustee)
{
  if (act_obj_id == 1){ /* supervisor */
    if ( (look_for_trustee == TRUSTEE_S) || (look_for_trustee == TRUSTEE_M)
      || (look_for_trustee == (TRUSTEE_S | TRUSTEE_M)) )
       return(look_for_trustee | TRUSTEE_S);
  }
  return(0);
}

int set_nw_trustee(int dev, ino_t inode, uint32 id, int trustee)
{
  return(0);

#if 0
  if (get_own_trustee(dev, inode, TRUSTEE_M) & (TRUSTEE_M | TRUSTEE_S)){

  } else return(-0x8c); /* no modify privileges */
#endif
}


void free_nw_ext_inode(int volume, char *unixname, int dev, ino_t inode)
/* removes all attrib or trustees entries for files or dirs */
/* must be called after deleting nw file or dir             */
{
  int voloptions=get_volume_options(volume);
  if (voloptions & VOL_OPTION_NO_INODES) return;
  free_attr_from_disk(dev, inode);
  free_trustees_from_disk(dev, inode);
}
