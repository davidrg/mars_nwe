/* extpipe.c 03-Aug-98       */
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
#include "extpipe.h"

static char **build_argv(char *buf, int bufsize,  char *command, int flags)
/* routine returns **argv for use with execv routines */
/* buf will contain the path component 	     	      */
{
  int len        = strlen(command);
  int offset     = ((len+4) / 4) * 4; /* aligned offset for **argv */
  int components = (bufsize - offset) / 4;
  if (components > 1) {  /* minimal argv[0] + NULL */
    char **argv  = (char **)(buf+offset);
    char **pp    = argv;
    char  *p     = buf;
    char  c;
    int   i=0;
    --components;
    memcpy(buf, command, len);
    memset(buf+len, 0, bufsize - len);
    *pp    = p;
    while ((0 != (c = *p++)) && i < components) {
      if (c == 32 || c == '\t') {
        *(p-1) = '\0';
        if (*p != 32 && *p != '\t') {
          *(++pp)=p;
          i++;
        }
      } else if (!i && (flags&1) && c == '/') {  /* here i must get argv[0] */
        *pp=p;
      }
    }
    XDPRINTF((5, 0, "build_argv,   path='%s'",  buf));
    pp=argv;
    while (*pp) {
      XDPRINTF((5, 0, "build_argv, argv='%s'", *pp));
      pp++;
    }
    return(argv);
  }
  return(NULL);
}


static void close_piped(int piped[3][2])
{
  int j=3;
  while (j--) {
    int k=2;
    while (k--) {
      if (piped[j][k] > -1){
	close(piped[j][k]);
	piped[j][k] = -1;
      }
    }
  }
}

static int x_popen(char *command, int uid, int gid, FILE_PIPE *fp, int flags)
{
  int piped[3][2];
  int lpid=-1;
  int j=3;
  char buf[300];
  char **argv=build_argv(buf, sizeof(buf), command, flags);
  if (argv == NULL) return(-1);
  while (j--){
    int k=2;
    while(k--) piped[j][k] = -1;
  }
  if (! (pipe(&piped[0][0]) > -1 && pipe(&piped[1][0]) > -1
       && pipe(&piped[2][0]) > -1 && (lpid=fork()) > -1)) {
    close_piped(piped);
    return(-1);
  }
  if (lpid == 0) { /* Child */
    signal(SIGTERM,  SIG_DFL);
    signal(SIGQUIT,  SIG_DFL);
    signal(SIGINT,   SIG_DFL);
    signal(SIGPIPE,  SIG_DFL);
    signal(SIGHUP,   SIG_DFL);
    j=3;
    while(j--) close(j);
    j=3;
    while(j--) {
      int x  = (j) ? 0 : 1;
      int x_ = (j) ? 1 : 0;
      close(piped[j][x]    );
      dup2( piped[j][x_], j);
      close(piped[j][x_]   );
    }
    if (uid > -1 || gid > -1) {
      seteuid(0);
      if (gid > -1) setgid(gid);
      if (uid > -1) setuid(uid);
      if (gid > -1) setegid(gid);
      if (uid > -1) seteuid(uid);
    }
    if (flags&1)
      execvp(buf, argv);
    else
      execv(buf, argv);
    exit(1);    /* Never reached I hope */
  }
  j=-1;
  while (++j < 3) {
    int x  = (j) ? 0 : 1;
    int x_ = (j) ? 1 : 0;
    close(piped[j][x_]);
    piped      [j][x_]  = -1;
    fp->fds[j] = piped[j][x];
  }
  return(lpid);
}

int ext_pclose(FILE_PIPE *fp)
{
  int status=-1;
  void (*intsave) (int) = signal(SIGINT,  SIG_IGN);
  void (*quitsave)(int) = signal(SIGQUIT, SIG_IGN);
  void (*hupsave) (int) = signal(SIGHUP,  SIG_IGN);
  int j = 3;
  int tries=5;
  while (j--) close(fp->fds[j]);
  while (fp->command_pid != waitpid(fp->command_pid, &status, WNOHANG) 
       && tries>0) {
    --tries;
    XDPRINTF((10,0, "ext_pclose, tries=%d", tries));
    if (tries==2 || tries==1) 
      kill(fp->command_pid, SIGTERM);
    else if (!tries)
      kill(fp->command_pid, SIGKILL);
    sleep(1);
  }
  signal(SIGINT,   intsave);
  signal(SIGQUIT,  quitsave);
  signal(SIGHUP,   hupsave);
  xfree(fp);
  return(status);
}

FILE_PIPE *ext_popen(char *command, int uid, int gid, int flags)
/* flags & 1 use path version of exec */
{
  FILE_PIPE *fp=(FILE_PIPE*) xcmalloc(sizeof(FILE_PIPE));
  void (*intsave) (int) = signal(SIGINT,  SIG_IGN);
  void (*quitsave)(int) = signal(SIGQUIT, SIG_IGN);
  void (*hupsave) (int) = signal(SIGHUP,  SIG_IGN);
  if ((fp->command_pid  = x_popen(command, uid, gid, fp, flags)) < 0) {
    xfree(fp);
    fp=NULL;
    XDPRINTF((1, 0x10, "ext_popen failed:uid=%d, gid=%d,command='%s'", 
        uid, gid, command));
  }
  signal(SIGINT,   intsave);
  signal(SIGQUIT,  quitsave);
  signal(SIGHUP,   hupsave);
  return(fp);
}
