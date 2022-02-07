#define DO_IPX_SEND_TEST 1
/* emutli.c 28-Apr-96 */
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/config.h>
#include <linux/sockios.h>
#include "net.h"
#include <linux/if.h>
#include <linux/route.h>
#include <linux/in.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

static int locipxdebug=0;

void set_locipxdebug(int debug)
{
  locipxdebug = debug;
}

void set_sock_debug(int sock)
{
  if (setsockopt(sock, SOL_SOCKET, SO_DEBUG, &locipxdebug, sizeof(int))==-1){
    errorp(0, "setsockopt SO_DEBUG", NULL);
  }
}

void sock2ipxadr(ipxAddr_t *i, struct sockaddr_ipx *so)
{
   memcpy(i->net,  &so->sipx_network, IPX_NET_SIZE + IPX_NODE_SIZE);
   memcpy(i->sock, &so->sipx_port, 2);
}

void ipx2sockadr(struct sockaddr_ipx *so, ipxAddr_t *i)
{
   memcpy(&so->sipx_network, i->net, IPX_NET_SIZE + IPX_NODE_SIZE);
   memcpy(&so->sipx_port,    i->sock, 2);
}

void set_emu_tli(void)
{
  int i = get_ini_int(100);
  if (i > -1) locipxdebug = i;
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


#if DO_IPX_SEND_TEST
static int new_try;
static int anz_tries;
static struct t_unitdata *test_ud;

static void sig_alarm(int rsig)
{
  signal(rsig, SIG_IGN);
  XDPRINTF((0, 0,"GOT ALARM try=%d, sendto=%s",
        anz_tries+1, visable_ipx_adr((ipxAddr_t *) test_ud->addr.buf) ));
  if (anz_tries++ < 10) new_try++;
}
#endif


int t_sndudata(int fd, struct t_unitdata *ud)
{
   int result;
   struct sockaddr_ipx ipxs;
   if (ud->addr.len != sizeof(ipxAddr_t)) return(-1);

#if DO_IPX_SEND_TEST
    {
    anz_tries=1;
    test_ud  =ud;
    do {
      void (*old_sig)(int rsig) = signal(SIGALRM, sig_alarm);
      new_try  = 0;
      alarm(1+anz_tries);
#endif
   memset(&ipxs, 0, sizeof(struct sockaddr_ipx));
   ipxs.sipx_family=AF_IPX;
   ipx2sockadr(&ipxs, (ipxAddr_t*) (ud->addr.buf));
   ipxs.sipx_type    = (ud->opt.len) ? (uint8) *((uint8*)(ud->opt.buf)) : 0;

   result = sendto(fd,(void *)ud->udata.buf,
          ud->udata.len, 0, (struct sockaddr *) &ipxs, sizeof(ipxs));

#if DO_IPX_SEND_TEST
     alarm(0);
     signal(SIGALRM, old_sig);
   } while (new_try);
   }
#endif

   return(result);
}


int t_rcvuderr(int fd, struct t_uderr *ud)
{
   return(0);
}

