/* emutli.h 11-Jul-98 */

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

#ifndef _EMUTLI_H_
#define _EMUTLI_H_

#ifdef FREEBSD
# include <netipx/ipx.h>
# define sipx_node sipx_addr.x_host.c_host
# define sipx_network sipx_addr.x_net.c_net
# define ipx_netlong(iaddr) (((union ipx_net_u *)(&((iaddr).x_net)))->long_e)

# define ETHERTYPE_IPX	0x8137
# define IPX_RT_8022	ETHERTYPE_IPX
# define IPX_RT_8023	ETHERTYPE_IPX
# define IPX_RT_SNAP	ETHERTYPE_IPX
# define IPX_RT_BLUEBOOK	ETHERTYPE_IPX
 int  ipx_iffind(char *ifname,struct ipx_addr *addr);
#else
# include <linux/types.h>
# include <netinet/in.h>
# include <linux/ipx.h>
#endif

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

#define IPXCMPSOCK(a, b) ( \
       ((char *)(a))[0]  == ((char*)(b))[0] && \
       ((char *)(a))[1]  == ((char*)(b))[1]    \
)

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

extern void set_locipxdebug(int debug);
extern void set_sock_debug(int sock);
extern void set_emu_tli(void);

extern int  poll(struct pollfd *fds, unsigned long nfds, int timeout);
extern int  t_open(char *name, int open_mode, char *p);
extern int  t_bind(int sock, struct t_bind *a_in, struct t_bind *a_out);
extern int  t_unbind(int sock);
extern void t_error(char *s);
extern int  t_close(int fd);
extern inline int t_rcvudata(int fd, struct t_unitdata *ud, int *flags);
extern int  t_rcvuderr(int fd, struct t_uderr    *ud);
extern inline int t_sndudata(int fd, struct t_unitdata *ud);

#ifndef  IPX_FRAME_8022
#  define  OLD_KERNEL_IPX    1
#  define  IPX_FRAME_8022    IPX_RT_8022
#endif

#ifndef  IPX_FRAME_8023
#  define  IPX_FRAME_8023    0
#endif

#ifndef  IPX_FRAME_SNAP
#  define  IPX_FRAME_SNAP    IPX_RT_SNAP
#endif

#ifndef  IPX_FRAME_ETHERII
#  define  IPX_FRAME_ETHERII  IPX_RT_BLUEBOOK
#endif

#endif

