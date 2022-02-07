/* unxsendm.c 23-Oct-96 */
/* simple UNX program to work together with 'pipe-filesystem'
 * and DOS sendm.exe to get sendmail functionality under DOS
 * QUICK + DIRTY !!!
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>

#define MAXARGLEN 100

int bl_read(int fd, void *buf, int size)
{
  fd_set fdin;
  struct timeval t;
  int result;
  FD_ZERO(&fdin);
  FD_SET(fd, &fdin);
  t.tv_sec   = 1;   /* 1 sec should be enough */
  t.tv_usec  = 0;
  result = select(fd+1, &fdin, NULL, NULL, &t);
  if (result > 0)
    result=read(fd, buf, size);
  return(result);
}

int main(int argc, char *argv[])
{
  int size;
  char buf[1024];
  close(2);
  dup2(1,2);  /* we want stdout and errout */
  if (-1 < (size=bl_read(0, buf, MAXARGLEN))){
    FILE *f;
    char path[MAXARGLEN+200];
    buf[size]='\0';
    sprintf(path, "/usr/sbin/sendmail %s", buf);
    f=popen(path, "w");
    if (NULL != f) {
      write(1, "+++++\n", 6);
      while (0 < (size=bl_read(0, buf, sizeof(buf)))){
        fwrite(buf, size, 1, f);
      }
      pclose(f);
      return(0);
    }
    perror(path);
  } else
    perror("read stdin");
  return(1);
}
