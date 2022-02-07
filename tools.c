/* tools.c  08-Jun-97 */
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
#include <syslog.h>

#ifndef LINUX
extern int   _sys_nerr;
extern char *_sys_errlist[];
#endif


int    nw_debug=0;
uint32 debug_mask=0; /* special debug masks */

static FILE *logfile=stderr;
static int   use_syslog=0; /* 1 = use syslog for all loggings
                            * 2 = only for errors
                            */

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
  if (size)
    memset(p, 0, size);
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
/* dest must be 1 byte larger than len */
{
  int slen = (source != (uint8 *)NULL) ? min(len, strlen((char*)source)) : 0;
  if (slen)
    memcpy(dest, source, slen);
  dest[slen] = '\0';
  return(slen);
}

int x_x_xnewstr(uint8 **p,  uint8 *s)
{
  int len = (s == NULL) ? 0 : strlen((char*)s);
  if (*p != (uint8 *)NULL)
    free((char*)*p);
  *p = (uint8*)xmalloc(len+1);
  if (len)
    strcpy((char*)(*p), (char*)s);
  else
    **p = '\0';
  return (len);
}

void xdprintf(int dlevel, int mode, char *p, ...)
/* mode flags
 * 0x01 : no 'begin line'
 * 0x02 : no new line (endline)
 * 0x10 : add errno print.
 */
{
  va_list ap;
static char *buffered=NULL;
  int errnum      = errno;
  if (nw_debug >= dlevel) {
    if (use_syslog==1) {
      char *buf;
      char *pb;
      if (buffered) {
        buf=buffered;
        pb=buf+strlen(buffered);
        buffered=NULL;
      } else {
        pb=buf=xmalloc(2048);
      }
      if (p) {
        int l;
        va_start(ap, p);
        l=vsprintf(pb, p, ap);
        va_end(ap);
        pb+=l;
      }
      if (mode & 0x10) {
        int l=sprintf(pb, ", errno=%d", errnum);
        pb+=l;
        if (errnum > 0 && errnum < _sys_nerr)
          l=sprintf(pb, " (%s)",  _sys_errlist[errnum]);
      }
      if (!(mode & 2)) {
        char identstr[200];
        sprintf(identstr, "%-8s %d", get_modstr(), connection);
        openlog(identstr, LOG_CONS, LOG_DAEMON);
        syslog(LOG_DEBUG, buf);
        closelog();
      } else {
        int l=strlen(buf);
        buffered=xmalloc(l+2048);
        memcpy(buffered, buf, l+1);
      }
      xfree(buf);
    } else {
      if (!(mode & 1))
        fprintf(logfile, "%-8s %d:", get_modstr(), connection);
      if (p) {
        va_start(ap, p);
        vfprintf(logfile, p, ap);
        va_end(ap);
      }
      if (mode & 0x10) {
        fprintf(logfile, ", errno=%d", errnum);
        if (errnum > 0 && errnum < _sys_nerr)
          fprintf(logfile, " (%s)",  _sys_errlist[errnum]);
      }
      if (!(mode & 2))
        fprintf(logfile, "\n");
      fflush(logfile);
    }
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

  if (use_syslog) {
    int prio=(mode) ? LOG_CRIT : LOG_ERR;
    char identstr[200];
    char buf[2048];
    int l=sprintf(buf, "%s:%s ", what, errstr);
    if (p) {
      va_start(ap, p);
      vsprintf(buf+l, p, ap);
      va_end(ap);
    }
    sprintf(identstr, "%-8s %d", get_modstr(), connection);
    openlog(identstr, LOG_CONS, LOG_DAEMON);
    syslog(prio, buf);
    closelog();
    if (!mode) return;
    lologfile=stderr;
  }
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
  int uid=geteuid();
  if (f == (FILE*)NULL && uid > 0) {
    seteuid(0);
    f=fopen(fname, "r");
    if (seteuid(uid)) {
      errorp(1, "seteuid", "uid=%d", uid);
      exit(1);
    }
  }
  if (f == (FILE*)NULL)
    fprintf(logfile, "Cannot open ini file `%s`\n", fname);
  return(f);
}

int get_ini_entry(FILE *f, int entry, uint8 *str, int strsize)
/* returns ini_entry or 0 if nothing found */
{
  char  buff[512];
  int   do_open = ((FILE*) NULL == f);
  if (do_open) f = open_nw_ini();
  if ((FILE*) NULL != f) {
    while (fgets(buff, sizeof(buff), f) != NULL){
      int len       = strlen(buff);
      char *ppi     = NULL;
      char *ppe     = NULL;
      char *p_buff  = buff;
      int  se       =  0;
      int   j       = -1;
      char *pp;

      while (len && (*p_buff == '\t' || *p_buff == 32)) {
        --len;
        p_buff++;
      }
      pp  = p_buff;

      while (++j < len){
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
        pp++;
      }

      if (len > se+1 && se > 0 && se < 4 && ppi){
        char sx[10];
        int  fentry;
        strmaxcpy((uint8*)sx, (uint8*)p_buff, se);
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

static uint8 *path_bindery=NULL;
static uint8 *path_spool=NULL;

char *get_div_pathes(char *buff, char *name, int what, char *p, ... )
{
  char *wpath;
  int  len;
  uint8 locbuf[200];
  switch (what) {
    case  0 : wpath = PATHNAME_PROGS;    break;
    case  1 : if (path_bindery==NULL) {
                if (get_ini_entry(NULL, 45, locbuf, sizeof(locbuf))
                      && *locbuf) {
                  new_str(path_bindery, locbuf);
                } else
                  new_str(path_bindery, PATHNAME_BINDERY);
              }
              wpath = path_bindery;
              break;
    case  2 : wpath = PATHNAME_PIDFILES; break;

    case  3 :
    case  4 : if (path_spool==NULL) {
                if (get_ini_entry(NULL, 42, locbuf, sizeof(locbuf))
                      && *locbuf) {
                  new_str(path_spool, locbuf);
                } else
                  new_str(path_spool, "/var/spool/nwserv");
              }
              wpath = path_spool;
              if (what==4) name="queues/";
              break;


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
     && 1==sscanf((char*)buff, "%i", &i) ) return(i);
  return(-1);
}


static void sig_segv(int isig)
{
  errorp(11, "!!! SIG_SEGV !!!", "at pid=%d", my_pid);
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

static void creat_pidfile(void)
{
  char buf[300];
  char *pidfn=get_pidfilefn(buf);
  FILE *f=fopen(pidfn, "w");
  if (f != NULL) {
    fprintf(f, "%d\n", getpid());
    fclose(f);
  } else {
    errorp(1, "INIT", "Cannot creat pidfile=%s", pidfn);
    exit(1);
  }
}

void get_debug_level(uint8 *buf)
{
  char buf1[300], buf2[300];
  int i=sscanf((char*)buf, "%s %s", buf1, buf2);
  if (i > 0) {
    nw_debug=atoi((char*)buf1);
    debug_mask=0;
    if (i > 1) {
      char dummy;
      if (sscanf(buf2, "%ld%c", &debug_mask, &dummy) != 1)
          sscanf(buf2, "%lx",   &debug_mask);
    }
  }
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
  uint8 buff[300];
  if (get_ini_entry(NULL, 100+module, buff, sizeof(buff)))
    get_debug_level(buff);
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
  my_pid     = getpid();
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
      } else if (options == 3) { /* update tables */
        sig = SIGUSR1;
      } else {
        errorp(11, "INIT", "Program pid=%d already running and pidfn=%s exists" ,
               kill_pid, pidfn);
        exit(1);
      }
      if (kill_pid > 1) {
        kill(kill_pid, sig);
        if (sig == SIGUSR1) {  /* we try twice */
          sleep(2);
          kill(kill_pid, sig);
        }
      }
      exit(0);
    } else if (options == 1 || options == 2 || options == 3) {
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
        int flags = hextoi((char*)buf);
        new_log=flags&1;
        use_syslog=(flags&2) ? 2 : 0;
      } else if (100+module == what) {
        get_debug_level(buf);
      }
    }
    fclose(f);
  }
  if (dodaemon) {
    if (!withlog) strcpy(logfilename, "./nw.log");
    if (!strcmp(logfilename, "syslog"))
       use_syslog=1;

    if (NWSERV == module) {
      fprintf(stdout, "\n\nMars_nwe V%d.%02dpl%d started using %s.\n", 
         _VERS_H_, _VERS_L_, _VERS_P_, FILENAME_NW_INI);
      fprintf(stdout, "If you have problems, please read mars_nwe/doc/BUGS !\n");
      if (use_syslog==1) {
        fprintf(stdout, "Errors/warnings will be reported in syslog\n");
      } else {
        fprintf(stdout, "Errors/warnings will be reported in %s\n", logfilename);
      }
      fprintf(stdout, "\n\n");
      fflush(stdout);
    }

    if (NWSERV == module || NWROUTED == module) { /* now make daemon */
      int fd=fork();
      if (fd) exit((fd > 0) ? 0 : 1);
      my_pid=getpid();
    }
    if (use_syslog != 1){
      if (new_log && (NWSERV == module || NWROUTED == module))
        unlink(logfilename);
      if (NULL == (logfile = fopen(logfilename, "a"))) {
        logfile = stderr;
        errorp(1, "INIT", "Cannot open logfile='%s'",logfilename);
        exit(1);
      }
    }
    if (NWSERV == module || NWROUTED == module) {
      creat_pidfile();
      setsid();
    }
  } else logfile=stdout;
  if (  NWCONN != module || nw_debug > 1 ) {
    XDPRINTF((1, 0, "Starting Version: %d.%02dpl%d",
         _VERS_H_, _VERS_L_, _VERS_P_ ));
  }
#if 1
  if (nw_debug < 8)
    sigsegv_func = signal(SIGSEGV, sig_segv);
#endif
}

void exit_tools(void)
{
  if (in_module == NWSERV || in_module == NWROUTED) {
    char buf[300];
    unlink(get_pidfilefn(buf));
  }
}

uint8 down_char(uint8 ch)
{
  if (ch > 64 && ch < 91) return(ch + 32);
  switch(ch){
    case 142:  return(132);
    case 153:  return(148);
    case 154:  return(129);
  }
  return(ch);
}

uint8 up_char(uint8 ch)
{
  if (ch > 96 && ch < 123) return(ch - 32);
  switch(ch) {
    case 132:  return(142);
    case 148:  return(153);
    case 129:  return(154);
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

int hextoi(char *buf)
{
  int i;
  if (!buf || (1 != sscanf(buf, "%x", &i)))
    i=0;
  return(i);
}

unsigned int atou(char *buf)
{
  unsigned int u;
  if (!buf || (1 != sscanf(buf, "%i", &u)))
    u=0;
  return(u);
}

char *hex_str(char *buf, uint8 *s, int len)
{
  char *pp=buf;
  while (len--) {
    int i = sprintf(pp, "%02x ", *s++);
    pp += i;
  }
  return(buf);
}

int name_match(uint8 *s, uint8 *p)
/* simple match routine matches '?' and '*' */
{
  uint8   pc;
  while ( (pc = *p++) != 0){
    switch  (pc) {
      case '?' : if (!*s++) return(0);    /* simple char */
                 break;

      case '*' : if (!*p) return(1);      /* last star    */
                 while (*s) {
                   if (name_match(s, p) == 1) return(1);
                   ++s;
                 }
                 return(0);

      default : if (pc != *s++) return(0); /* normal char */
                break;
    } /* switch */
  } /* while */
  return ( (*s) ? 0 : 1);
}

uint8  *station_fn=NULL;

int find_station_match(int entry, ipxAddr_t *addr)
{
  int matched = 0;
  if (station_fn && *station_fn) {
    FILE *f=fopen((char*)station_fn, "r");
    if (f) {
      uint8  buff[200];
      uint8  addrstring[100];
      int   what;
      ipx_addr_to_adr((char*)addrstring, addr);
      upstr(addrstring);
      while (0 != (what = get_ini_entry(f, 0, buff, sizeof(buff)))){
        if (what == entry) {
          uint8  *p = buff + strlen((char*)buff);
          while (p-- > buff && *p==32) *p='\0';
          upstr(buff);
          if (name_match(addrstring, buff)) {
            matched=1;
            break;
          }
        }
      }
      fclose(f);
    } else {
      XDPRINTF((3, 0, "find_station_match, cannot open '%s'",
           station_fn));
    }
  }
  XDPRINTF((3, 0, "find_station_match entry=%d, matched=%d, addr=%s",
          entry, matched, visable_ipx_adr(addr)));
  return(matched);
}

