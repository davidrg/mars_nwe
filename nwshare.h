/* nwshare.h:  25-Sep-99*/
/* (C)opyright (C) 1993-1999  Martin Stover, Marburg, Germany
 */

#ifndef _NWSHARE_H_
#define _NWSHARE_H_ 1
/* changed by: Ingmar Thiemann <ingmar@gefas.com> */
extern int share_file(int dev, int inode, int open_mode, int action);
extern int share_lock( int dev, int inode, int fd, int action, 
                       int lock_flag, int l_start, int l_len );
extern int share_unlock_all( int dev, int inode, int fd );

extern int share_set_file_add_rm(int lock_flag, int dev, int inode);
extern int share_set_logrec_add_rm(int lock_flag, int timeout, int len, char *data);
extern int share_handle_lock_sets(int type, int lock_flag, int timeout);

#endif
