/* connect.h 04-Apr-00 */
#ifndef _CONNECT_H_
#define _CONNECT_H_

typedef struct {
  uint8 path[256];      /* directory        */
  uint8 fn[256];        /* file             */
  int   volume;         /* Volume Number    */
  int   has_wild;       /* fn has wildcards */
} NW_PATH;

typedef struct {
  uint8   name[14];              /* filename in DOS format */
  uint8   attrib[2];             /* LO-HI  attrib, ext_attrib  */
  uint8   size[4];               /* size of file     */
  uint8   create_date[2];
  uint8   acces_date[2];
  uint8   modify_date[2];
  uint8   modify_time[2];
} NW_FILE_INFO;

typedef struct {
  uint8   name[14];              /* dirname */
  uint8   attrib[2];             /* LO-HI  attrib, ext_attrib  */
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
  uint8   attributes[4]; /* 0x20,0,0,0   LO-HI  */
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
  uint8   attributes[4]; /* 0x10,0,0,0   LO-HI   */
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

typedef struct {
  uint8   searchsequence[4]; /* same as NW_SCAN_DIR_INFO */
  uint8   change_bits[4];    /* LO-HI, 2=Attributes */
  union {
    NW_DOS_DIR_INFO  d;
    NW_DOS_FILE_INFO f;
  } u;
} NW_SET_DIR_INFO;

extern int use_mmap;
extern int tells_server_version;
extern int server_version_flags;
extern int max_burst_send_size;
extern int max_burst_recv_size;
extern int  default_uid;
extern int  default_gid;

extern int nw_init_connect(void);
extern void nw_exit_connect(void);

extern int nw_free_handles(int task);

extern int nw_creat_open_file(int dir_handle, uint8 *data, int len,
                NW_FILE_INFO *info, int attrib, int access, int mode, int task);

extern int nw_delete_files(int dir_handle, int searchattrib, uint8 *data, int len);
extern int nw_set_file_information(int dir_handle, uint8 *data, int len,
                             int searchattrib, NW_FILE_INFO *f);

extern int nw_set_file_attributes(int dir_handle, uint8 *data, int len,
                          int attrib, int newattrib);

extern int nw_mv_files(int searchattrib, 
                        int sourcedirhandle, uint8 *sourcedata, int qlen,
                        int zdirhandle, uint8 *destdata, int destdatalen);

extern int mv_dir(int dir_handle, uint8 *sourcedata, int qlen,
                           uint8 *destdata, int destdatalen);

extern int nw_unlink_node(int volume, uint8 *unname, struct stat *stb);
extern int nw_creat_node(int volume, uint8 *unname, int mode);

extern int nw_utime_node(int volume, uint8 *unname, struct stat *stb,
                   time_t t);

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
                      int    task,         /* Prozess Task            */
                      int    *eff_rights);


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

extern int nw_get_directory_path(int dir_handle, uint8 *name, int size_name);

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
extern int     act_id_flags; /* &1 == supervisor equivalence !!! */
extern int     entry8_flags; /* special flags, see examples nw.ini, entry 8 */
extern int     entry31_flags; /* special flags, see examples nw.ini, entry 31 */

extern int conn_get_full_path(int dirhandle, uint8 *data, int len,
                          uint8 *fullpath, int size_fullpath);

extern int conn_get_kpl_unxname(char *unixname,
                         int size_unixname,
                         int dirhandle,
                         uint8 *data, int len);

extern void   set_default_guid(void);
extern void   set_guid(int gid, int uid);
extern void   reset_guid(void);
extern void   reseteuid(void);
extern int    in_act_groups(gid_t gid);
extern int    get_unix_access_rights(struct stat *stb, uint8 *unixname);
extern int    get_unix_eff_rights(struct stat *stb);
extern void set_nw_user(int gid, int uid,
                 int id_flags, 
                 uint32 obj_id,   uint8 *objname,
                 int unxloginlen, uint8 *unxloginname,
                 int grpcount,    uint32 *grps);

extern uint32 get_file_owner(struct stat *stb);

extern int nw_scan_a_directory(uint8   *rdata,
                        int     dirhandle,
                        uint8   *data,
                        int     len,
                        int     searchattrib,
                        uint32  searchbeg);   /* 32 bit */

extern int nw_scan_a_root_dir(uint8   *rdata,
                              int     dirhandle);

extern int nw_set_a_directory_entry(int     dirhandle,
                             uint8   *data,
                             int     len,
                             int     searchattrib,
                             uint32  searchbeg,
                             NW_SET_DIR_INFO *f);

extern int fn_dos_match(uint8 *s, uint8 *p, int options);

extern void   un_date_2_nw(time_t time, uint8 *d, int high_low);
extern time_t nw_2_un_time(uint8 *d, uint8 *t);

extern void   un_time_2_nw(time_t time, uint8 *d, int high_low);

extern void mangle_dos_name(NW_VOL *vol, uint8 *unixname, uint8 *pp);

extern int nw_add_trustee(int dir_handle, uint8 *data, int len,
                   uint32 id,  int trustee, int extended);

extern int nw_del_trustee(int dir_handle, uint8 *data, int len,
                   uint32 id, int extended);

extern int nw_set_dir_info(int dir_handle, uint8 *data, int len,
                   uint32 owner_id, int max_rights, 
                   uint8 *creationdate, uint8 *creationtime);

extern int nw_scan_user_trustee(int volume, int *sequence, uint32 id, 
                        int *access_mask, uint8 *path);

extern int nw_scan_for_trustee( int    dir_handle, 
                         int    sequence,
                         uint8 *path,  
                         int    len,
                         int     max_entries, 
                         uint32 *ids,
                         int    *trustees,
                         int     extended);

extern int nw_log_file(int lock_flag,
                  int timeout,
                  int dir_handle,
                  int len,
                  char *data);

#endif
