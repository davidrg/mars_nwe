/* nwroute.c 24-Dec-95 */
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

#include "net.h"
#include "nwserv.h"

#define MAX_NW_ROUTES  50       /* max. 1 complete RIP packet should be enough */

typedef struct {
  uint32  net;                  /* destnet              */
  uint16  hops;                 /* hops to net          */
  uint16  ticks;                /* ticks to net, ether 1/hop, isdn 7/hop */

  uint32  rnet;                 /* net  of forw. router */
  uint8   rnode[IPX_NODE_SIZE]; /* node of forw. router */
} NW_ROUTES;

static int        anz_routes=0;
static NW_ROUTES *nw_routes[MAX_NW_ROUTES];

static void insert_delete_net(uint32 destnet,
                              uint32 rnet,      /* routernet  */
                              uint8  *rnode,    /* routernode */
                              uint16 hops,
                              uint16 ticks,
                              int    do_delete) /* delete == 1 */
{
  int  k=-1;
  int  freeslot=-1;
  NW_ROUTES *nr=NULL;

  XDPRINTF((3,0,"%s net:0x%X, over 0x%X, 0x%02x:%02x:%02x:%02x:%02x:%02x",
    (do_delete) ? "DEL" : "INS", destnet, rnet,
    (int)rnode[0], (int)rnode[1], (int)rnode[2],
    (int)rnode[3], (int)rnode[4], (int)rnode[5]));

  if (!destnet || destnet == internal_net) return;

  while (++k < anz_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (nd->net == destnet && (do_delete || (nd->ticks <= ticks))) return;
  }

  k=-1;
  while (++k < anz_routes && nw_routes[k]->net != destnet) {
    XDPRINTF((3,0, "NET 0x%X is routed", nw_routes[k]->net));
    if (freeslot < 0 && !nw_routes[k]->net) freeslot=k;
  }

  if (k == anz_routes) {    /* no route slot found */
    if (do_delete) return;  /* nothing to delete   */
    if (freeslot < 0) {
      if (anz_routes == MAX_NW_ROUTES) return;
      nw_routes[k] = (NW_ROUTES*)xmalloc(sizeof(NW_ROUTES));
      anz_routes++;
    } else k=freeslot;
    nr=nw_routes[k];
    memset(nr, 0, sizeof(NW_ROUTES));
    nr->net   = destnet;
    nr->ticks = 0xffff;
    nr->hops  = 0xffff;
  } else if (do_delete) {
    nr=nw_routes[k];
    if (nr->rnet == rnet &&
        IPXCMPNODE(nr->rnode, rnode) ) {
      /* only delete the routes, which we have inserted */
      XDPRINTF((2,0,"ROUTE DEL NET=0x%X over Router NET 0x%X",
                nr->net, rnet));
      ipx_route_del(nr->net);
      nr->net = 0L;
    } else {
      XDPRINTF((3,0,"ROUTE NOT deleted NET=0x%X, RNET=0X%X",
                nr->net, rnet));
    }
    return;
  } else nr=nw_routes[k];
  if (ticks <= nr->ticks) {
    if (ticks > nr->ticks) return;
    if (ticks == nr->ticks && hops > nr->hops) return;
    nr->hops  = hops;
    nr->ticks = ticks;
    nr->rnet  = rnet;
    memcpy(nr->rnode, rnode, IPX_NODE_SIZE);
    XDPRINTF((2,0,"ADD ROUTE NET=0x%X, over 0x%X, 0x%02x:%02x:%02x:%02x:%02x:%02x",
         nr->net, nr->rnet,
         (int)nr->rnode[0], (int)nr->rnode[1], (int)nr->rnode[2],
         (int)nr->rnode[3], (int)nr->rnode[4], (int)nr->rnode[5]));
    ipx_route_add(nr->net, nr->rnet, nr->rnode);
  }
}

static uint32    rnet=0L;
static int       rentries=0;
static int       rmode; /* 0=normal, 1=shutdown, 10=request */
static uint8     rip_buff[402]; /* operation + max. 50 RIPS */

static void init_rip_buff(uint32 net, int mode)
{
  rnet=net;
  rentries=0;
  rmode=mode;
  U16_TO_BE16((mode > 9) ? 1 : 2, rip_buff);  /* rip request or response */
}

static void ins_rip_buff(uint32 net, uint16 hops, uint16 ticks)
{
  if ( net && rentries < 50 &&
    (net != rnet || (!rentries && net == internal_net))) {
    uint8  *p=rip_buff+2+(rentries*8);
    U32_TO_BE32(net,   p);
    U16_TO_BE16(hops,  p+4);
    U16_TO_BE16(ticks, p+6);
    rentries++;
  }
}

static void build_rip_buff(uint32 destnet)
{
  int    is_wild     = (destnet==MAX_U32);
  int    is_response = (rmode < 10);
  int    k;

  if (!destnet) return;
  if (is_wild)  rentries=0;

  if (is_response) {
    if (is_wild || internal_net == destnet) {
      ins_rip_buff(internal_net, (rmode==1) ? 16 : 1,
                       (rnet==internal_net) ?  1 : 2);
    }
    k=-1;
    while (++k < anz_net_devices) {
      NW_NET_DEVICE *nd=net_devices[k];
      if (is_wild || nd->net == destnet)
        ins_rip_buff(nd->net, (rmode==1) ? 16 : 1, nd->ticks+1);
    }
  }
  k=-1;
  while (++k < anz_routes) {
    NW_ROUTES *nr=nw_routes[k];
    if ((is_wild || nr->net == destnet) && rmode==1 || nr->hops < 2)
      ins_rip_buff(nr->net, (rmode==1) ? 16 : nr->hops, nr->ticks);
  }
}

static void send_rip_buff(ipxAddr_t *from_addr)
{
  if (rentries > 0) {
    int datasize=(rentries*8)+2;
    ipxAddr_t  to_addr;
    if (from_addr) memcpy(&to_addr, from_addr, sizeof(ipxAddr_t));
    else {
      memset(&to_addr, 0, sizeof(ipxAddr_t));
      U32_TO_BE32(rnet, to_addr.net);
      memset(to_addr.node, 0xFF, IPX_NODE_SIZE);
      U16_TO_BE16(SOCK_RIP, to_addr.sock);
    }

    if (nw_debug) {
      uint8    *p   = rip_buff;
      int operation = GET_BE16(p);
      XDPRINTF((2,0, "Send Rip %s entries=%d",
          (operation==1) ? "Request" : "Response", rentries));
      p+=2;
      while (rentries--) {
        uint32   net     = GET_BE32(p);
        uint16   hops    = GET_BE16(p+4);
        uint16   ticks   = GET_BE16(p+6);
        XDPRINTF((2,0, "hops=%3d, ticks %3d, network:%02x.%02x.%02x.%02x",
           (int)hops, (int)ticks, (int)*(p), (int)*(p+1), (int)*(p+2), (int)*(p+3)));
        p+=8;
      }
    }

    send_ipx_data(sockfd[RIP_SLOT], 1,
                    datasize,
                    (char *)rip_buff,
                    &to_addr, "SEND RIP");
    rentries=0;
  }
}

void send_rip_broadcast(int mode)
/* mode=0, standard broadcast */
/* mode=1, first trie         */
/* mode=2, shutdown	      */
{
  int k=-1;
  while (++k < anz_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (nd->ticks < 7) { /* isdn devices should not get RIP broadcasts everytime */
      init_rip_buff(nd->net, (mode == 2) ? 1 : 0);
      build_rip_buff(MAX_U32);
      send_rip_buff(NULL);
    }
  }
}

void rip_for_net(uint32 net)
{
  int k=-1;
  while (++k < anz_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (nd->ticks < 7) { /* isdn devices should not get RIP broadcasts everytime */
      init_rip_buff(nd->net, 10);
      ins_rip_buff(net, MAX_U16, MAX_U16);
      send_rip_buff(NULL);
    }
  }
}

void handle_rip(int fd,       int ipx_pack_typ,
	        int data_len, IPX_DATA *ipxdata,
	        ipxAddr_t     *from_addr)
{
  int operation    = GET_BE16(ipxdata->rip.operation);
  int entries      = (data_len-2) / 8;
  uint8    *p      = ((uint8*)ipxdata)+2;
  int  is_response = operation==2;

  XDPRINTF((2,0, "Got Rip %s entries=%d from: %s",
       (!is_response) ? "Request" : "Response", entries,
           visable_ipx_adr(from_addr)));

  if (!is_response) {
    if (operation != 1) {
      XDPRINTF((1,0, "UNKNOWN RIP operation %d", operation));
      return;
    }
    init_rip_buff(GET_BE32(from_addr->net), 0);
  }

  while (entries--) {
    uint32   net     = GET_BE32(p);
    uint16   hops    = GET_BE16(p+4);
    uint16   ticks   = GET_BE16(p+6);
    XDPRINTF((2,0,"hops=%3d, ticks %3d, network:%02x.%02x.%02x.%02x",
       (int)hops, (int)ticks, (int)*(p), (int)*(p+1), (int)*(p+2), (int)*(p+3)));

    if (is_response) {
      insert_delete_net(net, GET_BE32(from_addr->net),
         from_addr->node,  hops+1, ticks+1, (hops > 15) ? 1 : 0);
    } else { /* rip request */
      build_rip_buff(net);
      if (net == MAX_U32) break;
    }
    p+=8;
  }
  if (!is_response)  /* rip request */
    send_rip_buff(from_addr);
}

/* <========================= SAP ============================> */
void send_sap_broadcast(int mode)
/* mode=0, standard broadcast */
/* mode=1, first trie         */
/* mode=2, shutdown	      */
{
  int k=-1;
  while (++k < anz_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (nd->ticks < 7 || mode) { /* isdn devices should not get SAP broadcasts everytime */
      IPX_DATA      ipx_data;
      ipxAddr_t     wild;
      memset(&wild, 0, sizeof(ipxAddr_t));

      U32_TO_BE32(nd->net,    wild.net);
      memset(wild.node, 0xFF, IPX_NODE_SIZE);
      U16_TO_BE16(SOCK_SAP,   wild.sock);

      memset(&ipx_data, 0, sizeof(ipx_data.sip));
      strcpy(ipx_data.sip.server_name, my_nwname);
      memcpy(&ipx_data.sip.server_adr, &my_server_adr, sizeof(ipxAddr_t));
      U16_TO_BE16(SOCK_NCP, ipx_data.sip.server_adr.sock);
         /* use NCP SOCKET */

      U16_TO_BE16(2,    ipx_data.sip.response_type);  /* General    */
      U16_TO_BE16(4,    ipx_data.sip.server_type);    /* Fileserver */

      if (mode == 2) {
        U16_TO_BE16(16, ipx_data.sip.intermediate_networks);
      } else {
        U16_TO_BE16(1,  ipx_data.sip.intermediate_networks);
       /* I hope 1 is ok here */
      }
      send_ipx_data(sockfd[MY_BROADCAST_SLOT], 0,
	             sizeof(ipx_data.sip),
	             (char *)&(ipx_data.sip),
	             &wild, "SIP Broadcast");
    }
  }
}


static void query_sap_on_net(uint32 net)
/* searches for the next server on this network */
{
  SQP               sqp;
  ipxAddr_t         wild;
  memset(&wild, 0,  sizeof(ipxAddr_t));
  memset(wild.node, 0xFF, IPX_NODE_SIZE);
  U32_TO_BE32(net,      wild.net);
  U16_TO_BE16(SOCK_SAP, wild.sock);
  U16_TO_BE16(3,        sqp.query_type);
  U16_TO_BE16(4,        sqp.server_type);
  send_ipx_data(sockfd[SAP_SLOT], 17, sizeof(SQP),
       (char*)&sqp, &wild, "SERVER Query");
}

void get_servers(void)
{
  int k=-1;
  while (++k < anz_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (nd->ticks < 7)  query_sap_on_net(nd->net); /* only fast routes */
  }
  if (!anz_net_devices) query_sap_on_net(internal_net);
}


