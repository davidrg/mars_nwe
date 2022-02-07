#ifndef _NWCONN_H_
#define _NWCONN_H_

typedef struct {
  int   size;            /* max. read size or write size */
  char *ubuf;            /* readbuf                      */
  /* ------------------------------*/
  char    *ncp_resp;     /* response packet              */
  int     resp_size;     /* max. size of response packet */
  /* ------------------------------*/
  int     fh_r;   /* NCP-Filehandle,  read  */
  int     fd_r;   /* file-descriptor, read */

  int     fh_w;   /* NCP-Filehandle,  write  */
  int     fd_w;   /* file-descriptor, write */
} IPX_IO_RW;

extern IPX_IO_RW ipx_io;
extern int       use_ipx_io;

extern int act_connection;
extern int act_pid;
#endif

