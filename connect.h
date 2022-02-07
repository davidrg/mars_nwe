/* connect.h 21-Jan-96 */

typedef struct {
  int      fd;          /* von System bei Open bzw. Create */
  long   offd;          /* aktueller File Offset           */
  time_t tmodi;         /* modification TIME               */
  FILE      *f;         /* for PIPE                        */
  int    flags;         /* 2 = PIPE                        */
                        /* 4 = don't reuse after close     */
  char   name[256];     /* UNIX Dateiname                  */
} FILE_HANDLE;

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


