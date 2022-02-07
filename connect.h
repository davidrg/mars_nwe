/* connect.h 28-Jan-96 */
#ifndef _CONNECT_H_
#define _CONNECT_H_
typedef struct {
  DIR    *f;
  char   unixname[256]; /* kompletter unixname       */
  ino_t  inode;         /* Unix Inode                */
  time_t timestamp;     /* fÅr letzte Allocierung    */
  char   *kpath;        /* Ein Zeichen nach unixname */
  uint8  vol_options;   /* Suchoptions               */
  uint8  volume;        /* Volume Number	     */
} DIR_HANDLE;

typedef struct {
  uint8 path[256];      /* directory        */
  uint8 fn[256];        /* file             */
  int   volume;         /* Volume Number    */
  int   has_wild;       /* fn has wildcards */
} NW_PATH;

typedef struct {
   ino_t  inode;        /* Unix Inode dieses Verzeichnisses */
   time_t timestamp;    /* Zeitmarke          */
   uint8  *path;        /* path ab Volume     */
   uint8  volume;       /* Welches Volume     */
   uint8  is_temp;      /* 0:perm. 1:temp 2: spez. temp */
   uint8  drive;        /* driveletter        */
   uint8  task;         /* actual task        */
} NW_DIR;

typedef struct {
  uint8   name[14];              /* filename in DOS format */
  uint8   attrib;                /* Attribute  */
  uint8   ext_attrib;            /* File Execute Type */
  uint8   size[4];               /* size of file     */
  uint8   create_date[2];
  uint8   acces_date[2];
  uint8   modify_date[2];
  uint8   modify_time[2];
} NW_FILE_INFO;

typedef struct {
  uint8   name[14];              /* dirname */
  uint8   attrib;
  uint8   ext_attrib;
  uint8   create_date[2];
  uint8   create_time[2];
  uint8   owner_id[4];
  uint8   access_right_mask;
  uint8   reserved; /* future use */
  uint8   next_search[2];
} NW_DIR_INFO;


extern int nw_init_connect(void);
extern int nw_free_handles(int task);

extern int nw_creat_open_file(int dir_handle, uint8 *data, int len,
                NW_FILE_INFO *info, int attrib, int access, int mode);

extern int nw_delete_datei(int dir_handle,  uint8 *data, int len);
extern int nw_chmod_datei(int dir_handle, uint8 *data, int len, int modus);

extern int mv_file(int qdirhandle, uint8 *q, int qlen,
            int zdirhandle, uint8 *z, int zlen);



extern int nw_mk_rd_dir(int dir_handle, uint8 *data, int len, int mode);

extern int nw_search(uint8 *info,
              int dirhandle, int searchsequence,
              int search_attrib, uint8 *data, int len);

extern int nw_dir_search(uint8 *info,
              int dirhandle, int searchsequence,
              int search_attrib, uint8 *data, int len);


extern int nw_find_dir_handle( int dir_handle,
                               uint8      *data, /* zusÑtzlicher Pfad  */
                               int         len); /* LÑnge Pfad        */

extern int nw_alloc_dir_handle(
                      int dir_handle,  /* Suche ab Pfad dirhandle   */
                      uint8  *data,       /* zusÑtzl. Pfad             */
                      int    len,         /* LÑnge DATA                */
                      int    driveletter, /* A .. Z normal             */
                      int    is_temphandle, /* temporÑres Handle 1     */
                                               /* spez. temp Handle  2    */
                      int    task);          /* Prozess Task            */


extern int nw_open_dir_handle( int        dir_handle,
                        uint8      *data,     /* zusÑtzlicher Pfad  */
                        int        len,       /* LÑnge DATA         */
                        int        *volume,   /* Volume             */
                        int        *dir_id,   /* Ñhnlich Filehandle */
                        int        *searchsequence);


extern int nw_free_dir_handle(int dir_handle);

extern int nw_set_dir_handle(int targetdir, int dir_handle,
                             uint8 *data, int len, int task);

extern int nw_get_directory_path(int dir_handle, uint8 *name);

extern int nw_get_vol_number(int dir_handle);



extern int nw_get_eff_dir_rights(int dir_handle, uint8 *data, int len, int modus);

extern int nw_scan_dir_info(int dir_handle, uint8 *data, int len,
                            uint8 *subnr, uint8 *subname,
                            uint8 *subdatetime, uint8 *owner);


#define MAX_NW_DIRS    255
extern NW_DIR  dirs[MAX_NW_DIRS];
extern int     used_dirs;


extern int conn_get_kpl_path(NW_PATH *nwpath, int dirhandle,
	                  uint8 *data,   int len, int only_dir) ;

extern void set_default_guid(void);
extern void set_guid(int gid, int uid);


extern int nw_scan_a_directory(uint8   *rdata,
                        int     dirhandle,
                        uint8   *data,
                        int     len,
                        int     searchattrib,
                        uint32  searchbeg);   /* 32 bit */

extern int nw_scan_a_root_dir(uint8   *rdata,
                              int     dirhandle);


extern int fn_match(uint8 *s, uint8 *p, uint8 options);


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


extern uint16 un_date_2_nw(time_t time, uint8 *d);
extern time_t nw_2_un_time(uint8 *d, uint8 *t);
extern uint16 un_time_2_nw(time_t time, uint8 *d);

extern void xun_date_2_nw(time_t time, uint8 *d);
extern void xun_time_2_nw(time_t time, uint8 *d);

#endif
