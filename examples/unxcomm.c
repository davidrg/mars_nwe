/* unxcomm.c 22-Oct-98 
 * simple UNX program to work together with 'comm'  
 * to demonstrate usage of pipefilesystem
 * needs mars_nwe version >= 0.99.pl13 !
 * comm and unxcomm must be same version  !  
 * 'run' directory must exist and must have  
 * read and write permission for every user.
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

static char **build_argv(int bufsize, char *command, int len)
/* routine returns **argv for use with execv routines */
{
  int offset     = ((len+4) / 4) * 4; /* aligned offset for **argv */
  int components = (bufsize - offset) / 4;
  if (components-- > 1) {  /* minimal argv[0] + NULL */
    char **argv  = (char **)(command+offset);
    char **pp    = argv;
    char  *p     = command;
    char  c;
    int   i=0;
    *pp    = p;
    *(p+len) = 0;
    while ((0 != (c = *p++)) && i < components) {
      if (c == 10 || c == 13) {
        *(p-1) = '\0';
        break;
      } else if (c == 32 || c == '\t') {
        *(p-1) = '\0';
        if (*p != 32 && *p != '\t' && *p != 10 && *p != 13) {
          *(++pp)=p;
          i++;
        }
      } else if (!i && c == '/') {  /* here i must get argv[0] */
        *pp=p;
      }
    }
    if (*pp && !**pp) *pp=NULL;
    else
      *(++pp)=NULL;
    return(argv);
  }
  return(NULL);
}
#define MAXARGLEN 1024

int bl_read(int fd, void *buf, int size)
{
  fd_set fdin;
  struct timeval t;
  int result;
  FD_ZERO(&fdin);
  FD_SET(fd, &fdin);
  t.tv_sec   =   1; 
  t.tv_usec  =   0; 
  result = select(fd+1, &fdin, NULL, NULL, &t);
  if (result > 0)
    result=read(fd, buf, size);
  return(result);
}

int main(int argc, char *argv[])
{
  int  size=-1;
  int  pid=getpid();
  char buf[MAXARGLEN+1024];
  char fifopath[257];
  char *p;
  
  close(2);
  dup2(1,2);
  
  if (argc > 3) {
    strcpy(fifopath, argv[0]);
    p=strrchr(fifopath, '/');
    if (p) {
      ++p;
      sprintf(p, "run/%08x.in",  pid);
      if (mkfifo(fifopath, 0600)) {
        perror("mkfifo"); 
        fprintf(stderr, "unxcomm:fifo.in=`%s`\n", fifopath);
      } else {
        fprintf(stdout, "#%08x\n", pid);
        fflush(stdout);
        size=0;
      }
    }
  }
  if (!size) {
    int  tries=0;
    int  fd = open(fifopath, O_RDONLY);
    if (fd > -1) {
      while (tries++ < 5) {
        int  l;
        while (0 < (l=bl_read(fd, buf+size, MAXARGLEN-size))) {
          size+=l;
        }
        if (size && buf[size-1] == '\0') break;
      }
      close(fd);
    } else {
      perror("open fifo");
      size=-1;
    }
    unlink(fifopath);
  }  
  if ( 0 < size) {
    char **argvv=build_argv(sizeof(buf), buf, size);
    if (argvv) {
      char path[300];
      execv(buf, argvv);
      sprintf(path, "/usr/bin/%s", *argvv);
      execv(path, argvv);
      sprintf(path, "/bin/%s", *argvv);
      execv(path, argvv);
      sprintf(path, "/usr/sbin/%s", *argvv);
      execv(path, argvv);
      sprintf(path, "/sbin/%s", *argvv);
      execv(path, argvv);
      fprintf(stderr, "%s:\tCould not find program '%s'\n", *argv, buf);
      exit(1);
    }
  }
  fprintf(stderr, "%s:\tGot no paras\n", *argv);
  exit(1);
}
