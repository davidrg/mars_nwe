/* nwfile.h 23-Jan-96 */
#ifndef _NWFILE_H_
#define _NWFILE_H_

typedef struct {
  int      fd;          /* filehandle from system open/creat */
  long   offd;          /* aktuell file offset               */
  time_t tmodi;         /* modification TIME                 */
  FILE      *f;         /* for PIPE                          */
  int    flags;         /* 2 = PIPE                          */
                        /* 4 = don't reuse after close       */
  char   fname[256];    /* UNIX filename                     */
} FILE_HANDLE;

extern void init_file_module(void);

extern int file_creat_open(int volume, uint8 *unixname,
                           struct stat *stbuff,
                           int attrib, int access, int creatmode);

extern int nw_set_fdate_time(uint32 fhandle, uint8 *datum, uint8 *zeit);


extern int nw_close_datei(int fhandle, int reset_reuse);

extern uint8 *file_get_unix_name(int fhandle);

extern int nw_read_datei(int fhandle, uint8 *data, int size, uint32 offset);
extern int nw_seek_datei(int fhandle, int modus);
extern int nw_write_datei(int fhandle, uint8 *data, int size, uint32 offset);
extern int nw_server_copy(int qfhandle, uint32 qoffset,
                   int zfhandle, uint32 zoffset,
                   uint32 size);

extern int nw_lock_datei(int fhandle, int offset, int size, int do_lock);


#endif
