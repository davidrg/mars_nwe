/* emutli1.c 05-May-98 */
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
#include <linux/config.h>
#if 0
# include <linux/sockios.h>
#endif
#include "net.h"
#if 0
# include <linux/if.h>
# include <linux/route.h>
# include <linux/in.h>
#else
# include <net/if.h>
# include <net/route.h>
# include <netinet/in.h>
#endif
#include <errno.h>

static int    have_ipx_started=0;
static int    auto_interfaces=0;
static int    org_auto_interfaces=0;

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

int read_interface_data(uint8* data, uint32 *rnet, uint8 *node,
                          int *flags, uint8 *name)

/* returns frame or -1 for no frame or -2 for error */
{
  uint32 snet;
  int   frame  =-2;
  int   xflags = 0;
  uint8 buff1[200];
  uint8 buff2[200];
  uint8 buff3[200];
  uint8 buff4[200];
  if (!rnet)  rnet =&snet;
  if (!flags) flags=&xflags;
  else *flags=0;
  if (sscanf(data, "%lx %s %s %s %s",
          rnet, buff1, buff2, buff3, buff4) == 5  ) {
    int len = strlen(buff4);
    if (!len) return(-2);
    switch (*(buff4+len-1)) {
      case  '2' : frame = IPX_FRAME_8022; break;
      case  '3' : frame = IPX_FRAME_8023; break;
      case  'P' : frame = IPX_FRAME_SNAP; break;
      case  'I' : frame = IPX_FRAME_ETHERII; break;
#ifdef IPX_FRAME_TR_8022
      case  'R' : frame = IPX_FRAME_TR_8022; break;
#endif
      case  'e' :
      case  'E' : frame = -1; break;   /* NONE */
      default   : return(-2);
    }
    if (node) strmaxcpy(node, buff1, 12);

    upstr(buff2);
    if (!strcmp(buff2, "YES")) /* primary */
      *flags |= 1;

    if (name) strmaxcpy(name, buff3, 20);
    upstr(buff3);
    if (!strcmp(buff3, "INTERNAL")) /* internal net */
      *flags |= 2;
  }
  return(frame);
}

int get_interface_frame_name(char *name, uint32 net)
/* returns frame and name of Device of net */
{
  int frame = -1;
  FILE *f=fopen("/proc/net/ipx_interface", "r");
  if (f) {
    char buff[200];
    while (fgets((char*)buff, sizeof(buff), f) != NULL){
      uint32   rnet;
      uint8 dname[25];
      int fframe = read_interface_data((uint8*) buff, &rnet, NULL, NULL, dname);
      if (fframe < 0) continue;
      if (rnet == net) {
        if (name) strcpy(name, dname);
        frame = fframe;
        break;
      }
    }
    fclose(f);
  }
  return(frame);
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
        char  buff[200];
        uint8 name[25];
        while (fgets((char*)buff, sizeof(buff), f) != NULL){
          int flags = 0;
          int frame = read_interface_data((uint8*) buff, NULL, NULL,
                                     &flags, name);
          if (frame < 0) continue;
          sipx->sipx_type = frame;
          if (flags & 1) { /* primary */
            if (flags & 2){ /* primary == internal net */
              sipx->sipx_special = IPX_INTERNAL;
            } else
              strcpy(id.ifr_name, name);
            break;
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

static void del_all_interfaces_nets(void)
/* removes all ipx_interfaces */
{
  int sock = socket(AF_IPX, SOCK_DGRAM, AF_IPX);
  if (sock > -1) {
    FILE *f=fopen("/proc/net/ipx_interface", "r");
    if (f) {
      char  buff[200];
      uint8 name[25];
      struct ifreq  id;
      struct sockaddr_ipx *sipx = (struct sockaddr_ipx *)&id.ifr_addr;

      while (fgets((char*)buff, sizeof(buff), f) != NULL){
        int flags = 0;
        int frame = read_interface_data((uint8*) buff, NULL, NULL,
                                   &flags, name);
        if (frame < -1) continue;

        memset(&id, 0, sizeof(struct ifreq));
        sipx->sipx_network = 0L;
        sipx->sipx_family  = AF_IPX;

        if (flags & 2) {     /* internal */
          sipx->sipx_special = IPX_INTERNAL;
        } else {
          sipx->sipx_type = frame;
          strcpy(id.ifr_name, name);
          if (flags & 1) /* primary  */
            sipx->sipx_special = IPX_PRIMARY;
          else
            sipx->sipx_special = IPX_SPECIAL_NONE;
        }
        sipx->sipx_action  = IPX_DLTITF;
        x_ioctl(sock, SIOCSIFADDR, &id);
      }
      fclose(f);
    }
    close(sock);
  }
}

#define del_internal_net() \
  del_special_net(IPX_INTERNAL, NULL, 0)
#define del_interface(devname, frame) \
  del_special_net(IPX_SPECIAL_NONE, (devname), (frame))
#define del_primary_net() \
  del_special_net(IPX_PRIMARY, NULL, 0)

static int add_special_net(int special,
  char *devname, int frame, uint32 netnum, uint32 node)
{
  int result = -1;
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
    result = x_ioctl(sock, SIOCSIFADDR, &id);
    close(sock);
  }
  return(result);
}
#define add_internal_net(netnum, node) \
  add_special_net(IPX_INTERNAL, NULL, 0, (netnum), (node))

#define add_primary_net(devname, frame, netnum) \
  add_special_net(IPX_PRIMARY, (devname), (frame), (netnum), 0)

int add_device_net(char *devname, int frame, uint32 netnum) 
{
  return(add_special_net(IPX_SPECIAL_NONE, (devname), (frame), (netnum), 0));
}

int get_frame_name(uint8 *framename, int frame)
{
  char *frname=0;
  switch (frame) {
    case  -1               : frname = "AUTO";       break;
#ifdef IPX_FRAME_TR_8022
    case IPX_FRAME_TR_8022 : frname = "TOKEN";      break;
#endif
    case IPX_FRAME_8022    : frname = "802.2";      break;
    case IPX_FRAME_8023    : frname = "802.3";      break;
    case IPX_FRAME_SNAP    : frname = "SNAP";       break;
    case IPX_FRAME_ETHERII : frname = "ETHERNET_II";break;
    default                : framename[0] = '\0';
                             return(-1);
  } /* switch */
  strcpy(framename, frname);
  return(0);
}

int ipx_inuse(int mode)
/* returns 0 if ipx is idle */
{
  FILE *f=fopen("/proc/net/ipx", "r");
  int idle=0;
  if (f) {
    char buff[200];
    while (fgets((char*)buff, sizeof(buff), f) != NULL){
      uint32 network;
      int     sock;
      if (2==sscanf(buff, "%lx:%x", &network, &sock)) {
        if (mode == 0) {
          if (sock >= 0x4000) { /* user socket */
            idle=sock;
            break;
          }
        } else if (sock==mode) {
          idle=sock;
          break;
        }
      }
    }
    fclose(f);
  }
  return(idle);
}

static void ipx_in_use_abort()
{
  errorp(11, "!! IPX IN USE ERROR !!",
       "mars_nwe would kill existing IPX programs if starting\n"
       "because it must reinit ipx devices.\n"
       "Please stop other IPX programs e.g. ncpmount,\n"
       "or change mars_nwe's configuration.\n"
       );
  exit(1);
}

int init_ipx(uint32 network, uint32 node, int ipx_debug, int flags)
{
  int result=-1;
  int sock;
  uint32 primary_net=0L;
#if INTERNAL_RIP_SAP
# ifdef CONFIG_IPX_INTERN
  errorp(11, "!! configuration error !!",
       "mars_nwe don't run with kernel 'full internal net'.\n"
       "Change kernel option CONFIG_IPX_INTERN=NO (nobody needs it)\n"
       "or use 'ipxd' and change mars_nwe INTERNAL_RIP_SAP=0.");
  exit(1);
# endif
#endif

  if ((sock = socket(AF_IPX, SOCK_DGRAM, AF_IPX)) < 0) {
    errorp(1,  "EMUTLI:init_ipx", NULL);
    errorp(10, "Problem", "probably kernel-IPX is not setup correctly");
    exit(1);
  } else {
    ipx_config_data cfgdata;
    struct sockaddr_ipx ipxs;
    ioctl(sock, SIOCIPXCFGDATA, &cfgdata);
    org_auto_interfaces =
        auto_interfaces = cfgdata.ipxcfg_auto_create_interfaces;
    set_sock_debug(sock);
    result=0;
    
    memset((char*)&ipxs, 0, sizeof(struct sockaddr_ipx));
    ipxs.sipx_port   = htons(SOCK_NCP);
    ipxs.sipx_family = AF_IPX;
    if (bind(sock, (struct sockaddr*)&ipxs,
          sizeof(struct sockaddr_ipx))==-1) {
      if (errno == EEXIST || errno == EADDRINUSE) {
        result = -1;
        errorp(1, "EMUTLI:init_ipx socket 0x451", NULL);
        exit(1);
      }
    } else {
      int maxplen=sizeof(struct sockaddr_ipx);
      if (getsockname(sock, (struct sockaddr*)&ipxs, &maxplen) != -1)
        primary_net= ntohl(ipxs.sipx_network);
      if (primary_net)
        have_ipx_started++;
    }
    /* build new internal net */
    if (network) {
      int diffs = (primary_net && (network != primary_net));
      if (diffs) {
        XDPRINTF((1,0,"Existing primary network will be reinit from %x to %x", 
           primary_net, network));
        if (ipx_inuse(0) && !(flags&4)) 
          ipx_in_use_abort();
      }
      if ((flags&4) || diffs || !primary_net) {  /* if complete reinit or diffs */
        del_internal_net();
        add_internal_net(network, node);
      }
      have_ipx_started++;
    }
    if ((flags & 2) && !auto_interfaces) { /* set auto interfaces */
      auto_interfaces = 1;
      ioctl(sock, SIOCAIPXITFCRT, &auto_interfaces);
    }
    close(sock);
  }
  return(result);
}

void exit_ipx(int flags)
{
  int sock = socket(AF_IPX, SOCK_DGRAM, AF_IPX);
  if (sock > -1) {
    /* Switch DEBUG off */
    set_locipxdebug(0);
    set_sock_debug(sock);
    if (have_ipx_started && !(flags&1))
       org_auto_interfaces = 0;
    if (auto_interfaces != org_auto_interfaces)
      ioctl(sock, SIOCAIPXITFCRT, &org_auto_interfaces);
    close(sock);
  }
  if (flags&4) {
    del_all_interfaces_nets();
  } else if (!ipx_inuse(0)) {
    del_internal_net();
  }
}

int init_dev(char *devname, int frame, uint32 network, int wildmask)
{
   if (!(wildmask & 3))
      del_interface(devname, frame);
   if (!have_ipx_started) {
     if (wildmask) return(-99);
     have_ipx_started++;
     if (!ipx_inuse(0)) {
       del_primary_net(); 
       add_primary_net(devname, frame, network);
     }
   } else {
     if (!wildmask)
       return(add_device_net(devname, frame, network));
     return(1);
   }
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
  struct sockaddr_ipx   *sr = (struct sockaddr_ipx *)&rd.rt_gateway;
  /* Target */
  struct sockaddr_ipx   *st = (struct sockaddr_ipx *)&rd.rt_dst;

  rd.rt_flags = RTF_GATEWAY;

  st->sipx_network = htonl(dest_net);
  sr->sipx_network = htonl(route_net);

  if (route_node)
    memcpy(sr->sipx_node, route_node, IPX_NODE_SIZE);
  else
    memset(sr->sipx_node, 0, IPX_NODE_SIZE);

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
  struct sockaddr_ipx   *sr = (struct sockaddr_ipx *)&rd.rt_gateway;
  /* Target */
  struct sockaddr_ipx   *st = (struct sockaddr_ipx *)&rd.rt_dst;
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
