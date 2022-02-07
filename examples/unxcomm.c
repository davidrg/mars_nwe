/* unxcomm.c 08-Jun-97 */
/* simple UNX program to work together with 'comm'   */
/* to demonstrate usage of pipefilesystem */

#include <stdio.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>


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
  t.tv_sec   = 0;   
  t.tv_usec  = 100; /* 100 msec should be enough */
  result = select(fd+1, &fdin, NULL, NULL, &t);
  if (result > 0)
    result=read(fd, buf, size);
  return(result);
}

int main(int argc, char *argv[])
{
  int size=0;
  int l;
  char buf[MAXARGLEN+1024];
  close(2);
  dup2(1,2);
  
  while (0 < (l=bl_read(0, buf+size, MAXARGLEN-size)))
    size+=l;

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
