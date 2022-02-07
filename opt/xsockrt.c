/* (C)opyright (C) 1997  Martin Stover, Marburg, Germany
 * simple program for adding/deleting ipx-routes for special sockets
 * e.g. for playing doom around ipx-networks.
 * needs special linux/net/ipx/af_ipx.c patch !!
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <strings.h>
#include <linux/ipx.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>


static int usage(char *prog)
{
  char *p=strrchr(prog, '/');
  if (p==NULL)p=prog;
  else p++;
  fprintf(stderr, "usage:\t%s add|del [socknr] \n", p);
  fprintf(stderr, "\tsocknr defaults to 0x869b (doom)\n");
  fprintf(stderr, "\tother known sockets are:\n");
  fprintf(stderr, "\t-0x8813 virgin games, Red Alert\n");
  fprintf(stderr, "\tadd 0 activates automatic add of socknr !!\n");
  fprintf(stderr, "\tdel 0 removes all socknr and deactivates automatic add !!\n");
  return(1);
}

static int handle_ioctl(int mode, int socknr)
{
  int fd = socket(AF_IPX, SOCK_DGRAM, AF_IPX);
  int result=0;
  if (fd > -1) {
    if (mode == 1 || !mode) {
      result = ioctl(fd, (mode==1) ? SIOCPROTOPRIVATE+4
                                   : SIOCPROTOPRIVATE+5,
                                    &socknr);
    } else result++;
    if (result==-1) {
      perror("ioctl");
      result=2;
    }
    close(fd);
  } else {
    result++;
    perror("open socket");
  }
  return(result);
}


int main(int argc, char *argv[])
{
  int socknr=0x869b;
  if (argc < 2)
    return(usage(argv[0]));
  if (argc > 2 && 1 != sscanf(argv[2],"%i", &socknr))
    return(usage(argv[0]));
  if (!strncasecmp(argv[1], "add", 3)) {
    return(handle_ioctl(1, socknr));
  } else if (!strncasecmp(argv[1], "del", 3)) {
    return(handle_ioctl(0, socknr));
  } else
    return(usage(argv[0]));
}


