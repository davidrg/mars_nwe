/* nwfile.h 11-May-96 */
#ifndef _NWFILE_H_
#define _NWFILE_H_
#include "nwqueue.h"

typedef struct {
  int      fd;          /* filehandle from system open/creat */
  long   offd;          /* actual file offset                */
  time_t tmodi;         /* modification TIME                 */
  FILE_PIPE *f;         /* for PIPE                          */
  int fh_flags;         /* 2 = PIPE                          */
                        /* 4 = don't reuse after close       */
                        /* 0x20 = readonly                   */
  char   fname[256];    /* UNIX filename                     */
} FILE_HANDLE;

/* fh_flags */
#define FH_IS_PIPE           0x01
#define FH_IS_PIPE_COMMAND   0x02
#define FH_DO_NOT_REUSE      0x04
#define FH_IS_READONLY       0x20

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
