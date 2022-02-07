/* nwqueue.c 04-Jun-98       */
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
#include "nwdbm.h"
#include "nwbind.h"
#include "nwqueue.h"

/* 
 * the bindery/global queue handling is in this modul.
 * The running process is nwbind.
 * the connection based queue stuff is in nwqconn.c         
*/

typedef struct S_INT_QUEUE_JOB {
  int     client_connection;
  int     client_task;
  uint32  client_id;
  time_t  entry_time;    
  uint32  target_id;          /* if 0xffffffff then all q-servers allowed */
  time_t  execute_time;       /* if -1 then at once */
  int     job_id;             /* 1 ... 999 */
  int     job_typ;
  int     job_position;
  int     job_control_flags;  /* if | 0x20 then job is actually queued */
  int     server_station;     
  int     server_task;
  uint32  server_id;
  uint8   job_description[50];
  time_t  file_entry_time;   /* for filenamehandling */  
  uint8   client_area[152];  /* must be int aligned  */
  struct S_INT_QUEUE_JOB *next;
} INT_QUEUE_JOB;

typedef struct S_QUEUE_SERVER {
  int     connection;           /* actual connection         */
  uint32  user_id;
  uint8   status_record[64];    /* for free queue server use */
} QUEUE_SERVER;

typedef struct S_NWE_QUEUE {
  uint32        id;
  int           last_job_id;  /* job id 1 .. 999 */
  int           count_jobs;
  INT_QUEUE_JOB *queue_jobs;
  int           status;       /* 0x1 = stations not allow to insert in queue
  			       * 0x2 = no more queue servers allowed to login.
                               * 0x4 = queue servers not allowed to handle queue.
                               */
  int           changed;      /* needs flush     */
  
  uint8         *queuedir;
  int           queuedir_len;
  int           queuedir_downshift;  /* is downshift volume */
  QUEUE_SERVER  *qserver;
  struct S_NWE_QUEUE *next;
} NWE_QUEUE;

NWE_QUEUE *nwe_queues=NULL;

static int entry18_flags;

static NWE_QUEUE *new_queue(uint32 id)
{
  NWE_QUEUE *p=(NWE_QUEUE*)xcmalloc(sizeof(NWE_QUEUE));
  if (!nwe_queues) {
    nwe_queues=p;
  } else {
    NWE_QUEUE *q=nwe_queues;
    while (q->next != NULL) q=q->next;
    q->next=p;
  }
  p->next=NULL;
  p->id=id;
  return(p);
}

static void free_queue_p(NWE_QUEUE *q)
{
  if (!q) return;
  xfree(q->qserver);
  xfree(q->queuedir);
  xfree(q);
}

static void free_queue(uint32 id)
{
  NWE_QUEUE *q=nwe_queues;
  if (!q) return;
  if (q->id==id){
    nwe_queues=q->next;
    free_queue_p(q);
    return;
  }
  while (q->next) {
    if (q->next->id == id) {
      NWE_QUEUE *tmp=q->next;
      q->next=tmp->next;
      free_queue_p(tmp);
      return;
    } else q=q->next;
  }
  /* not found */
}

static NWE_QUEUE *find_queue(uint32 id)
{
  NWE_QUEUE *q=(NWE_QUEUE*)nwe_queues;
  while (q) {
    if (q->id==id) return(q);
    q=q->next;
  }
  return(NULL);
}

static INT_QUEUE_JOB *add_queue_job(NWE_QUEUE *que, INT_QUEUE_JOB *p)
{
  if (que) {
    if (!que->queue_jobs) {
      que->queue_jobs=p;
      p->job_position=1;
      if (++(que->last_job_id)>999) que->last_job_id=1;
    } else {
      int flag;
      INT_QUEUE_JOB *qj;
      int verylastjobid=que->last_job_id?que->last_job_id:1;
      do {
        qj=que->queue_jobs;
        flag=1;
        do {
          if (++(que->last_job_id)>999) que->last_job_id=1;
          if (que->last_job_id==verylastjobid) {
            xfree(p);
            return(NULL);
          }
        } while (que->last_job_id==qj->job_id);
        while (qj->next != NULL) {
          qj=qj->next;
          if (qj->job_id==que->last_job_id) {
            flag=0;
            break;
          }
        }
      } while (!flag);
      qj->next=p;
      p->job_position=qj->job_position+1;
    }
    if (!p->job_id) 
      p->job_id=que->last_job_id;
    p->next=NULL;
    que->changed++;
    que->count_jobs++;
    return(p);
  }
  return(NULL);
}

static INT_QUEUE_JOB *new_queue_job(NWE_QUEUE*que, int connection, int task, uint32 id)
{
  if (que) {
    INT_QUEUE_JOB *p=(INT_QUEUE_JOB*)xcmalloc(sizeof(INT_QUEUE_JOB));
    time(&(p->entry_time));
    p->file_entry_time=p->entry_time;
    p->client_connection=connection;
    p->client_task=task;
    p->client_id=id;
    p->next=NULL;
    p->job_id=0;
    return(add_queue_job(que, p));
  }
  return(NULL);
}

static void free_queue_job(NWE_QUEUE *que, int job_id)
{
  INT_QUEUE_JOB *qj=(que) ? que->queue_jobs : NULL;
  if (!qj) return;
  if (qj->job_id==job_id){
    int pos=1;
    que->queue_jobs=qj->next;
    xfree(qj);
    qj=que->queue_jobs;
    while(qj) {
      qj->job_position=pos++;
      qj=qj->next;
    }
    que->changed++;
    que->count_jobs--;
    return;
  }
  while (qj->next) {
    if (qj->next->job_id == job_id) {
      INT_QUEUE_JOB *tmp=qj->next;
      int pos=tmp->job_position;
      qj->next=tmp->next;
      xfree(tmp);
      tmp=qj->next;
      while(tmp) {
        tmp->job_position=pos++;
        tmp=tmp->next;
      }
      que->changed++;
      que->count_jobs--;
      return;
    } else qj=qj->next;
  }
  /* not found */
}

static INT_QUEUE_JOB *find_queue_job(NWE_QUEUE *que, uint32 job_id)
{
  if (que) {
    INT_QUEUE_JOB *qj=que->queue_jobs;
    while (qj) {
      if (qj->job_id==job_id) return(qj);
      qj=qj->next;
    }
  }
  return(NULL);
}

static void r_w_queue_jobs(NWE_QUEUE *q, int mode)
/* mode == 0 read, 1 = write if changed, 2 = write always */
{
  if (q && (q->changed || !mode || mode == 2)) {
    uint8 path[300];
    int fd=open(get_div_pathes(path, NULL, 4, "%x/queue", q->id),
           mode ? O_RDWR|O_TRUNC|O_CREAT:O_RDONLY);
    if (fd>-1) {
      int i;
      if (!mode) { /* read */
        if (read(fd, &i, sizeof(i))==sizeof(i) && i==sizeof(INT_QUEUE_JOB)){
          INT_QUEUE_JOB *qj=(INT_QUEUE_JOB*)xmalloc(sizeof(INT_QUEUE_JOB));
          while (i == read(fd, qj, i)){
            if (i < sizeof(INT_QUEUE_JOB)) 
              memset(((uint8*)qj)+i, 0, sizeof(INT_QUEUE_JOB)-i);
            /* correct some possible wrong values */
            qj->server_station=0;
            qj->server_id=0;
            qj->job_control_flags &= ~0x20;
            

            add_queue_job(q, qj);
            qj=(INT_QUEUE_JOB*)xmalloc(sizeof(INT_QUEUE_JOB));
          
          }
          xfree(qj);
        }
      } else {
        INT_QUEUE_JOB *qj=q->queue_jobs;
        if (qj) {
          i=sizeof(INT_QUEUE_JOB);
          i=(sizeof(i)==write(fd, &i, sizeof(i))) ? 0: -1;
          while (!i&&qj) {
            if(sizeof(INT_QUEUE_JOB) != write(fd, qj, sizeof(INT_QUEUE_JOB)))
              --i;
            qj=qj->next;
          }
        }
      }
      close(fd);
    } else if (mode || errno != 2)  {
      XDPRINTF((1,0x10, "Cannot open '%s'", path));
    }
    q->changed=0;
  }
}

static void set_time_field(uint8 *timef, time_t ptime)
{
  if ((int)ptime == -1) {
    memset(timef, 0xff, 6);
  } else {
    struct tm *s_tm = localtime(&ptime);
    timef[0]    = (uint8) s_tm->tm_year;
    timef[1]    = (uint8) s_tm->tm_mon+1;
    timef[2]    = (uint8) s_tm->tm_mday;
    timef[3]    = (uint8) s_tm->tm_hour;
    timef[4]    = (uint8) s_tm->tm_min;
    timef[5]    = (uint8) s_tm->tm_sec;
  }
}

static time_t get_time_field(uint8 *timef)
{
  struct tm s_tm;
  if (0xff==timef[0]) return((time_t)-1);
  s_tm.tm_year   = timef[0];
  s_tm.tm_mon    = timef[1]-1;
  s_tm.tm_mday   = timef[2];
  s_tm.tm_hour   = timef[3];
  s_tm.tm_min    = timef[4];
  s_tm.tm_sec    = timef[5];
  s_tm.tm_isdst  = -1;  
  return(mktime(&s_tm));
}

static void build_queue_file_name(uint8 *job_file_name, INT_QUEUE_JOB *jo)
{
  *job_file_name = (uint8) sprintf((char*)job_file_name+1, 
     "%08lX.%03d", jo->file_entry_time, jo->job_id);
}

static void build_unix_queue_file(uint8 *buf, NWE_QUEUE *q, INT_QUEUE_JOB *jo)
{
  if (q->queuedir_len) {
    memcpy(buf, q->queuedir, q->queuedir_len);
    sprintf(buf+q->queuedir_len, "/%08lX.%03d", jo->file_entry_time, jo->job_id);
    if (q->queuedir_downshift)
      downstr(buf+q->queuedir_len+1);
  } else *buf='\0';
  XDPRINTF((3,0, "build_unix_queue_file=`%s`", buf));
}

int nw_get_q_dirname(uint32 q_id, uint8 *buff)
{
  return(nw_get_prop_val_str(q_id, "Q_DIRECTORY", buff));
}

static int nw_get_q_prcommand(uint32 q_id, uint8 *buff)
{
  return(nw_get_prop_val_str(q_id, "Q_UNIX_PRINT", buff));
}

static int fill_q_job_entry_old(INT_QUEUE_JOB *jo, 
                                QUEUE_JOB_OLD *job,
                                int full)
{
  job->client_connection = (uint8)jo->client_connection;
  job->client_task       = (uint8)jo->client_task;
  U32_TO_BE32(jo->client_id, job->client_id);
  U32_TO_BE32(jo->target_id, job->target_id);
  
  set_time_field(job->target_execute_time, jo->execute_time);
  set_time_field(job->job_entry_time,      jo->entry_time);
  
  U16_TO_BE16(jo->job_id,   job->job_id);
  U16_TO_BE16(jo->job_typ,  job->job_typ);

  job->job_position      = (uint8)jo->job_position;
  job->job_control_flags = (uint8)jo->job_control_flags;

  build_queue_file_name(job->job_file_name, jo);
  /* file handle is not filled here */
  job->server_station    = (uint8)jo->server_station;
  job->server_task       = (uint8)jo->server_task;
  U32_TO_BE32(jo->server_id, job->server_id);
  if (full) {
    memcpy(job->job_description, jo->job_description, 
          sizeof(job->job_description));
    memcpy(job->client_area, jo->client_area, 
          sizeof(job->client_area));
    return(sizeof(QUEUE_JOB_OLD));
  }
  return(54);
}

static int fill_q_job_entry(INT_QUEUE_JOB *jo, 
                            QUEUE_JOB *job,
                            int full)
{
  memset(job->record_in_use,   0xff, 2);
  memset(job->record_previous, 0, 4);
  
  if (jo->next)	{ /* (Alexey) we _must_ set id of next job in job-list */
    U32_TO_32(jo->next->job_id, job->record_next);
  } else  
    memset(job->record_next,     0, 4);
  
  U32_TO_32(jo->client_connection, job->client_connection);
  U32_TO_32(jo->client_task, job->client_task);
  U32_TO_BE32(jo->client_id, job->client_id);
  U32_TO_BE32(jo->target_id, job->target_id);
  
  set_time_field(job->target_execute_time, jo->execute_time);
  set_time_field(job->job_entry_time,      jo->entry_time);
  
  U16_TO_BE16(jo->job_id,   job->job_id);
  *(job->job_id+2) = 0;
  *(job->job_id+3) = 0;

  U16_TO_BE16(jo->job_typ,  job->job_typ);

  U16_TO_16(jo->job_position, job->job_position);
  U16_TO_16(jo->job_control_flags, job->job_control_flags);

  build_queue_file_name(job->job_file_name, jo);
  /* file handle is not filled here */
  U32_TO_32(jo->server_station, job->server_station);
  U32_TO_32(jo->server_task,    job->server_task);
  U32_TO_BE32(jo->server_id,    job->server_id);
  if (full) {
    memcpy(job->job_description, jo->job_description, 
          sizeof(job->job_description));
    memcpy(job->client_area, jo->client_area, 
          sizeof(job->client_area));
    return(sizeof(QUEUE_JOB));
  }
  return(78);
}

int nw_creat_queue_job(int connection, int task, uint32 object_id,
                       uint32 q_id, uint8 *q_job, uint8 *responsedata,
                       int old_call)
{
  uint8 *fulldirname  = (old_call) ? responsedata+sizeof(QUEUE_JOB_OLD)
                                   : responsedata+sizeof(QUEUE_JOB);
  int result          = nw_get_q_dirname(q_id, fulldirname+1);
  NWE_QUEUE *que=find_queue(q_id);
  INT_QUEUE_JOB *jo=NULL;
  if (result > 0 && que) {
    jo = new_queue_job(que, connection, task, object_id);
    *fulldirname=(uint8) result++;
    if (jo == NULL) return(-0xd4); /* queue full */
    if (old_call) {  /* before 3.11 */
      QUEUE_JOB_OLD *job  = (QUEUE_JOB_OLD*)q_job; 
      memcpy(jo->job_description, job->job_description, 
             sizeof(jo->job_description));
      memcpy(jo->client_area, job->client_area, 
             sizeof(jo->client_area));
      jo->target_id         = GET_BE32(job->target_id);
      jo->execute_time      = get_time_field(job->target_execute_time);
      jo->job_typ           = GET_BE16(job->job_typ);
      jo->job_control_flags = job->job_control_flags|0x20;
      result+=fill_q_job_entry_old(jo, (QUEUE_JOB_OLD*)responsedata, 1);
    } else {
      QUEUE_JOB *job  = (QUEUE_JOB*)q_job; 
      memcpy(jo->job_description, job->job_description, 
             sizeof(jo->job_description));
      memcpy(jo->client_area, job->client_area, 
             sizeof(jo->client_area));
      jo->target_id         = GET_BE32(job->target_id);
      jo->execute_time      = get_time_field(job->target_execute_time);
      jo->job_typ           = GET_BE16(job->job_typ);
      jo->job_control_flags = GET_16(job->job_control_flags) | 0x20;
      result+=fill_q_job_entry(jo, (QUEUE_JOB*)responsedata, 1);
    }
  } else {
    result=-0xd3; /* no rights */
  }
  XDPRINTF((6, 0, "creat_q_job, id=%d, result=%d", jo ? jo->job_id : -1, 
    result));
  return(result);
}

int nw_close_queue_job(uint32 q_id, int job_id, 
                   uint8 *responsedata)
{
  int result=-0xd8; /* queue not active */
  NWE_QUEUE *que=find_queue(q_id);
  if (que) {
    INT_QUEUE_JOB *jo=find_queue_job(que, job_id);
    if (jo) {
      int i;
      QUEUE_PRINT_AREA *qpa=(QUEUE_PRINT_AREA*)jo->client_area;
      result=sizeof(jo->client_area);
      if (entry18_flags&0x1) { /* always suppress banner */
        qpa->print_flags[1] &= ~0x80;
      }
      jo->job_control_flags &= ~0x20;
      memcpy(responsedata, jo->client_area, result);
      i = nw_get_q_prcommand(q_id, responsedata+result+1);
      if (i > -1) {  /* this job is handled directly by client */
        *(responsedata+result)=(uint8)i;
        result+=i;
        free_queue_job(que, job_id);
      } else 
        *(responsedata+result)=0;
      ++result;
    } else 
     result=-0xff;
  } 
  XDPRINTF(((result<0) ? 1 : 5, 0, "nw_close_queue_job, q=%lx, job=%d, result=%d", 
            q_id, job_id, result));
  return(result);  
}

int nw_get_queue_status(uint32 q_id,  int *status, int *entries, 
                 int *servers, int server_ids[], int server_conns[])
{
  NWE_QUEUE *q=find_queue(q_id);
  if (q) {
    *status=q->status;
    *entries=q->count_jobs;
    if (q->qserver) {
      *servers=1;
      server_ids[0]   = q->qserver->user_id;
      server_conns[0] = q->qserver->connection;
    } else
      *servers=0;
    return(0);
  }
  return(-0xff);
}

int nw_set_queue_status(uint32 q_id, int status)
{
  NWE_QUEUE *q=find_queue(q_id);
  if (q) {
    q->status=status;
    return(0);
  }
  return(-0xd3); /* no rights */
}

int nw_get_q_job_entry(uint32 q_id, int job_id,  uint32 fhandle,
                       uint8 *responsedata, int old_call)
{
  int result=-0xd5;
  NWE_QUEUE     *q  = find_queue(q_id);
  INT_QUEUE_JOB *qj = find_queue_job(q, job_id);
  if (qj) {
    if (old_call) {
      QUEUE_JOB_OLD *job=(QUEUE_JOB_OLD*)responsedata;
      result=fill_q_job_entry_old(qj, job, 1);
      U16_TO_BE16(0,           job->job_file_handle);
      U32_TO_32(fhandle,       job->job_file_handle+2);
    } else {
      QUEUE_JOB *job=(QUEUE_JOB*)responsedata;
      result=fill_q_job_entry(qj, job, 1);
      U32_TO_32(fhandle,       job->job_file_handle);
    }
  }
  return(result);
}

int nw_get_queue_job_list_old(uint32 q_id, uint8 *responsedata)
{
  int result   = -0xff;
  NWE_QUEUE *q = find_queue(q_id);
  if (q) {
    INT_QUEUE_JOB *qj=q->queue_jobs;
    uint8 *p=responsedata+2;
    int count=0;
    while (qj && count < 255) {
      U16_TO_BE16(qj->job_id, p);
      p+=2;
      ++count;
      qj=qj->next;
    }
    U16_TO_BE16(count, responsedata);  /* Hi-Lo !! */
    result=2+count*2;
  }
  return(result);
}

int nw_get_queue_job_list(uint32 q_id, uint32 offset, uint8 *responsedata)
{
  int result   = -0xff;
  NWE_QUEUE *q = find_queue(q_id);
  if (q) {
    INT_QUEUE_JOB *qj=q->queue_jobs;
    uint8 *p=responsedata+8;
    int fullcount=0;
    int count=0;
    if (offset == MAX_U32)
      offset = 0;
    while (qj) {   
      if (++fullcount > offset && count < 125) { /* max. 125 entries */
        ++count;
        U16_TO_BE16(qj->job_id, p);
        p+=2;
        *p++=0;
        *p++=0;
      }
      qj=qj->next;
    }
#if 0
    U32_TO_BE32(fullcount, responsedata);  
    U32_TO_BE32(count,     responsedata+4); 
#else
    /* georg@globaltrading.net */
    U32_TO_32(fullcount, responsedata);  
    U32_TO_32(count,     responsedata+4); 
#endif
    result=8+count*4;
  }
  return(result);
}



static int get_qj_file_size(NWE_QUEUE *q, INT_QUEUE_JOB  *qj)
{
  if (q && qj) {
    struct stat stb;
    uint8 buf[300];
    build_unix_queue_file(buf, q, qj);
    if (!stat(buf, &stb))
       return(stb.st_size);
  }
  return(0);
}

int nw_get_queue_job_file_size(uint32 q_id, int job_id)
{
  int result=-0xd5;
  NWE_QUEUE     *q  = find_queue(q_id);
  INT_QUEUE_JOB *qj = find_queue_job(q, job_id);
  if (qj) 
    return(get_qj_file_size(q, qj));
  return(result);
}

int nw_change_queue_job_entry(uint32 q_id, uint8 *qjstruct)
{
   /*  TODO */

   return(-0xfb);
}

static int remove_queue_job_file(NWE_QUEUE *q, INT_QUEUE_JOB *qj)
{
  struct stat stb;
  uint8 buf[300];
  build_unix_queue_file(buf, q, qj);
  if (!stat(buf, &stb)) {
    int result=unlink(buf);
    if (result) {
      XDPRINTF((1, 0, "remove_queue_job_file, cannot remove `%s`.", buf));
    }
    return(result);
  }
  return(0);
}

int nw_remove_job_from_queue(uint32 user_id, uint32 q_id, int job_id)
{
  int result=-0xff;
  NWE_QUEUE     *q  = find_queue(q_id);
  INT_QUEUE_JOB *qj = find_queue_job(q, job_id);
  if (qj) {  
    if (user_id==1 || user_id == qj->client_id) {
      result=remove_queue_job_file(q, qj);
      if (!result)
        free_queue_job(q, job_id);
      else 
        result=-0xd6;
    } else result=-0xd6; /* no queue user rights */
  }
  return(result);
}

void nw_close_connection_jobs(int connection, int task)
/* 
 * this routine closes pending client open queue jobs 
 * if (task == -1) all jobs of connection are affected
*/
{
  NWE_QUEUE *q=(NWE_QUEUE*)nwe_queues;
  while (q) {
    INT_QUEUE_JOB *qj=q->queue_jobs;
    while(qj) {
      if (qj->client_connection == connection
        && (qj->client_task == task || task == -1)
        && (qj->job_control_flags & 0x20) ) { /* actual queued */
        if (get_qj_file_size(q, qj) > 0) {  /* we mark it as not queued */
          qj->job_control_flags &= ~0x20;
        } else {  /* we remove it */
          XDPRINTF((1, 0, "Queue job removed by nw_close_connection_jobs, conn=%d, task=%d", connection, task));
          (void)remove_queue_job_file(q, qj);
          free_queue_job(q, qj->job_id);
        }
        qj=q->queue_jobs;
        continue;
      }
      qj=qj->next;
    }
    q=q->next;
  }
}

/* ------------------ for queue servers ------------------- */
static QUEUE_SERVER *new_qserver(uint32 user_id, int connection)
{
  QUEUE_SERVER *qs=(QUEUE_SERVER*)xcmalloc(sizeof(QUEUE_SERVER));
  qs->user_id=user_id;
  qs->connection=connection;
  return(qs);
}

static void free_qserver(QUEUE_SERVER *qs)
{
  if (qs) {
    xfree(qs);
  }
}

int nw_attach_server_to_queue(uint32 user_id, 
                              int connection, 
                              uint32 q_id)
{
  int result=-0xff;
  NWE_QUEUE *q = find_queue(q_id);
  if (q) {
    if (!(result=nw_is_member_in_set(q_id, "Q_SERVERS", user_id))){
#if 1      
      if (q->qserver) {
        free_qserver(q->qserver);
        q->qserver=NULL;
      }
#endif
      if (!q->qserver) {
        q->qserver=new_qserver(user_id, connection);
      } else result=-0xdb; /* too max queue servers */
        /* we only allow 1 qserver/queue in this version */
    }
  }
  XDPRINTF((2, 0, "attach TO QUEUE q_id=0x%x, user=0x%x, conn=%d, result=0x%x", 
     q_id, user_id, connection, result));
  return(result);
}

int nw_detach_server_from_queue(uint32 user_id, 
                                int connection, 
                                uint32 q_id)
{
  int result=-0xff;
  NWE_QUEUE *q = find_queue(q_id);
  if (q && q->qserver 
        && q->qserver->user_id    == user_id
        && q->qserver->connection == connection) {
    free_qserver(q->qserver);
    q->qserver=NULL;
    result=0;
  }
  return(result);
}


int nw_service_queue_job(uint32 user_id, int connection, int task,
    			uint32 q_id, int job_typ, 
    			uint8 *responsedata, int old_call)	 
{
  int result=-0xd5;  /* no job */
  NWE_QUEUE *q = find_queue(q_id);
  if (q && q->qserver 
        && q->qserver->user_id    == user_id
        && q->qserver->connection == connection) {
    if ( !(q->status & 4) ) {  /* not stopped printing */
      uint8 *fulldirname  = (old_call) ? responsedata+sizeof(QUEUE_JOB_OLD)
                                       : responsedata+sizeof(QUEUE_JOB);
      int len             = nw_get_q_dirname(q_id, fulldirname+1);
      
      if (len > 0) {
        INT_QUEUE_JOB *qj=q->queue_jobs;
        INT_QUEUE_JOB *fqj=NULL;
        time_t acttime=time(NULL);
        *fulldirname=(uint8) len++;
        *(fulldirname+len)=0; /* for testprints only */
        while(qj) {
          if (  (!qj->server_id)
             && !(qj->job_control_flags&0x20)  /* not actual queued */
             && qj->execute_time <= acttime 
             && (qj->target_id == MAX_U32 || qj->target_id == user_id)
             && (qj->job_typ == MAX_U16 || job_typ==MAX_U16 
                                        || qj->job_typ == job_typ)) {
            if (get_qj_file_size(q, qj) > 0) {
              fqj=qj;
              break;
            } else {
              if (time(NULL) - qj->entry_time > 60) {  /* ca. 1 min */
                XDPRINTF((1, 0, "Queue job of size 0 automaticly removed"));
                (void)remove_queue_job_file(q, qj);
                free_queue_job(q, qj->job_id);
                qj=q->queue_jobs;
                continue;
              }
            }
          } else {
            XDPRINTF((6, 0, "Queue job ignored: station=%d, target_id=0x%x,job_typ=0x%x, %s",
               qj->server_station, qj->target_id, qj->job_typ,  
               (qj->execute_time > acttime) ? "execute time not reached" : ""));
          }
          qj=qj->next;
        }
        if (fqj) {
          fqj->server_id      = user_id;
          fqj->server_station = connection;
          fqj->server_task    = task;
          if (old_call) {
            QUEUE_JOB_OLD *job=(QUEUE_JOB_OLD*)responsedata;
            result=fill_q_job_entry_old(fqj, job, 1);
          } else {
            QUEUE_JOB *job=(QUEUE_JOB*)responsedata;
            result=fill_q_job_entry(fqj, job, 1);
          }
          result+=len;
          XDPRINTF((3, 0, "nw service queue job dirname=`%s`", fulldirname+1));
        } else {
          XDPRINTF((3, 0, "No queue job found for q_id=0x%x, user_id=0x%x,job_typ=0x%x", 
               q_id, user_id, job_typ));
        }
      } else {
        XDPRINTF((1, 0, "Could not get queuedir of q_id=0x%x", q_id));
      }
    } /* if */
  } else {
    XDPRINTF((1, 0, "Could not find qserver q_id=0x%x, user_id=0x%x, connect=%d",
            q_id, user_id, connection));
  }
  return(result);
}

int nw_finish_abort_queue_job(int mode, uint32 user_id, int connection,
                       uint32 q_id, int job_id)
/* modes
 * 0 = finish,
 * 1 = abort
*/
{
  NWE_QUEUE *que=find_queue(q_id);
  if (que) {
    INT_QUEUE_JOB *jo=find_queue_job(que, job_id);
    if (jo && jo->server_id == user_id
      && jo->server_station == connection) {
      if (mode && (jo->job_control_flags&0x10) ) {  /* restart job */
        jo->server_id=0;
        jo->server_station=0;
      } else {
        if (!remove_queue_job_file(que, jo))
          free_queue_job(que, job_id);
        else return(-0xd6);
      }
      return(0);
    }
    return(-0xff);
  }
  return(-0xd8);  /* queue not active */
}

void exit_queues(void)
{
  if (nwe_queues) {
    NWE_QUEUE *q=(NWE_QUEUE*)nwe_queues;
    while (q) {
      INT_QUEUE_JOB *qj=q->queue_jobs;
      uint32 qid=q->id;
      r_w_queue_jobs(q, 2);
      while(qj) {
        int job_id=qj->job_id;
        qj=qj->next;
        free_queue_job(q, job_id);
      }
      q=q->next;
      free_queue(qid);
    }
    nwe_queues=NULL;
  }
}

static int build_unix_queue_dir(uint8 *buf, uint32 q_id)
{
  int result = -0xff;
  uint8 buf1[300];
  uint8 *p;
  memcpy(buf, sys_unixname, sys_unixnamlen);
  result=nw_get_q_dirname(q_id, buf1);
  upstr(buf1);
  if (result > -1 && NULL != (p=strchr(buf1, ':')) ) {
    *p++='\0';
    result -= (int)(p - buf1);
    if (!strcmp(buf1, sys_sysname)) {
      memcpy(buf+sys_unixnamlen, p, result);
      result+=sys_unixnamlen;
      if (buf[result-1]=='/')
        --result;
      buf[result]='\0';
      if (sys_downshift)
        downstr(buf+sys_unixnamlen);
    }
  }
  XDPRINTF((result<0?1:5,0, "build_unix_queue_dir=`%s`, len=%d", buf, result));
  return(result);
}


int nw_creat_queue(int q_typ, uint8 *q_name, int q_name_len, 
                   uint8 *path, int path_len, uint32 *q_id)
{
  NETOBJ obj;
  int result;
  if (q_typ != 0x3) return(-0xfb); /* we only support print queues */
  strmaxcpy(obj.name, q_name, min(47, q_name_len));
  obj.type      =  q_typ;
  obj.flags     =  (uint8)  O_FL_STAT;
  obj.security  =  (uint8)  0x31;
  obj.id        =  0L;
  result        =  nw_create_obj(&obj, 0);
  
  if (!result) {
    uint8 q_directory[300];
    if (path_len && path_len < 230) {
      memcpy(q_directory, path, path_len);
      path=q_directory+path_len;
      *path=0;
      upstr(q_directory);
    } else {
      xstrcpy(q_directory, "SYS:SYSTEM");
      path_len=10;
      path=q_directory+path_len;
    }
    sprintf(path, "/%08lX.QDR", obj.id);
    *q_id = obj.id;
    nw_new_obj_prop(obj.id, NULL,            0,     0,    0,
	             "Q_DIRECTORY",      P_FL_ITEM,   0x31,
	              q_directory,  strlen(q_directory), 1);
    
    nw_new_obj_prop(obj.id , NULL,             0  ,   0  ,   0,
	             "Q_USERS",           P_FL_SET,   0x31,
	              NULL,  0, 0);
    nw_new_obj_prop(obj.id , NULL,             0  ,   0  ,   0,
	             "Q_OPERATORS",       P_FL_SET,   0x31,
	              NULL,  0, 0);
    nw_new_obj_prop(obj.id , NULL,             0  ,   0  ,   0,
 	             "Q_SERVERS",         P_FL_SET,   0x31,
	              NULL,  0, 0);

    nwdbm_mkdir(get_div_pathes(q_directory, NULL, 4, "%x", obj.id), 
                  0700, 0);

    result=build_unix_queue_dir(q_directory, obj.id);
    
    if (result > 0) {
      NWE_QUEUE *que=new_queue(obj.id);
      nwdbm_mkdir(q_directory, 0775, 0);
      new_str(que->queuedir, q_directory);
      que->queuedir_len=result;
      que->queuedir_downshift=sys_downshift;
      r_w_queue_jobs(que, 0);
      result=0;
    } else result=-1;
  }
  return(result);
}

int nw_destroy_queue(uint32 q_id)
{
  NETOBJ obj;
  int result;
  obj.id=q_id;
  result=nw_get_obj(&obj);
  if (!result) {
    if (obj.type == 3) { /* only print queues */
      uint8 buf[300];
      get_div_pathes(buf, NULL, 4, "%x", obj.id);
      nwdbm_rmdir(buf);
      result=build_unix_queue_dir(buf, obj.id);
      if (result > 0) {
        NWE_QUEUE *q=find_queue(obj.id);
        if (q) {
          INT_QUEUE_JOB *qj=q->queue_jobs;
          while(qj) {
            int job_id=qj->job_id;
            qj=qj->next;
            free_queue_job(q, job_id);
          }
          free_queue(obj.id);
        }
        nwdbm_rmdir(buf);
        nw_delete_obj(&obj);
        result=0;
      } else
        result=result=-0xd3;  /* no rights */
    } else 
      result=result=-0xd3;  /* no rights */
  }
  return(result);
}


void init_queues(int entry18_flags_p)
{
  NETOBJ obj;
  uint8 buf[300];
  int result;
  uint8 *wild="*";
  uint32 last_obj_id=MAX_U32;
  entry18_flags=entry18_flags_p;
  exit_queues();
  strmaxcpy(buf, sys_unixname, sys_unixnamlen);
  XDPRINTF((3,0, "init_queues:unixname='%s'", buf));
  obj.type = 3; /* queue */
  xstrcpy(obj.name, wild);
  
  result = scan_for_obj(&obj, last_obj_id, 1);
  while (!result) {
    NWE_QUEUE *que;
    nwdbm_mkdir(get_div_pathes(buf, NULL, 4, "%x", obj.id), 
                  0700, 0);
    strmaxcpy(buf, obj.name, 47);
    XDPRINTF((3, 0, "init queue, id=0x%x, '%s'",
      obj.id, buf));
    result=build_unix_queue_dir(buf, obj.id);
    if (result > 0) {
      que=new_queue(obj.id);
      new_str(que->queuedir, buf);
      que->queuedir_len=result;
      que->queuedir_downshift=sys_downshift;
      r_w_queue_jobs(que, 0);
    }
    last_obj_id=obj.id;
    xstrcpy(obj.name, wild);
    result = scan_for_obj(&obj, last_obj_id, 1);
  }
}

