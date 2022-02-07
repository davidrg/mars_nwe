/* tools.c  06-Dec-95 */
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
#include <sys/vfs.h>

int  nw_debug=0;
FILE *logfile=stdout;

char *xmalloc(uint size)
{
  char *p = (size) ? (char *)malloc(size) : (char*)NULL;
  if (p == (char *)NULL && size){
    fprintf(logfile, "not enough core, need %d Bytes\n", size);
    exit(1);
  }
  return(p);
}

void x_x_xfree(char **p)
{
  if (*p != (char *)NULL){
    free(*p);
    *p = (char*)NULL;
  }
}

int strmaxcpy(char *dest, char *source, int len)
{
  int slen = (source != (char *)NULL) ? min(len, strlen(source)) : 0;
  if (slen) memcpy(dest, source, slen);
  dest[slen] = '\0';
  return(slen);
}

int x_x_xnewstr(uint8 **p,  uint8 *s)
{
  int len = (s == NULL) ? 0 : strlen(s);
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
    va_start(ap, p);
    vfprintf(logfile, p, ap);
    va_end(ap);
    fflush(logfile);
  }
}

void xdprintf(int dlevel, char *p, ...)
{
  va_list ap;
  if (nw_debug >= dlevel) {
    va_start(ap, p);
    vfprintf(logfile, p, ap);
    va_end(ap);
    fflush(logfile);
  }
}

void errorp(int mode, char *what, char *p, ...)
{
  va_list ap;
  int errnum = errno;
  if (errnum >= 0 && errnum < _sys_nerr)
    fprintf(logfile, "%s:%s\n",       what, _sys_errlist[errnum]);
  else
    fprintf(logfile, "%s:errno=%d\n", what, errnum);
  if (p) {
    va_start(ap, p);
    vfprintf(logfile, p, ap);
    va_end(ap);
    fprintf(logfile, "\n");
  }
  fflush(logfile);
}

FILE *open_nw_ini(void)
{
  char *fname=FILENAME_NW_INI;
  FILE *f=fopen(fname, "r");
  if (f == (FILE*)NULL) fprintf(logfile, "Cannot open ini file `%s`\n", fname);
  return(f);
}

int get_ini_entry(FILE *f, int entry, char *str, int strsize)
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
        strmaxcpy(sx, buff, se);
        fentry = atoi(sx);
        if (fentry > 0 && ((!entry) || entry == fentry)) {
          if (ppe) *(ppe+1) = '\0';
          strmaxcpy(str, ppi, strsize-1);
          if (do_open) fclose(f);
          return(fentry);
        }
      }
    } /* while */
    if (do_open) fclose(f);
  }
  return(0);
}

char *get_exec_path(char *buff, char *progname)
{
  sprintf(buff, "%s/%s", PATHNAME_PROGS, progname);
  return(buff);
}

int get_ini_int(int what)
{
  char buff[30];
  int  i;
  if (get_ini_entry(NULL, what, buff, sizeof(buff))
     && 1==sscanf(buff, "%d", &i) ) return(i);
  return(-1);
}

void get_ini_debug(int module)
/* what:
 * 1 = nwserv
 * 2 = ncpserv
 * 3 = nwconn
 */
{
  int debug = get_ini_int(100+module);
  if (debug > -1) nw_debug = debug;
}

void init_tools(int module)
{
  char buff[300];
  char logfilename[300];
  FILE *f=open_nw_ini();
  int  withlog=0;
  int  dodaemon=0;
  int  new_log=0;
  if (f) {
    int  what;
    while (0 != (what=get_ini_entry(f, 0, buff, sizeof(buff)))) { /* daemonize */
      if (200 == what) dodaemon = atoi(buff);
      else if (201 == what) {
        strmaxcpy(logfilename, buff, sizeof(logfilename)-1);
        withlog++;
      } else if (202 == what) {
        new_log = atoi(buff);
      } else if (100+module == what) nw_debug=atoi(buff);
    }
    fclose(f);
  }
  if (dodaemon) {
    if (!withlog) strcpy(logfilename, "./nw.log");
    if (NWSERV == module) { /* now make daemon */
      int fd=fork();
      if (fd) exit((fd > 0) ? 0 : 1);
    }
    if (NULL == (logfile = fopen(logfilename,
           (new_log && NWSERV == module) ? "w" : "a"))) {
      char sxx[100];
      sprintf(sxx, "\n\nOpen logfile `%s`", logfilename);
      perror(sxx);
      logfile = stdout;
      fprintf(stderr, "\n!! ABORTED !!\n");
      exit(1);
    }
    if (NWSERV == module) setsid();
  }
}

void exit_tools(int what)
{
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

uint8 *upstr(uint8 *s)
{
  if (!s) return((uint8*)NULL);
  for (;*s;s++) *s=up_char(*s);
  return(s);
}

uint8 *downstr(uint8 *s)
{
  if (!s) return((uint8*)NULL);
  for (;*s;s++) *s=down_char(*s);
  return(s);
}

/* next is stolen from GNU-fileutils */
static long adjust_blocks (long blocks, int fromsize, int tosize)
{
  if (fromsize == tosize)	/* E.g., from 512 to 512.  */
    return blocks;
  else if (fromsize > tosize)	/* E.g., from 2048 to 512.  */
    return blocks * (fromsize / tosize);
  else				/* E.g., from 256 to 512.  */
    return (blocks + (blocks < 0 ? -1 : 1)) / (tosize / fromsize);
}

int get_fs_usage(char *path, struct fs_usage *fsp)
{
  struct statfs fsd;
  if (statfs (path, &fsd) < 0) return (-1);
  XDPRINTF((2,
    "blocks=%d, bfree=%d, bavail=%d, files=%d, ffree=%d, bsize=%d\n",
    fsd.f_blocks, fsd.f_bfree, fsd.f_bavail,
    fsd.f_files, fsd.f_ffree,  fsd.f_bsize));
#define convert_blocks(b) adjust_blocks ((b), fsd.f_bsize, 512)
  fsp->fsu_blocks = convert_blocks (fsd.f_blocks);
  fsp->fsu_bfree  = convert_blocks (fsd.f_bfree);
  fsp->fsu_bavail = convert_blocks (fsd.f_bavail);
  fsp->fsu_files  = fsd.f_files;
  fsp->fsu_ffree  = fsd.f_ffree;
  return(0);
}
