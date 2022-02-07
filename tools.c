/* tools.c  13-May-96 */
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

#include "net.h"
#include <stdarg.h>

#ifndef LINUX
extern int   _sys_nerr;
extern char *_sys_errlist[];
#endif


int  nw_debug=0;
FILE *logfile=stdout;

static int   in_module=0;  /* in which process i am ?   */
static int   connection=0; /* which connection (nwconn) */
static int   my_pid = -1;
static void  (*sigsegv_func)(int isig);
static char  *modnames[] =
{ "?",
  "NWSERV",
  "NCPSERV",
  "NWCONN",
  "NWCLIENT",
  "NWBIND",
  "NWROUTED" };

static char *get_modstr(void)
{
  return(modnames[in_module]);
}

char *xmalloc(uint size)
{
  char *p = (size) ? (char *)malloc(size) : (char*)NULL;
  if (p == (char *)NULL && size){
    errorp(1, "xmalloc", "not enough core, need %d Bytes\n", size);
    exit(1);
  }
  return(p);
}

char *xcmalloc(uint size)
{
  char *p = xmalloc(size);
  if (size) memset(p, 0, size);
  return(p);
}

void x_x_xfree(char **p)
{
  if (*p != (char *)NULL){
    free(*p);
    *p = (char*)NULL;
  }
}

int strmaxcpy(uint8 *dest, uint8 *source, int len)
{
  int slen = (source != (uint8 *)NULL) ? min(len, strlen((char*)source)) : 0;
  if (slen) memcpy(dest, source, slen);
  dest[slen] = '\0';
  return(slen);
}

int x_x_xnewstr(uint8 **p,  uint8 *s)
{
  int len = (s == NULL) ? 0 : strlen((char*)s);
  if (*p != (uint8 *)NULL) free((char*)*p);
  *p = (uint8*)xmalloc(len+1);
  if (len) strcpy((char*)(*p), (char*)s);
  else **p = '\0';
  return (len);
}

void dprintf(char *p, ...)
{
  va_list ap;
  if (nw_debug){
    fprintf(logfile, "%-8s:", get_modstr());
    va_start(ap, p);
    vfprintf(logfile, p, ap);
    va_end(ap);
    fprintf(logfile, "\n");
    fflush(logfile);
    fflush(logfile);
  }
}

void xdprintf(int dlevel, int mode, char *p, ...)
{
  va_list ap;
  if (nw_debug >= dlevel) {
    if (!(mode & 1)) fprintf(logfile, "%-8s %d:", get_modstr(), connection);
    if (p) {
      va_start(ap, p);
      vfprintf(logfile, p, ap);
      va_end(ap);
    }
    if (!(mode & 2)) fprintf(logfile, "\n");
    fflush(logfile);
  }
}

void errorp(int mode, char *what, char *p, ...)
{
  va_list ap;
  int errnum      = errno;
  FILE *lologfile = logfile;
  char errbuf[200];
  char *errstr    = errbuf;
  if (mode > 9) {
    errnum = -1;
    mode  -= 10;
  }
  if (errnum >= 0 && errnum < _sys_nerr) errstr = _sys_errlist[errnum];
  else if (errnum > -1)
    sprintf(errbuf, "errno=%d", errnum);
  else
    errbuf[0] = '\0';
  while (1) {
    if (mode==1) fprintf(lologfile, "\n!! %-8s %d:PANIC !!\n", get_modstr(), connection);
    fprintf(lologfile, "%-8s %d:%s:%s\n", get_modstr(), connection,  what, errstr);
    if (p) {
      va_start(ap, p);
      vfprintf(lologfile, p, ap);
      va_end(ap);
      fprintf(lologfile, "\n");
    }
    fflush(lologfile);
    if ((!mode) || (lologfile == stderr)) break;
    else lologfile = stderr;
  }
}

FILE *open_nw_ini(void)
{
  char *fname=FILENAME_NW_INI;
  FILE *f=fopen(fname, "r");
  if (f == (FILE*)NULL) fprintf(logfile, "Cannot open ini file `%s`\n", fname);
  return(f);
}

int get_ini_entry(FILE *f, int entry, uint8 *str, int strsize)
/* returns ini_entry or 0 if nothing found */
{
  char  buff[512];
  int   do_open = ((FILE*) NULL == f);
  if (do_open) f = open_nw_ini();
  if ((FILE*) NULL != f) {
    while (fgets((char*)buff, sizeof(buff), f) != NULL){
      int len   = strlen(buff);
      char *ppi = NULL;
      char *ppe = NULL;
      int  se   =  0;
      int   j   = -1;
      while (++j < len){
        char *pp=(buff+j);
        if (*pp == '#' || *pp == '\r' || *pp == '\n') {
          *pp      = '\0';
          len 	   = j;
          break;
        } else if ( *pp == 32 || *pp == '\t') {
          if (!se) se = j;
        } else {
          if ((!ppi) && se) ppi = pp;
          ppe=pp;
        }
      }
      if (len > se+1 && se > 0 && se < 4 && ppi){
        char sx[10];
        int  fentry;
        strmaxcpy((uint8*)sx, (uint8*)buff, se);
        fentry = atoi(sx);
        if (fentry > 0 && ((!entry) || entry == fentry)) {
          if (ppe) *(ppe+1) = '\0';
          strmaxcpy((uint8*)str, (uint8*)ppi, strsize-1);
          if (do_open) fclose(f);
          return(fentry);
        }
      }
    } /* while */
    if (do_open) fclose(f);
  }
  return(0);
}

char *get_div_pathes(char *buff, char *name, int what, char *p, ... )
{
  char *wpath;
  int  len;
  switch (what) {
    case  0 : wpath = PATHNAME_PROGS;    break;
    case  1 : wpath = PATHNAME_BINDERY;  break;
    case  2 : wpath = PATHNAME_PIDFILES; break;
    default : buff[0]='\0';
              return(buff);
  }
  len=sprintf(buff, (name && *name) ? "%s/%s" : "%s/", wpath, name);
  if (NULL != p) {
    va_list ap;
    va_start(ap, p);
    vsprintf(buff+len, p, ap);
    va_end(ap);
  }
  return(buff);
}

int get_ini_int(int what)
{
  uint8 buff[30];
  int  i;
  if (get_ini_entry(NULL, what, buff, sizeof(buff))
     && 1==sscanf((char*)buff, "%d", &i) ) return(i);
  return(-1);
}

void get_ini_debug(int module)
/* what:
 * 1 = nwserv
 * 2 = ncpserv
 * 3 = nwconn
 * 4 = nwclient
 * 5 = nwbind
 * 6 = nwrouted
 */
{
  int debug = get_ini_int(100+module);
  if (debug > -1) nw_debug = debug;
}

static void sig_segv(int isig)
{
  char *s= "!!!! PANIC signal SIGSEGV at pid=%d !!!!!\n" ;
  XDPRINTF((0, 0, s, my_pid));
  fprintf(stderr, "\n");
  fprintf(stderr, s, my_pid);
#if 0
  (*sigsegv_func)(isig);
#endif
}

static int fn_exist(char *fn)
{
  struct stat stb;
  return((stat(fn, &stb) == -1) ? 0 : stb.st_mode);
}

static char *get_pidfilefn(char *buf)
{
  char lbuf[100];
  strcpy(lbuf, get_modstr());
  return(get_div_pathes(buf, (char*)downstr((uint8*)lbuf), 2, ".pid"));
}

void creat_pidfile(void)
{
  char buf[300];
  char *pidfn=get_pidfilefn(buf);
  FILE *f=fopen(pidfn, "w");
  if (f != NULL) {
    fprintf(f, "%d\n", getpid());
    fclose(f);
  } else {
    XDPRINTF((1, 0, "Cannot creat pidfile=%s", pidfn));
  }
}

void init_tools(int module, int options)
{
  uint8 buf[300];
  char  logfilename[300];
  FILE *f=open_nw_ini();
  int   withlog=0;
  int   dodaemon=0;
  int   new_log=0;
  in_module  = module;
  connection = (NWCONN == module) ? options : 0;
  if (NWSERV == module || NWROUTED == module) {
    int kill_pid=-1;
    char *pidfn=get_pidfilefn((char*)buf);
    if (fn_exist(pidfn)) {
      FILE *pf=fopen(pidfn, "r");
      if ( NULL != pf) {
        if (1 != fscanf(pf, "%d", &kill_pid) || kill_pid < 1
           || kill(kill_pid, 0) < 0)
          kill_pid=-1;
        fclose(pf);
      }
      if (kill_pid < 0) unlink((char*)buf);
    }
    if (kill_pid > -1) {
      int sig;
      if (options == 1) {  /* kill -HUP prog */
        sig = SIGHUP;
      } else if (options == 2) { /* kill prog */
        sig = SIGTERM;
      } else {
        errorp(11, "INIT", "Program pid=%d already running and pidfn=%s exists" ,
               kill_pid, pidfn);
        exit(1);
      }
      if (kill_pid > 1) kill(kill_pid, sig);
      exit(0);
    } else if (options == 1 || options == 2) {
      errorp(11, "INIT", "Program not running yet" );
      exit(1);
    }
  }
  if (f) {
    int  what;
    while (0 != (what=get_ini_entry(f, 0, buf, sizeof(buf)))) { /* daemonize */
      if (200 == what) dodaemon = atoi((char*)buf);
      else if (201 == what) {
        strmaxcpy((uint8*)logfilename, (uint8*)buf, sizeof(logfilename)-1);
        withlog++;
      } else if (202 == what) {
        new_log = atoi((char*)buf);
      } else if (100+module == what) nw_debug=atoi((char*)buf);
    }
    fclose(f);
  }
  if (dodaemon) {
    if (!withlog) strcpy(logfilename, "./nw.log");
    if (NWSERV == module || NWROUTED == module) { /* now make daemon */
      int fd=fork();
      if (fd) exit((fd > 0) ? 0 : 1);
    }
    if (NULL == (logfile = fopen(logfilename,
           (new_log && (NWSERV == module || NWROUTED == module)) ? "w" : "a"))) {
      char sxx[100];
      sprintf(sxx, "\n\nOpen logfile `%s`", logfilename);
      perror(sxx);
      logfile = stdout;
      fprintf(stderr, "\n!! ABORTED !!\n");
      exit(1);
    }
    if (NWSERV == module || NWROUTED == module) setsid();
  }
  if (  NWCONN != module || nw_debug > 1 ) {
    XDPRINTF((1, 0, "Starting Version: %d.%02dpl%d",
         _VERS_H_, _VERS_L_, _VERS_P_ ));
  }
#if 1
  if (nw_debug < 8)
    sigsegv_func = signal(SIGSEGV, sig_segv);
#endif
  my_pid = getpid();
}

void exit_tools(void)
{
  if (in_module == NWSERV || in_module == NWROUTED) {
    char buf[300];
    unlink(get_pidfilefn(buf));
  }
  if (logfile != stdout) {
    if (logfile != NULL) fclose(logfile);
    logfile=stdout;
  }
}

uint8 down_char(uint8 ch)
{
  if (ch > 64 && ch < 91) return(ch + 32);
  switch(ch){
    case 142:  ch =  132; break;
    case 153:  ch =  148; break;
    case 154:  ch =  129; break;
    default :break;
  }
  return(ch);
}

uint8 up_char(uint8 ch)
{
  if (ch > 96 && ch < 123) return(ch - 32);
  switch(ch) {
    case 132:  ch =  142; break;
    case 148:  ch =  153; break;
    case 129:  ch =  154; break;
    default :  break;
  }
  return(ch);
}

uint8 *upstr(uint8 *ss)
{
  uint8 *s=ss;
  if (!s) return((uint8*)NULL);
  for (;*s;s++) *s=up_char(*s);
  return(ss);
}

uint8 *downstr(uint8 *ss)
{
  uint8 *s=ss;
  if (!s) return((uint8*)NULL);
  for (;*s;s++) *s=down_char(*s);
  return(ss);
}

