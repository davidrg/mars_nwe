/* nwconn.c 04-May-96       */
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
#include <dirent.h>
#include "connect.h"
#include "nwfile.h"
#include "nwqueue.h"

static char **build_argv(char *buf, int bufsize,  char *command)
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
      } else if (!i && c == '/') {  /* here i must get argv[0] */
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

static void err_close_pipe(FILE_PIPE *fp, int lpid, int j, int piped[3][2])
{
  while (j--) if (fp->fildes[j]) fclose(fp->fildes[j]);
  close_piped(piped);
  kill(lpid, SIGTERM);
  kill(lpid, SIGQUIT);
  waitpid(lpid, NULL, 0);
  kill(lpid, SIGKILL);
}

static int x_popen(char *command, int uid, int gid, FILE_PIPE *fp)
{
  int piped[3][2];
  int lpid=-1;
  int j=3;
  char buf[300];
  char **argv=build_argv(buf, sizeof(buf), command);
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
    execvp(buf, argv);
    exit(1);    /* Never reached I hope */
  }
  j=-1;
  while (++j < 3) {
    int x  = (j) ? 0 : 1;
    int x_ = (j) ? 1 : 0;
    close(piped[j][x_]);
    piped      [j][x_]  = -1;
    fp->fildes [j]      = fdopen(piped[j][x], ( (j) ? "r" : "w") );
    if (NULL == fp->fildes[j]){
      err_close_pipe(fp, lpid, j+1, piped);
      return(-1);
    }
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
  while (j--) if (fp->fildes[j]) fclose(fp->fildes[j]);
  /* kill(fp->command_pid, SIGTERM); */
  waitpid(fp->command_pid, &status, 0);
  kill(fp->command_pid, SIGKILL);
  signal(SIGINT,   intsave);
  signal(SIGQUIT,  quitsave);
  signal(SIGHUP,   hupsave);
  xfree(fp);
  return(status);
}

FILE_PIPE *ext_popen(char *command, int uid, int gid)
{
  FILE_PIPE *fp=(FILE_PIPE*) xcmalloc(sizeof(FILE_PIPE));
  void (*intsave) (int) = signal(SIGINT,  SIG_IGN);
  void (*quitsave)(int) = signal(SIGQUIT, SIG_IGN);
  void (*hupsave) (int) = signal(SIGHUP,  SIG_IGN);
  if ((fp->command_pid  = x_popen(command, uid, gid, fp)) < 0) {
    xfree(fp);
    fp=NULL;
    XDPRINTF((1, 0, "ext_popen failed:command='%s'", command));
  }
  signal(SIGINT,   intsave);
  signal(SIGQUIT,  quitsave);
  signal(SIGHUP,   hupsave);
  return(fp);
}

/* minimal queue handling to enable simple printing */

#define MAX_JOBS    5   /*  max. open queue jobs for one connection */
static int anz_jobs=0;

typedef struct {
  uint32  fhandle;
  int     old_job;        /* is old structure */
  union  {
    QUEUE_JOB      n;
    QUEUE_JOB_OLD  o;
  } q;
} INT_QUEUE_JOB;

INT_QUEUE_JOB *queue_jobs[MAX_JOBS];

static INT_QUEUE_JOB *give_new_queue_job(int old_job)
{
  int k=-1;
  while (++k < anz_jobs) {
    INT_QUEUE_JOB *p=queue_jobs[k];
    if (!p->fhandle) { /* free slot */
      memset(p, 0, sizeof(INT_QUEUE_JOB));
      p->old_job = old_job;
      if (old_job)
        p->q.o.job_id[0] = k+1;
      else
        p->q.n.job_id[0] = k+1;
      return(p);
    }
  }
  if (anz_jobs < MAX_JOBS) {
    INT_QUEUE_JOB **pp=&(queue_jobs[anz_jobs++]);
    *pp = (INT_QUEUE_JOB *) xmalloc(sizeof(INT_QUEUE_JOB));
    memset(*pp, 0, sizeof(INT_QUEUE_JOB));
    (*pp)->old_job = old_job;
    if (old_job)
      (*pp)->q.o.job_id[0] = anz_jobs;
    else
      (*pp)->q.n.job_id[0] = anz_jobs;
    return(*pp);
  }
  return(NULL);
}

static void free_queue_job(int q_id)
{
  if (q_id > 0 && q_id <= anz_jobs) {
    INT_QUEUE_JOB **pp=&(queue_jobs[q_id-1]);
    uint32 fhandle   = (*pp)->fhandle;
    if (fhandle > 0) nw_close_datei(fhandle, 1);
    if (q_id == anz_jobs) {
      xfree(*pp);
      --anz_jobs;
    } else (*pp)->fhandle=0L;
  }
}

static void set_entry_time(uint8 *entry_time)
{
  struct tm  *s_tm;
  time_t     timer;
  time(&timer);
  s_tm = localtime(&timer);
  entry_time[0]    = (uint8) s_tm->tm_year;
  entry_time[1]    = (uint8) s_tm->tm_mon+1;
  entry_time[2]    = (uint8) s_tm->tm_mday;
  entry_time[3]    = (uint8) s_tm->tm_hour;
  entry_time[4]    = (uint8) s_tm->tm_min;
  entry_time[5]    = (uint8) s_tm->tm_sec;
}

static int create_queue_file(uint8   *job_file_name,
                             uint32 q_id,
                             int    jo_id,
                             int    connection,
                             uint8  *dirname,
                             int    dir_nam_len,
                             uint8  *job_bez)

{
  int result;
  NW_FILE_INFO fnfo;
  *job_file_name
     = sprintf((char*)job_file_name+1, "%07lX%d.%03d", q_id, jo_id, connection);

  result=nw_alloc_dir_handle(0, dirname, dir_nam_len, 99, 2, 1);
  if (result > -1)
    result = nw_creat_open_file(result, job_file_name+1,
                                       (int)  *job_file_name,
                                        &fnfo, 0x6, 0x6, 1 | 4);

  XDPRINTF((5,0,"creat queue file bez=`%s` handle=%d",
                                         job_bez, result));
  return(result);
}


int nw_creat_queue(int connection, uint8 *queue_id, uint8 *queue_job,
                           uint8 *dirname, int dir_nam_len, int old_call)
{
  INT_QUEUE_JOB *jo   = give_new_queue_job(old_call);
  uint32         q_id = GET_BE32(queue_id);
  int result = -0xff;
  XDPRINTF((5,0,"NW_CREAT_Q:dlen=%d, dirname=%s", dir_nam_len, dirname));

  if (NULL  != jo) {
    int jo_id = 0;
    if (jo->old_job) {
      jo_id = (int) jo->q.o.job_id[0];
      memcpy(&(jo->q.o), queue_job, sizeof(QUEUE_JOB_OLD));
      jo->q.o.job_id[0]         = (uint8) jo_id;
      jo->q.o.client_connection = (uint8)connection;
      jo->q.o.client_task       = (uint8)0xfe; /* ?? */
      U32_TO_BE32(1, jo->q.o.client_id); /* SU */
      set_entry_time(jo->q.o.job_entry_time);
      jo->q.o.job_typ[0]            = 0x0; /* 0xd0;*/
      jo->q.o.job_typ[1]            = 0x0;
      jo->q.o.job_position          = 0x1;
      jo->q.o.job_control_flags    |= 0x20;

      result = create_queue_file(jo->q.o.job_file_name,
                                 q_id, jo_id, connection,
                                 dirname, dir_nam_len,
                                 jo->q.o.job_bez);

      if (result > -1) {
        jo->fhandle     = (uint32) result;
        U16_TO_BE16(0,           jo->q.o.job_file_handle);
        U32_TO_BE32(jo->fhandle, jo->q.o.job_file_handle+2);
        result = 0;
      }
      jo->q.o.server_station = 0;
      jo->q.o.server_task    = 0;
      U32_TO_BE32(0, jo->q.o.server_id);
      if (!result) memcpy(queue_job, &(jo->q.o), sizeof(QUEUE_JOB_OLD));
    } else {
      jo_id = (int) jo->q.n.job_id[0];
      memcpy(&(jo->q.n), queue_job, sizeof(QUEUE_JOB));
      jo->q.n.job_id[0]         = (uint8) jo_id;

      U16_TO_BE16(0xffff, jo->q.n.record_in_use);
      U32_TO_BE32(0x0,    jo->q.n.record_previous);
      U32_TO_BE32(0x0,    jo->q.n.record_next);
      memset(jo->q.n.client_connection, 0, 4);
      jo->q.n.client_connection[0] = (uint8)connection;
      memset(jo->q.n.client_task,  0, 4);
      jo->q.n.client_task[0]       = (uint8)0xfe; /* ?? */
      U32_TO_BE32(1, jo->q.n.client_id); /* SU */
      set_entry_time(jo->q.n.job_entry_time);

      jo->q.n.job_typ[0]            = 0x0; /* 0xd0;*/
      jo->q.n.job_typ[1]            = 0x0;
      jo->q.n.job_position[0]       = 0x1;
      jo->q.n.job_position[1]       = 0x0;
      jo->q.n.job_control_flags[0] |= 0x20;
      jo->q.n.job_control_flags[1]  = 0x0;

      result = create_queue_file(jo->q.n.job_file_name,
                                 q_id, jo_id, connection,
                                 dirname, dir_nam_len,
                                 jo->q.n.job_bez);

      if (result > -1) {
        jo->fhandle = (uint32) result;
        U32_TO_BE32(jo->fhandle,    jo->q.n.job_file_handle);
        result = 0;
      }
      U32_TO_BE32(0, jo->q.n.server_station);
      U32_TO_BE32(0, jo->q.n.server_task);
      U32_TO_BE32(0, jo->q.n.server_id);
      if (!result) memcpy(queue_job, &(jo->q.n), sizeof(QUEUE_JOB));
    }
    if (result) free_queue_job(jo_id);
  }
  return(result);
}

int nw_close_file_queue(uint8 *queue_id,
                        uint8 *job_id,
                        uint8 *prc, int prc_len)
{
  int result = -0xff;
  int jo_id  = (int) *job_id;  /* ever only the first byte */
  XDPRINTF((5,0,"nw_close_file_queue JOB=%d", jo_id));
  if (jo_id > 0 && jo_id <= anz_jobs){
    INT_QUEUE_JOB *jo=queue_jobs[jo_id-1];
    int fhandle = (int)jo->fhandle;
    char unixname[300];
    strmaxcpy((uint8*)unixname, (uint8*)file_get_unix_name(fhandle), sizeof(unixname)-1);
    XDPRINTF((5,0,"nw_close_file_queue fhandle=%d", fhandle));
    if (*unixname) {
      char printcommand[256];
      FILE *f=NULL;
      strmaxcpy((uint8*)printcommand, prc, prc_len);
      nw_close_datei(fhandle, 1);
      jo->fhandle = 0L;
      if (NULL != (f = fopen(unixname, "r"))) {
        int  is_ok = 0;
        FILE_PIPE *fp = ext_popen(printcommand, geteuid(), getegid());
        if (fp) {
          char buff[1024];
          int  k;
          is_ok++;
          while ((k = fread(buff, 1, sizeof(buff), f)) > 0) {
            if (1 != fwrite(buff, k, 1, fp->fildes[0])) {
              XDPRINTF((1,0,"Cannot write to pipe `%s`", printcommand));
              is_ok=0;
            }
          }
          if (ext_pclose(fp)) {
            XDPRINTF((1,0,"Error by closing print pipe"));
          }
        } else
          XDPRINTF((1,0,"Cannot open pipe `%s`", printcommand));
        fclose(f);
        if (is_ok) {
          unlink(unixname);
          result=0;
        }
      } else XDPRINTF((1,0,"Cannot open queue-file `%s`", unixname));
    } else
      XDPRINTF((2,0,"fhandle=%d NOT OK !", fhandle));
    free_queue_job(jo_id);
  }
  return(result);
}
