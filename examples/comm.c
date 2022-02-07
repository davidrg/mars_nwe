/*
 * simple demo for a command programm which do a
 * DOS <-> UNX command handling using PIPE filesystem.
 * can be used with unxcomm for UNX.
 *
 * Can also be used under Linux for ncpfs <-> mars_nwe.
 *
 */

#define ENV_UNXCOMM    "UNXCOMM"
#ifdef LINUX
# define DEFAULT_COMM   "/pipes/unxcomm"
# else
# define DEFAULT_COMM   "p:/unxcomm"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#ifndef LINUX
#  include <io.h>
#endif
#include <fcntl.h>

static int usage(char *progname)
{
  fprintf(stderr, "Usage:\t%s prog [paras]\n", progname);
  return(1);
}

int main(int argc, char **argv)
{
  char *unxcomm=getenv("UNXCOMM");
  if (NULL == unxcomm) unxcomm=DEFAULT_COMM;
  if (argc > 1) {
    int fdout = open(unxcomm, O_RDWR);
    int fdin  = dup(fdout);
    if (fdout > -1 && fdin > -1)  {
      char **pp=argv+1;
      unsigned char b=32;
      int  size;
      int  buf[512];
      while(--argc) {
        write(fdout, *pp, strlen(*pp));
        ++pp;
        write(fdout, &b, 1);
      }
      b=0;
      write(fdout, &b, 1);
      close(fdout);

      while (0 < (size = read(fdin, buf, sizeof(buf)))) {
        write(1, buf, size);
      }
      close(fdin);
      return(0);
    } else
      fprintf(stderr, "Cannot open PIPECOMMAND '%s'\n", unxcomm);
  }
  return(usage(argv[0]));
}
