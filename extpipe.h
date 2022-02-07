/* extpipe.h 08-Aug-97 */

#ifndef _EXTPIPE_H_
#define _EXTPIPE_H_

/* enhanced pipe handling */
typedef struct {
  int  fds[3];              /* filedescriptor to  0,1,2 of new process */
  int  command_pid;         /* pid of piped command                    */
  int  flags;               /* special flags                           */
} FILE_PIPE;

extern int        ext_pclose(FILE_PIPE *fp);
extern FILE_PIPE *ext_popen(char *command, int uid, int gid);

#endif
