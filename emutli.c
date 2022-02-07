/* emutli.c 07-Feb-96 */
/*
 * One short try to emulate TLI with SOCKETS.
 */

/* (C)opyright (C) 1993,1996 Martin Stover, Marburg, Germany
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

/*
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * Some of the Code in this module is stolen from the following
 * Programms: ipx_interface, ipx_route, ipx_configure, which were
 * written by Greg Page, Caldera, Inc.
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include "net.h"
#include <linux/if.h>
#include <linux/route.h>
#include <linux/in.h>
#include <signal.h>
#include <string.h>
#include <errno.h>


#ifndef max
#define max(a,b)        (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b)        (((a) < (b)) ? (a) : (b))
#endif

static int    locipxdebug=0;
static int    have_ipx_started=0;


static void set_sock_debug(int sock)
{
  if (setsockopt(sock, SOL_SOCKET, SO_DEBUG, &locipxdebug, sizeof(int))==-1){
    errorp(0, "setsockopt SO_DEBUG", NULL);
  }
}

static void sock2ipxadr(ipxAddr_t *i, struct sockaddr_ipx *so)
{
   memcpy(i->net,  &so->sipx_network, IPX_NET_SIZE + IPX_NODE_SIZE);
   memcpy(i->sock, &so->sipx_port, 2);
}

static void ipx2sockadr(struct sockaddr_ipx *so, ipxAddr_t *i)
{
   memcpy(&so->sipx_network, i->net, IPX_NET_SIZE + IPX_NODE_SIZE);
   memcpy(&so->sipx_port,    i->sock, 2);
}

void set_emu_tli()
{
  int i = get_ini_int(100);
  if (i > -1) locipxdebug = i;
}

static int x_ioctl(int sock, int mode, void *id)
{
  int result;
  int i = 0;
  do {
    result = ioctl(sock, mode, id);
    i++;
  } while ((i < 5) && (result < 0) && (errno == EAGAIN));
  return(result);
}


static void del_special_net(int special, char *devname, int frame)
/* specials =       */
/* IPX_SPECIAL_NONE */
/* IPX_INTERNAL     */
/* IPX_PRIMARY      */
/* devname + frame only if not IPX_INTERNAL */
{
  int sock = socket(AF_IPX, SOCK_DGRAM, AF_IPX);
  if (sock > -1) {
    struct ifreq  id;
    struct sockaddr_ipx *sipx = (struct sockaddr_ipx *)&id.ifr_addr;
    memset(&id, 0, sizeof(struct ifreq));

    sipx->sipx_network = 0L;
    sipx->sipx_special = special;
    sipx->sipx_family  = AF_IPX;
    if (special == IPX_PRIMARY) {
      FILE *f=fopen("/proc/net/ipx_interface", "r");
      if (f) {
        char buff[200];
        char buff1[200];
        char buff2[200];
        char buff3[200];
        char buff4[200];
        char buff5[200];
        while (fgets((char*)buff, sizeof(buff), f) != NULL){
          if (sscanf(buff, "%s %s %s %s %s",
              buff1, buff2, buff3, buff4, buff5) == 5) {
            int len = strlen(buff5);
            if (!len) continue;
            switch (*(buff5+len-1)) {
              case  '2' : sipx->sipx_type = IPX_FRAME_8022; break;
              case  '3' : sipx->sipx_type = IPX_FRAME_8023; break;
              case  'P' : sipx->sipx_type = IPX_FRAME_SNAP; break;
              case  'I' : sipx->sipx_type = IPX_FRAME_ETHERII; break;
              default   : continue;
            }
            upstr(buff3);
            if (!strcmp(buff3, "YES")) { /* primary */
              strcpy(id.ifr_name, buff4);
              break;
            }
          }
        }
        fclose(f);
      }
    } else if (special != IPX_INTERNAL) {
      if (devname && *devname) strcpy(id.ifr_name, devname);
      sipx->sipx_type    = frame;
    }
    sipx->sipx_action  = IPX_DLTITF;
    x_ioctl(sock, SIOCSIFADDR, &id);
    close(sock);
  }
}

#define del_internal_net() \
  del_special_net(IPX_INTERNAL, NULL, 0)
#define del_interface(devname, frame) \
  del_special_net(IPX_SPECIAL_NONE, (devname), (frame))
#define del_primary_net() \
  del_special_net(IPX_PRIMARY, NULL, 0)

static void add_special_net(int special,
  char *devname, int frame, uint32 netnum, uint32 node)
{
  int sock = socket(AF_IPX, SOCK_DGRAM, AF_IPX);
  if (sock > -1) {
    struct ifreq  id;
    struct sockaddr_ipx *sipx = (struct sockaddr_ipx *)&id.ifr_addr;
    memset(&id, 0, sizeof(struct ifreq));
    if (special != IPX_INTERNAL){
      if (devname && *devname) strcpy(id.ifr_name, devname);
      sipx->sipx_type    =  frame;
    } else {
      uint32 xx=htonl(node);
      memcpy(sipx->sipx_node+2, &xx, 4);
    }
    sipx->sipx_network = htonl(netnum);
    sipx->sipx_special = special;
    sipx->sipx_family  = AF_IPX;
    sipx->sipx_action  = IPX_CRTITF;
    x_ioctl(sock, SIOCSIFADDR, &id);
    close(sock);
  }
}
#define add_internal_net(netnum, node) \
  add_special_net(IPX_INTERNAL, NULL, 0, (netnum), (node))

#define add_device_net(devname, frame, netnum) \
  add_special_net(IPX_SPECIAL_NONE, (devname), (frame), (netnum), 0)

#define add_primary_net(devname, frame, netnum) \
  add_special_net(IPX_PRIMARY, (devname), (frame), (netnum), 0)

int init_ipx(uint32 network, uint32 node, int ipx_debug)
{
  int result=-1;
  int sock=sock = socket(AF_IPX, SOCK_DGRAM, AF_IPX);
  if (socket < 0) {
    errorp(0, "EMUTLI:init_ipx", NULL);
    exit(1);
  } else {
    set_sock_debug(sock);
    close(sock);
    result=0;
    /* makes new internal net */
    if (network) {
      del_internal_net();
      add_internal_net(network, node);
      have_ipx_started++;
    }
  }
  return(result);
}

void exit_ipx(int full)
{
  int sock = socket(AF_IPX, SOCK_DGRAM, AF_IPX);
  if (sock > -1) {
    /* Switch DEBUG off */
    locipxdebug = 0;
    set_sock_debug(sock);
    close(sock);
  }
  if (have_ipx_started && full) del_internal_net();
}

int init_dev(char *devname, int frame, uint32 network)
{
   if (!network) return(0);
   del_interface(devname,  frame);
   if (!have_ipx_started) {
     have_ipx_started++;
     del_primary_net();
     add_primary_net(devname, frame, network);
   } else
     add_device_net(devname, frame, network);
   return(0);
}

void exit_dev(char *devname, int frame)
{
  del_interface(devname, frame);
}

void ipx_route_add(uint32  dest_net,
                   uint32  route_net,
                   uint8   *route_node)
{
  struct rtentry  rd;
  int result;
  int sock;
  /* Router */
  struct sockaddr_ipx	*sr = (struct sockaddr_ipx *)&rd.rt_gateway;
  /* Target */
  struct sockaddr_ipx	*st = (struct sockaddr_ipx *)&rd.rt_dst;

  rd.rt_flags = RTF_GATEWAY;

  st->sipx_network = htonl(dest_net);
  sr->sipx_network = htonl(route_net);
  memcpy(sr->sipx_node, route_node, IPX_NODE_SIZE);

  if ( (sock = socket(AF_IPX, SOCK_DGRAM, AF_IPX)) < 0){
    errorp(0, "EMUTLI:ipx_route_add", NULL);
    return;
  }
  sr->sipx_family = st->sipx_family = AF_IPX;

  if ( 0 != (result = x_ioctl(sock, SIOCADDRT, &rd))) {
    switch (errno) {
      case ENETUNREACH:
        errorp(0, "ROUTE ADD", "Router network (%08X) not reachable.\n",
      		     htonl(sr->sipx_network));
      break;

      case EEXIST:
      case EADDRINUSE:
      break;

      default:
        errorp(0, "ROUTE ADD", NULL);
        break;
    }
  }
  close(sock);
}

void ipx_route_del(uint32 net)
{
  struct rtentry  rd;
  int    sock;
  /* Router */
  struct sockaddr_ipx	*sr = (struct sockaddr_ipx *)&rd.rt_gateway;
  /* Target */
  struct sockaddr_ipx	*st = (struct sockaddr_ipx *)&rd.rt_dst;
  rd.rt_flags = RTF_GATEWAY;
  st->sipx_network = htonl(net);
  if ( (sock = socket(AF_IPX, SOCK_DGRAM, AF_IPX)) < 0){
    errorp(0, "EMUTLI:ipx_route_del", NULL);
    return;
  }
  sr->sipx_family = st->sipx_family = AF_IPX;
  x_ioctl(sock, SIOCDELRT, &rd);
  close(sock);
}


int t_open(char *name, int open_mode, char * p)
{
  int opt=1;
  int sock = socket(AF_IPX, SOCK_DGRAM, AF_IPX);
  if (sock < 0) return(sock);
  set_sock_debug(sock);   /* debug switch */

  /* Now allow broadcast */
  if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt,sizeof(opt))==-1){
    errorp(0, "setsockopt SO_BROADCAST", NULL);
    close(sock);
    return(-1);
  }
  return(sock);
}

int t_bind(int sock, struct t_bind *a_in, struct t_bind *a_out)
{
   struct sockaddr_ipx ipxs;
   int maxplen=sizeof(struct sockaddr_ipx);
   memset((char*)&ipxs, 0, sizeof(struct sockaddr_ipx));
   ipxs.sipx_family=AF_IPX;

   if (a_in != (struct t_bind*) NULL
          && a_in->addr.len == sizeof(ipxAddr_t))
     ipx2sockadr(&ipxs, (ipxAddr_t*) (a_in->addr.buf));

   ipxs.sipx_network = 0L;                    /* allways default net */

   memset(ipxs.sipx_node, 0, IPX_NODE_SIZE);  /* allways default node */
                                              /* Hi Volker  :)        */

   if (bind(sock, (struct sockaddr*)&ipxs, sizeof(struct sockaddr_ipx))==-1) {
     errorp(0, "TLI-BIND", "socket Nr:0x%x", (int)GET_BE16(&(ipxs.sipx_port)));
     return(-1);
   }
   if (a_out != (struct t_bind*) NULL) {
     if (getsockname(sock, (struct sockaddr*)&ipxs, &maxplen) == -1){
       errorp(0, "TLI-GETSOCKNAME", NULL);
       return(-1);
     }
     sock2ipxadr((ipxAddr_t*) (a_out->addr.buf), &ipxs);
     XDPRINTF((2,0,"T_BIND ADR=%s", visable_ipx_adr((ipxAddr_t *) a_out->addr.buf ) ));
   }
   return(0);
}

int t_unbind(int sock)
{
  return(0);
}


int t_errno=0;
void t_error(char *s)
{
  errorp(0, "t_error", s);
  t_errno=0;
}

int t_close(int fd)
{
   return(close(fd));
}


int poll( struct pollfd *fds, unsigned long nfds, int timeout)
/* only POLL-IN */
{
   fd_set  readfs;
   /*
   fd_set  writefs;
   fd_set  exceptfs;
   */
   struct pollfd *p = fds;
   struct timeval time_out;
   int  k  = (int)nfds;
   int  result=-1;
   int  high_f=0;
   FD_ZERO(&readfs);
   /*
   FD_ZERO(&writefs);
   FD_ZERO(&exceptfs);
   */
   while (k--){
     if (p->fd > -1 && (p->events & POLLIN)) {
       FD_SET(p->fd, &readfs);
       if (p->fd > high_f) high_f = p->fd;
     }
     p->revents=0;
     p++;
   }
   if (timeout > 1000) {
     time_out.tv_sec    = timeout / 1000;
     time_out.tv_usec   = 0;
   } else {
     time_out.tv_sec    = 0;
     time_out.tv_usec   = timeout*1000;
   }
   if (0 > (result = select(high_f+1, &readfs, NULL, NULL, &time_out)))
     return(-1);
   if (result) {
     int rest=result;
     k = (int)nfds;
     p = fds;
     while (k--){
       if (p->fd > -1 && FD_ISSET(p->fd, &readfs)){
	 p->revents = POLLIN;
	 if (! --rest) break; /* ready */
       }
       p++;
     }
   }
   return(result);
}

int t_rcvudata(int fd, struct t_unitdata *ud, int *flags)
{
   struct sockaddr_ipx ipxs;
   int  sz    = sizeof(struct sockaddr_ipx);
   int result;
   ipxs.sipx_family=AF_IPX;
   if (ud->addr.maxlen < sizeof(ipxAddr_t)) return(-1);
   result = recvfrom(fd, ud->udata.buf, ud->udata.maxlen, 0,
                         (struct sockaddr *) &ipxs, &sz);

   if (result < 0) return(result);
   if (ud->opt.maxlen) {
      *((uint8*)ud->opt.buf) = ipxs.sipx_type;
      ud->opt.len            = 1;
   }
   ud->udata.len=result;
   sock2ipxadr((ipxAddr_t*) (ud->addr.buf), &ipxs);
   ud->addr.len = sizeof(ipxAddr_t);
   return(result);
}
#define HAVE_IPX_SEND_BUG 0
#if HAVE_IPX_SEND_BUG
static int last_fd;
static int new_try;

static void sig_alarm(int rsig)
{
  struct sockaddr_ipx ipxs;
  int    maxplen=sizeof(struct sockaddr_ipx);
  signal(rsig, SIG_IGN);
  XDPRINTF((1, 0, "GOT ALARM SIGNAL in sendto"));
  memset((char*)&ipxs, 0, sizeof(struct sockaddr_ipx));
  ipxs.sipx_family=AF_IPX;
  if (getsockname(last_fd, (struct sockaddr*)&ipxs, &maxplen) != -1){
    int sock;
    int i    = 5;
    while (close(last_fd) == -1 && i--) sleep(1);
    sleep(2);
    sock = socket(AF_IPX, SOCK_DGRAM, AF_IPX);
    if (bind(sock, (struct sockaddr*)&ipxs, sizeof(struct sockaddr_ipx))==-1) {
      errorp(0, "TLI-BIND", "socket Nr:0x%x", (int)GET_BE16(&(ipxs.sipx_port)));
      exit(1);
    }
    if (sock != last_fd) {
      dup2(sock, last_fd);
      close(sock);
    }
    new_try++;
  } else
    errorp(0, "getsockname", NULL);
}
#endif


int t_sndudata(int fd, struct t_unitdata *ud)
{
   int result;
   struct sockaddr_ipx ipxs;
   if (ud->addr.len != sizeof(ipxAddr_t)) return(-1);

#if HAVE_IPX_SEND_BUG
    {
    int anz_tries=3;
    do {
      void (*old_sig)(int rsig) = signal(SIGALRM, sig_alarm);
      new_try  = 0;
      alarm(2);
      last_fd  = fd;
#endif

   memset(&ipxs, 0, sizeof(struct sockaddr_ipx));
   ipxs.sipx_family=AF_IPX;
   ipx2sockadr(&ipxs, (ipxAddr_t*) (ud->addr.buf));
   ipxs.sipx_type    = (ud->opt.len) ? (uint8) *((uint8*)(ud->opt.buf)) : 0;

   result = sendto(fd,(void *)ud->udata.buf,
	  ud->udata.len, 0, (struct sockaddr *) &ipxs, sizeof(ipxs));

#if HAVE_IPX_SEND_BUG
     alarm(0);
     signal(SIGALRM, old_sig);
   } while (new_try && anz_tries--);
   }
#endif

   return(result);
}


int t_rcvuderr(int fd, struct t_uderr *ud)
{
   return(0);
}

