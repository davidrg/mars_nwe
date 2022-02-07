/* emutli.h 30-Jan-96 */

/* (C)opyright (C) 1993,1995  Martin Stover, Marburg, Germany
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/ipx.h>

typedef unsigned char       uint8;
typedef unsigned short int uint16;
typedef unsigned long  int uint32;

#define IPX_NET_SIZE    4
#define IPX_NODE_SIZE   6
#define IPX_SOCK_SIZE   2

typedef struct {
	   uint8  net[IPX_NET_SIZE];
	   uint8  node[IPX_NODE_SIZE];
	   uint8  sock[IPX_SOCK_SIZE];
	} ipxAddr_t;

#define IPXCMPNODE(a, b) ( \
       ((char *)(a))[0]  == ((char*)(b))[0] && \
       ((char *)(a))[1]  == ((char*)(b))[1] && \
       ((char *)(a))[2]  == ((char*)(b))[2] && \
       ((char *)(a))[3]  == ((char*)(b))[3] && \
       ((char *)(a))[4]  == ((char*)(b))[4] && \
       ((char *)(a))[5]  == ((char*)(b))[5]    \
)

#define IPXCMPNET(a, b) ( \
       ((char *)(a))[0]  == ((char*)(b))[0] && \
       ((char *)(a))[1]  == ((char*)(b))[1] && \
       ((char *)(a))[2]  == ((char*)(b))[2] && \
       ((char *)(a))[3]  == ((char*)(b))[3]    \
)

struct netbuf {
  unsigned int maxlen;
  unsigned int len;
  char     *buf;
};

struct t_bind {
  struct netbuf  addr;
  unsigned int   qlen;
};


struct t_unitdata {
  struct netbuf  addr;    /* address  */
  struct netbuf  opt;     /* options */
  struct netbuf  udata;   /* userdata */
};

struct t_uderr {
  struct netbuf  addr;    /* address   */
  struct netbuf  opt;     /* options   */
  long    error;          /* eroorcode */
};

struct pollfd {
  int   fd;
  short events;
  short revents;
};

#define POLLIN    0x0001  /* fd readable   */
#define POLLPRI   0x0002  /* high priority */

#define TOUTSTATE      6  /* out of state */

extern int  poll(struct pollfd *fds, unsigned long nfds, int timeout);
extern int  t_open(char *name, int open_mode, char *p);
extern int  t_bind(int sock, struct t_bind *a_in, struct t_bind *a_out);
extern int  t_unbind(int sock);
extern void t_error(char *s);
extern int  t_close(int fd);
extern int  t_rcvudata(int fd, struct t_unitdata *ud, int *flags);
extern int  t_rcvuderr(int fd, struct t_uderr    *ud);
extern int  t_sndudata(int fd, struct t_unitdata *ud);


extern int  init_ipx(uint32 network, uint32 node, int ipx_debug);
extern void exit_ipx(int full);

extern int  init_dev(char  *devname, int frame, uint32 network);
extern void exit_dev(char  *devname, int frame);

#if 0
extern int get_ipx_addr(ipxAddr_t *addr);
#endif

extern void ipx_route_add(uint32  dest_net,
                          uint32  route_net,
                          uint8   *route_node);

extern void ipx_route_del(uint32  net);

extern void set_emu_tli(void);

#ifndef  IPX_FRAME_8022
#define  OLD_KERNEL_IPX    1
#define  IPX_FRAME_8022    IPX_RT_8022
#endif

#ifndef  IPX_FRAME_8023
#define  IPX_FRAME_8023    0
#endif

#ifndef  IPX_FRAME_SNAP
#define  IPX_FRAME_SNAP    IPX_RT_SNAP
#endif

#ifndef  IPX_FRAME_ETHERII
#define  IPX_FRAME_ETHERII  IPX_RT_BLUEBOOK
#endif



