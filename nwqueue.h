/* nwqueue.h 04-May-96 */
#ifndef _NWQUEUE_H_
#define _NWQUEUE_H_

/* enhanced pipe handling */
typedef struct {
  FILE *fildes[3];          /* filedescriptor to  0,1,2 of new process */
  int  command_pid;         /* pid of piped command                    */
  int  flags;               /* special flags                           */
} FILE_PIPE;

extern int        ext_pclose(FILE_PIPE *fp);
extern FILE_PIPE *ext_popen(char *command, int uid, int gid);

/* queues */
typedef struct {
  uint8   record_in_use[2];
  uint8   record_previous[4];
  uint8   record_next[4];
  uint8   client_connection[4];
  uint8   client_task[4];
  uint8   client_id[4];

  uint8   target_id[4];           /* 0xff, 0xff, 0xff, 0xff */
  uint8   target_execute_time[6]; /* all 0xff               */
  uint8   job_entry_time[6];      /* all zero               */
  uint8   job_id[4];              /* ?? alles 0 HI-LOW   */
  uint8   job_typ[2];             /* z.B. Printform HI-LOW */
  uint8   job_position[2];        /* ?? alles 0  low-high ? */
  uint8   job_control_flags[2];   /* z.B  0x10, 0x00   */
              /* 0x80 operator hold flag */
              /* 0x40 user hold flag     */
              /* 0x20 entry open flag    */
              /* 0x10 service restart flag */
              /* 0x08 autostart flag */

  uint8   job_file_name[14];      /* len + DOS filename */
  uint8   job_file_handle[4];
  uint8   server_station[4];
  uint8   server_task[4];
  uint8   server_id[4];
  uint8   job_bez[50];             /* "LPT1 Catch"  */
  uint8   client_area[152];
} QUEUE_JOB;

typedef struct {
  uint8   client_connection;
  uint8   client_task;
  uint8   client_id[4];
  uint8   target_id[4];           /* 0xff, 0xff, 0xff, 0xff */
  uint8   target_execute_time[6]; /* all 0xff               */
  uint8   job_entry_time[6];      /* all zero               */
  uint8   job_id[2];              /* ?? alles 0 HI-LOW   */
  uint8   job_typ[2];             /* z.B. Printform HI-LOW */
  uint8   job_position;           /* zero */
  uint8   job_control_flags;       /* z.B  0x10       */
              /* 0x80 operator hold flag */
              /* 0x40 user hold flag     */
              /* 0x20 entry open flag    */
              /* 0x10 service restart flag */
              /* 0x08 autostart flag */

  uint8   job_file_name[14];       /* len + DOS filename */
  uint8   job_file_handle[6];
  uint8   server_station;
  uint8   server_task;
  uint8   server_id[4];
  uint8   job_bez[50];             /* "LPT1 Catch"  */
  uint8   client_area[152];
} QUEUE_JOB_OLD;                   /* before 3.11 */

typedef struct {
  uint8   version;                /* normal 0x0       */
  uint8   tabsize;                /* normal 0x8       */
  uint8   anz_copies[2];          /* copies 0x0, 0x01 */
  uint8   print_flags[2];         /*        0x0, 0xc0  z.B. with banner */
  uint8   max_lines[2];           /*        0x0, 0x42 */
  uint8   max_chars[2];           /*        0x0, 0x84 */
  uint8   form_name[16];          /*        "UNKNOWN" */
  uint8   reserved[6];            /*        all zero  */
  uint8   banner_user_name[13];   /*        "SUPERVISOR"  */
  uint8   bannner_file_name[13];  /*        "LST:"        */
  uint8   bannner_header_file_name[14];  /* all zero      */
  uint8   file_path_name[80];            /* all zero      */
} QUEUE_PRINT_AREA;

extern int nw_creat_queue(int connection, uint8 *queue_id, uint8 *queue_job,
                           uint8 *dirname, int dir_nam_len, int old_call);

extern int nw_close_file_queue(uint8 *queue_id,
                        uint8 *job_id,
                        uint8 *prc, int prc_len);
#endif
