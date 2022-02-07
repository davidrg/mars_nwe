/* simple UNX program to work together with 'comm'   */
#include <stdio.h>
#include <fcntl.h>

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

int main(int argc, char *argv[])
{
  int status=fcntl(0, F_GETFL);
  int size;
  char buf[MAXARGLEN+1024];
  if (status != -1) fcntl(0, F_SETFL, status|O_NONBLOCK);
  close(2);
  dup2(1,2);
  if (-1 < (size=read(0, buf, MAXARGLEN))){
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
