/* unxfile.c:  09-Jul-97*/

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

