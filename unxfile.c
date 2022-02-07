/* unxfile.c:  30-Apr-98*/

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

#include "net.h"
#include <dirent.h>
#include "unxfile.h"

int unx_mvdir(uint8 *oldname, uint8 *newname)
{
  struct stat statb;
  if (!stat(newname, &statb)) return(EEXIST);
  if (stat(oldname,  &statb)) return(-1);
  else if (!S_ISDIR(statb.st_mode)) return(-1);
  return( (rename(oldname, newname) < 0) ? errno : 0);
}

int unx_mvfile(uint8 *oldname, uint8 *newname)
{
  struct stat statb;
  if (!stat(newname, &statb)) return(EEXIST);
  if (stat(oldname,  &statb)) return(-1);
  else if (S_ISDIR(statb.st_mode)) return(-1);
  return( (rename(oldname, newname) < 0) ? errno : 0);
}

int unx_mvfile_or_dir(uint8 *oldname, uint8 *newname)
{
  struct stat statb;
  if (!stat(newname, &statb)) return(EEXIST);
  if (stat(oldname,  &statb)) return(-1);
  return( (rename(oldname, newname) < 0) ? errno : 0);
}

int unx_xmkdir(char *unixname, int mode)
{
  if  (mkdir(unixname, mode)) {
    char *p=unixname;
    while (NULL != (p=strchr(p+1, '/'))) {
      *p = '\0';
      if (!mkdir(unixname, mode))
         chmod(unixname, mode);
      *p='/';
    }
    if (!mkdir(unixname, mode)) {
      chmod(unixname, mode);
      return(0);
    }
  } else {
    chmod(unixname, mode);
    return(0);
  }
  return(-1);
}

int unx_xrmdir(char *unixname)
/* removes complete directory if possible */
{
  DIR *d = opendir(unixname);
  if (NULL != d) {
    struct dirent *dirbuff;
    int    len   = strlen(unixname);
    char   *buf  = xmalloc(len + 300);
    char   *p    = buf+len;
    memcpy(buf, unixname, len);
    *p++ = '/';
    while ((dirbuff = readdir(d)) != (struct dirent*)NULL){
      if (dirbuff->d_ino && 
        (     dirbuff->d_name[0] != '.'
          || (dirbuff->d_name[1] != '\0' && 
             (dirbuff->d_name[1] != '.' || dirbuff->d_name[2] != '\0')))) {
        strmaxcpy(p, dirbuff->d_name, 298);
        if (unlink(buf) && unx_xrmdir(buf)) {
          errorp(1, "unx_xrmdir", "cannot remove '%s'", buf);
          break;
        }
      }
    }
    xfree(buf);
    closedir(d);
  }
  return(rmdir(unixname));
}

#if  0
int unx_mvdir(uint8 *oldname, uint8 *newname)
{
  uint8 command[500];
  struct stat statb;
  if (!stat(newname, &statb)) return(EEXIST);
  if (stat(oldname,  &statb)) return(-1);
  else if (!S_ISDIR(statb.st_mode)) return(-1);
  sprintf(command, "mv %s %s 2>&1 >/dev/null" , oldname, newname);
  return(system(command));
}
#endif

int unx_ftruncate(int fd, uint32 size)
{
#ifdef LINUX
  return(ftruncate(fd, size));
#else
  struct flock flockd;
  flockd.l_type   = 0;
  flockd.l_whence = SEEK_SET;
  flockd.l_start  = size;
  flockd.l_len    = 0;
  return(fcntl(fd, F_FREESP, &flockd));
#endif
}

