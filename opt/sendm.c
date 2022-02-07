/* sendm.c 23-Oct-96 */
/*
 * simple demo for a sendmail acces under DOS
 * DOS <-> UNX command handling using PIPE filesystem.
 * can be used with unxsendm for UNX.
 *
 * Can also be used under Linux for ncpfs <-> mars_nwe.
 * but do not use it directly (the opencall will destroy unxsendm)!!
 *
 * QUICK + DIRTY !!!
 */

#define ENV_UNXCOMM    "UNXSENDM"
#ifdef LINUX
# define DEFAULT_COMM   "/pipes/unxsendm"
# else
# define DEFAULT_COMM   "p:/unxsendm"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#ifndef LINUX
#  include <io.h>
#endif
#include <fcntl.h>

int main(int argc, char **argv)
{
  char *unxcomm=getenv(ENV_UNXCOMM);
  int fdout;
  int fdin;
  int is_pipe=isatty(0) ? 0 :1;
  if (NULL == unxcomm)
    unxcomm=DEFAULT_COMM;
  fdout = open(unxcomm, O_RDWR);
  fdin  = dup(fdout);
  if (fdout > -1 && fdin > -1)  {
    char **pp=argv+1;
    unsigned char b=32;
    int  size;
    char buf[1024];
    while(--argc >0) {
      write(fdout, *pp, strlen(*pp));
      ++pp;
      write(fdout, &b, 1);
    }
    b=0;
    write(fdout, &b, 1);
    close(fdout);
    fdout=dup(fdin);
    if (6 == (size = read(fdin, buf, 6)) && !memcmp(buf, "+++++\n", 6)) {
      /* now write stdin -> sendmail */
      if (is_pipe) {
        while (0 < (size = fread(buf, 1, sizeof(buf), stdin)))
          write(fdout, buf, size);
      }
    } else if (size > 0)
      write(1, buf, size); /* probably errormessage */
    close(fdout);
    /* now we print errors */
    while (0 < (size = read(fdin, buf, sizeof(buf)))) {
      write(1, buf, size);
    }
    close(fdin);
    return(0);
  } else
    fprintf(stderr, "Cannot open PIPECOMMAND '%s'\n", unxcomm);
  return(1);
}
