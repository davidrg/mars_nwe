/* nwqconn.c 26-Aug-97 */
/* (C)opyright (C) 1997  Martin Stover, Marburg, Germany
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

#include "nwvolume.h"
#include "nwfile.h"
#include "connect.h"
#include "nwqconn.h"

typedef struct S_INT_QUEUE_JOB {
  uint32           queue_id;
  int              job_id;
  int              fhandle;
  struct S_INT_QUEUE_JOB *next;
} INT_QUEUE_JOB;

INT_QUEUE_JOB *queue_jobs=NULL;

static INT_QUEUE_JOB *new_queue_job(uint32 queue_id, int job_id)
{
  INT_QUEUE_JOB *p=(INT_QUEUE_JOB*)xcmalloc(sizeof(INT_QUEUE_JOB));
  if (!queue_jobs) {
    queue_jobs=p;
  } else {
    INT_QUEUE_JOB *qj=queue_jobs;
    while (qj->next != NULL) qj=qj->next;
    qj->next=p;
  }
  p->next=NULL;
  p->queue_id=queue_id;
  p->job_id=job_id;
  return(p);
}

static void free_queue_job(uint32 queue_id, int job_id)
{
  INT_QUEUE_JOB *qj=queue_jobs;
  if (!qj) return;
  if (qj->queue_id==queue_id && qj->job_id == job_id){
    queue_jobs=qj->next;
    xfree(qj);
    return;
  }
  while (qj->next) {
    if (qj->next->queue_id == queue_id && qj->next->job_id == job_id) {
      INT_QUEUE_JOB *tmp=qj->next;
      qj->next=tmp->next;
      xfree(tmp);
      return;
    } else qj=qj->next;
  }
  /* not found */
}

static INT_QUEUE_JOB *find_queue_job(uint32 queue_id, int job_id)
{
  INT_QUEUE_JOB *qj=queue_jobs;
  while (qj) {
    if (qj->queue_id==queue_id && qj->job_id == job_id)
      return(qj);
    qj=qj->next;
  }
  return(NULL);
}

static int open_creat_queue_file(int mode, uint8 
                                *file_name, int file_name_len,
                                uint8 *dirname, int dirname_len)
 /* modes:
  * 0 : creat 
  * 1 : open ro  (as root)
  * 2 : open rw  (as root)
  */
{
  int result;
  result=nw_alloc_dir_handle(0, dirname, dirname_len, 99, 2, 1);
  if (result > -1) {
    char unixname[300];
    
    result=conn_get_kpl_unxname(unixname, result, file_name, file_name_len);
    if (result > -1) {
      struct stat stbuff;
      if (mode == 0) {  /* creat */
        result=file_creat_open(result, (uint8*)unixname,
                            &stbuff, 0x6, 0x6, 1|4|8, 0);
        if (result > -1)
           chmod(unixname, 0600);
      } else if (mode == 1) { /* open ro */
         result=file_creat_open(result, (uint8*)unixname,
                            &stbuff, 0x6, 0x9, 4|8, 0);
      } else if (mode == 2) { /* open rw  */
         result=file_creat_open(result, (uint8*)unixname,
                            &stbuff, 0x6, 0x6, 4|8, 0);
      } else result=-1;
    }
  } 
  if (result < 0) {
    uint8 dn[300];
    uint8 fn[300];
    strmaxcpy(dn, dirname, dirname_len);
    strmaxcpy(fn, file_name, file_name_len);
    XDPRINTF((1, 0, "open_creat_queue_file, mode=%d,result=-0x%x, dn='%s', fn='%s'", 
                        mode, -result, dn, fn));
    result=-0xff;
  }
  return(result);
}

int creat_queue_job(uint32 q_id,
                    uint8 *queue_job, 
                    uint8 *responsedata,
                    uint8 old_call)
{
  uint8 *dirname  = (old_call) ? queue_job+sizeof(QUEUE_JOB_OLD)
  		    	       : queue_job+sizeof(QUEUE_JOB);
  int   job_id;
  INT_QUEUE_JOB *jo;
  int result;
  memcpy(responsedata, queue_job, (old_call) ? sizeof(QUEUE_JOB_OLD)
   		       		  	     : sizeof(QUEUE_JOB)); 
  if (old_call) {
    QUEUE_JOB_OLD *job=(QUEUE_JOB_OLD*)responsedata;  /* before 3.11 */
    job_id = GET_BE16(job->job_id);
    jo     = new_queue_job(q_id, job_id);
    result = open_creat_queue_file(0, job->job_file_name+1, *(job->job_file_name),
                               dirname+1, *dirname);
    if (result > -1) {
      jo->fhandle  = (uint32) result;
      U16_TO_BE16(0,           job->job_file_handle);
      U32_TO_32(jo->fhandle,   job->job_file_handle+2);
      result = sizeof(QUEUE_JOB_OLD) - 202;
    }
  } else {
    QUEUE_JOB *job=(QUEUE_JOB*)responsedata; 
    job_id=GET_BE32(job->job_id);
    jo     = new_queue_job(q_id, job_id);
    result = open_creat_queue_file(0, job->job_file_name+1, *(job->job_file_name),
                               dirname+1, *dirname);
    if (result > -1) {
      jo->fhandle  = (uint32) result;
      U32_TO_32(jo->fhandle,   job->job_file_handle);
      result = sizeof(QUEUE_JOB) - 202;
    }
  }
  if (result < 0)
    free_queue_job(q_id, job_id);
  return(result);
}

int close_queue_job(uint32 q_id, int job_id)
{
  int result = -0xff;
  INT_QUEUE_JOB *jo=find_queue_job(q_id, job_id);
  if (jo) {
    nw_close_file(jo->fhandle, 0);
    result=0;
  }
  XDPRINTF((5,0,"close_queue_job Q=0x%x, job=%d, result=%d", 
        q_id, job_id, result));
  return(result);
}

int close_queue_job2(uint32 q_id, int job_id,
                     uint8 *client_area,
                     uint8 *prc, int prc_len)
{
  int result = -0xff;
  INT_QUEUE_JOB *jo=find_queue_job(q_id, job_id);
  XDPRINTF((5,0,"close_queue_job2, Q=0x%x, job=%d", q_id, job_id));
  if (jo) {
    if (prc_len) {
      char unixname[300];
      QUEUE_PRINT_AREA qpa;
      memcpy(&qpa, client_area, sizeof(QUEUE_PRINT_AREA));
      strmaxcpy((uint8*)unixname, 
          (uint8*)file_get_unix_name(jo->fhandle), sizeof(unixname)-1);
      XDPRINTF((5,0,"nw_close_file_queue fhandle=%d", jo->fhandle));
      if (*unixname) {
        char buff[1024];
        char printcommand[300];
        FILE *f=NULL;
        if (prc_len && *(prc+prc_len-1)=='!'){
          strmaxcpy((uint8*)buff, prc, prc_len-1);
          sprintf(printcommand, "%s %s %s", buff,
             qpa.banner_user_name, qpa.banner_file_name);
        } else
          strmaxcpy((uint8*)printcommand, prc, prc_len);
        nw_close_file(jo->fhandle, 1);
        jo->fhandle = 0L;
        if (NULL == (f = fopen(unixname, "r"))) {
          /* OK now we try the open as root */
          seteuid(0);
          f = fopen(unixname, "r");
          reset_guid();
        }
        if (NULL != f) {
          int  is_ok = 0;
          FILE_PIPE *fp = ext_popen(printcommand, geteuid(), getegid());
          if (fp) {
            int  k;
            is_ok++;
            while (is_ok && (k = fread(buff, 1, sizeof(buff), f)) > 0) {
              if (k != write(fp->fds[0], buff, k)) {
                XDPRINTF((1,0x10,"Cannot write to pipe `%s`", printcommand));
                if ((k=read(fp->fds[2], buff, sizeof(buff)-1)) > 0 ) {
                  buff[k]='\0';
                  XDPRINTF((1,0x0,"err='%s'", buff));
                }
                is_ok=0;
              }
            }
            if (0 != (k=ext_pclose(fp))) {
              XDPRINTF((1,0,"Errorresult = %d by closing print pipe", k));
            }
          } 
          fclose(f);
          if (is_ok) {
            seteuid(0);
            unlink(unixname);
            reset_guid();
            result=0;
          }
        } else XDPRINTF((1,0,"Cannot open queue-file `%s`", unixname));
      }
    } else {
      nw_close_file(jo->fhandle, 1);
    }
    free_queue_job(q_id, job_id);
  }
  return(result);
}

int service_queue_job(uint32 q_id,
                    uint8 *queue_job, 
                    uint8 *responsedata,
                    uint8 old_call)
{
  uint8 *dirname  = (old_call) ? queue_job+sizeof(QUEUE_JOB_OLD)
  		    	       : queue_job+sizeof(QUEUE_JOB);
  int   job_id;
  INT_QUEUE_JOB *jo;
  int result;
  memcpy(responsedata, queue_job, (old_call) ? sizeof(QUEUE_JOB_OLD)
   		       		  	     : sizeof(QUEUE_JOB)); 
  if (old_call) {
    QUEUE_JOB_OLD *job=(QUEUE_JOB_OLD*)responsedata;  /* before 3.11 */
    job_id = GET_BE16(job->job_id);
    jo     = new_queue_job(q_id, job_id);
    result = open_creat_queue_file(1, 
                               job->job_file_name+1, *(job->job_file_name),
                               dirname+1, *dirname);
    if (result > -1) {
      jo->fhandle  = (uint32) result;
      U16_TO_BE16(0,           job->job_file_handle);
      U32_TO_32(jo->fhandle,   job->job_file_handle+2);
      result = sizeof(QUEUE_JOB_OLD) - 202;
    }
  } else {
    QUEUE_JOB *job=(QUEUE_JOB*)responsedata; 
    job_id=GET_BE32(job->job_id);
    jo     = new_queue_job(q_id, job_id);
    result = open_creat_queue_file(1,
                               job->job_file_name+1, *(job->job_file_name),
                               dirname+1, *dirname);
    if (result > -1) {
      jo->fhandle  = (uint32) result;
      U32_TO_32(jo->fhandle,   job->job_file_handle);
      result = sizeof(QUEUE_JOB) - 202;
    }
  }
  if (result < 0)
    free_queue_job(q_id, job_id);
  return(result);
}

int finish_abort_queue_job(uint32 q_id, int job_id)
{
  int result = -0xff;
  INT_QUEUE_JOB *jo=find_queue_job(q_id, job_id);
  if (jo) {
    nw_close_file(jo->fhandle, 0);
    free_queue_job(q_id, job_id);
    result=0;
  }
  XDPRINTF((5,0,"finish_abort_queue_job Q=0x%x, job=%d, result=%d", 
        q_id, job_id, result));
  return(result);
}

void free_queue_jobs(void)
{
  INT_QUEUE_JOB *qj=queue_jobs;
  while(qj){
    INT_QUEUE_JOB *tmp=qj;
    qj=qj->next;
    xfree(tmp);
  }
  queue_jobs=NULL;
}

