/* connect.h 14-Feb-98 */
#ifndef _CONNECT_H_
#define _CONNECT_H_

/* some TRUSTEE defines */
#define TRUSTEE_R    0x01  /* Read Rights */
#define TRUSTEE_W    0x02  /* Write Rights */
#define TRUSTEE_O    0x04  /* Open, not used ?? by Novell */
#define TRUSTEE_C    0x08  /* Creat */
#define TRUSTEE_E    0x10  /* Erase */
#define TRUSTEE_A    0x20  /* Access Control */
#define TRUSTEE_F    0x40  /* File Scan      */
#define TRUSTEE_M    0x80  /* Modify         */
/* ........................................ */
#define TRUSTEE_S   0x100  /* Supervisor     */


/* <-------------- File Attributes -------------> */
#define FILE_ATTR_NORMAL    0x00000000
#define FILE_ATTR_R         0x00000001
#define FILE_ATTR_H         0x00000002
#define FILE_ATTR_S         0x00000004
#define FILE_ATTR_DIR       0x00000010
#define FILE_ATTR_A         0x00000020
#define FILE_ATTR_SHARE     0x00000080

typedef struct {
  uint8 path[256];      /* directory        */
  uint8 fn[256];        /* file             */
  int   volume;         /* Volume Number    */
  int   has_wild;       /* fn has wildcards */
} NW_PATH;

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


typedef struct {
  uint8   time[2];
  uint8   date[2];
  uint8   id[4];
} NW_FILE_DATES_INFO;

typedef struct {
  uint8   subdir[4];
  uint8   attributes[4]; /* 0x20,0,0,0   File  */
  uint8   uniqueid;      /* 0    */
  uint8   flags;         /* 0x18 */
  uint8   namespace;     /* 0    */
  uint8   namlen;
  uint8   name[12];
  NW_FILE_DATES_INFO created;
  NW_FILE_DATES_INFO archived;
  NW_FILE_DATES_INFO updated;
  uint8              size[4];
  uint8              reserved_1[44];
  uint8              inherited_rights_mask[2];
  uint8              last_access_date[2];
  uint8              reserved_2[28];
} NW_DOS_FILE_INFO;

typedef struct {
  uint8   subdir[4];
  uint8   attributes[4]; /* 0x10,0,0,0   DIR   */
  uint8   uniqueid;      /* 0 */
  uint8   flags;         /* 0x14 or 0x1c */
  uint8   namespace;     /* 0 */
  uint8   namlen;
  uint8   name[12];
  NW_FILE_DATES_INFO created;
  NW_FILE_DATES_INFO archived;
  uint8   modify_time[2];
  uint8   modify_date[2];
  uint8   next_trustee[4];
  uint8   reserved_1[48];
  uint8   max_space[4];
  uint8   inherited_rights_mask[2];
  uint8   reserved_2[26];
} NW_DOS_DIR_INFO;

typedef struct {
  uint8   searchsequence[4];
  union {
    NW_DOS_DIR_INFO  d;
    NW_DOS_FILE_INFO f;
  } u;
} NW_SCAN_DIR_INFO;

extern int use_mmap;
extern int tells_server_version;
extern int server_version_flags;
extern int max_burst_send_size;
extern int max_burst_recv_size;

extern int nw_init_connect(void);
extern void nw_exit_connect(void);

extern int nw_free_handles(int task);

extern int nw_creat_open_file(int dir_handle, uint8 *data, int len,
                NW_FILE_INFO *info, int attrib, int access, int mode, int task);

extern int nw_delete_datei(int dir_handle,  uint8 *data, int len);
extern int nw_set_file_information(int dir_handle, uint8 *data, int len,
                             int searchattrib, NW_FILE_INFO *f);

extern int nw_set_file_attributes(int dir_handle, uint8 *data, int len,
                          int attrib, int newattrib);

extern int mv_file(int qdirhandle, uint8 *q, int qlen,
            int zdirhandle, uint8 *z, int zlen);

extern int mv_dir(int dir_handle, uint8 *q, int qlen,
                           uint8 *z, int zlen);

extern int nw_creat_node(int volnr, uint8 *unname, int mode);

extern int nw_mk_rd_dir(int dir_handle, uint8 *data, int len, int mode);

extern int nw_search(uint8 *info, uint32 *fileowner,
              int dirhandle, int searchsequence,
              int search_attrib, uint8 *data, int len);

extern int nw_dir_get_vol_path(int dirhandle, uint8 *path);

extern int nw_dir_search(uint8 *info,
              int dirhandle, int searchsequence,
              int search_attrib, uint8 *data, int len);


extern int nw_find_dir_handle( int dir_handle,
                               uint8      *data, /* zus„tzlicher Pfad  */
                               int         len); /* L„nge Pfad        */

extern int xinsert_new_dir(int volume, uint8 *path,
                           int dev,   int inode,
                           int drive, int is_temp, int task);

extern int nw_alloc_dir_handle(
                      int dir_handle,       /* directory handle     */
                      uint8  *data,         /* extra path           */
                      int    len,           /* len of datat         */
                      int    driveletter,   /* A .. Z normal        */
                      int    is_temphandle, /* temp Handle 1        */
                                            /* spez. temp Handle  2 */
                      int    task);         /* Prozess Task         */


extern int nw_open_dir_handle( int        dir_handle,
                        uint8      *data,   /* extra path           */
                        int        len,     /* len data             */
                        int        *volume, /* Volume               */
                        int        *dir_id, /* similar to filehandle*/
                        int        *searchsequence);


extern int nw_free_dir_handle(int dir_handle, int task);

extern int alter_dir_handle(int targetdir, int volume, uint8 *path,
                     int dev, int inode, int task);

extern int nw_set_dir_handle(int targetdir, int dir_handle,
                             uint8 *data, int len, int task);

extern int nw_get_directory_path(int dir_handle, uint8 *name);

extern int nw_get_vol_number(int dir_handle);



extern int nw_get_eff_dir_rights(int dir_handle, uint8 *data, int len,
                                 int modus);

extern int nw_scan_dir_info(int dir_handle, uint8 *data, int len,
                            uint8 *subnr, uint8 *subname,
                            uint8 *subdatetime, uint8 *owner);


extern void get_dos_file_attrib(NW_DOS_FILE_INFO *f,
                               struct stat *stb,
                               int          volume,
                               uint8        *path,
                               char         *unixname);

void get_dos_dir_attrib(NW_DOS_DIR_INFO *f,
                                struct stat *stb,
                                int   volume,
                                uint8 *path,
                                char  *unixname);


#define MAX_NW_DIRS    255
extern int     act_uid;
extern int     act_gid;
extern int     act_obj_id;   /* not login == 0             */
extern int     entry8_flags; /* special flags, see examples nw.ini, entry 8 */

extern int conn_get_full_path(int dirhandle, uint8 *data, int len,
                          uint8 *fullpath);

extern int conn_get_kpl_unxname(char *unixname,
                         int dirhandle,
                         uint8 *data, int len);

extern void   set_default_guid(void);
extern void   set_guid(int gid, int uid);
extern void   reset_guid(void);
extern void   reseteuid(void);
extern int    in_act_groups(gid_t gid);
extern void   set_nw_user(int gid, int uid,
                        uint32 obj_id, uint8 *objname,
                        int homepathlen, uint8 *homepath);

extern int    get_real_access(struct stat *stb);
extern uint32 get_file_owner(struct stat *stb);


extern int nw_scan_a_directory(uint8   *rdata,
                        int     dirhandle,
                        uint8   *data,
                        int     len,
                        int     searchattrib,
                        uint32  searchbeg);   /* 32 bit */

extern int nw_scan_a_root_dir(uint8   *rdata,
                              int     dirhandle);


extern int fn_dos_match(uint8 *s, uint8 *p, int options);

extern void   un_date_2_nw(time_t time, uint8 *d, int high_low);
extern time_t nw_2_un_time(uint8 *d, uint8 *t);

#if !NEW_ATTRIB_HANDLING
extern int    un_nw_attrib(struct stat *stb, int attrib, int mode);
#endif

extern int    un_nw_rights(struct stat *stb);

extern void   un_time_2_nw(time_t time, uint8 *d, int high_low);

extern void mangle_dos_name(NW_VOL *vol, uint8 *unixname, uint8 *pp);

extern int nw_add_trustee(int dir_handle, uint8 *data, int len,
                   uint32 id,  int trustee, int extended);


#endif
