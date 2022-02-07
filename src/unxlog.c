/* unxlog.c : 11-Jul-98 */
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
#include "unxlog.h"

void write_utmp(int dologin, int connection, int pid,
                ipxAddr_t *from_addr, uint8  *username)
{
#ifndef FREEBSD
  struct utmp loc_ut;
  struct utmp *ut;
  int    fd;
  char   buff[200];
  int    found = 0;
  char   *fn_utmp=FILENAME_UTMP;
  char   *fn_wtmp=FILENAME_WTMP;
  if (NULL == fn_utmp) return;

  utmpname(fn_utmp);
  setutent();
  sprintf(buff, "NCP%03d", connection);
  while (NULL != (ut = getutent())) {
    if (pid == ut->ut_pid ||
       !strncmp(buff, ut->ut_line, sizeof(ut->ut_line))) {
      found++;
      break;
    }
  }

  if (!found) {
    if (!dologin) {
      endutent();
      return;
    } else {
      ut=&loc_ut;
      memset(ut, 0, sizeof(struct utmp));
    }
  }
  ut->ut_type = dologin ? USER_PROCESS : DEAD_PROCESS;

  if (dologin) {
    strncpy(ut->ut_line, buff, sizeof(ut->ut_line));
    ut->ut_pid = pid;
    sprintf(buff, "%d", connection);
    strncpy(ut->ut_id, buff, sizeof(ut->ut_id));
    if (username) strncpy((char*)ut->ut_user, (char*)username, sizeof(ut->ut_user));
#ifdef LINUX
    ut->ut_addr = (long) GET_BE32(from_addr->net);
    strncpy(ut->ut_host, xvisable_ipx_adr(from_addr, 2), sizeof(ut->ut_host));
#endif
  } else {
    memset(ut->ut_user, 0, sizeof(ut->ut_user));
    ut->ut_pid = 0;
  }
  (void)time(&(ut->ut_time));
  pututline(ut);
  endutent();
  if (NULL == fn_wtmp) return;
  if ((fd = open(fn_wtmp, O_APPEND|O_WRONLY)) > -1) {
    write(fd, (char *)ut, sizeof(struct utmp));
    close(fd);
  }
#endif /* !FREEBSD */
}
