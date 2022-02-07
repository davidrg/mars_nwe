/* nwroute.c 08-Feb-96 */
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


typedef struct {
  uint32  net;                  /* destnet              */
  uint16  hops;                 /* hops to net          */
  uint16  ticks;                /* ticks to net, ether 1/hop, isdn 7/hop */
  uint32  rnet;                 /* net  of forw. router */
  uint8   rnode[IPX_NODE_SIZE]; /* node of forw. router */
} NW_ROUTES;

static int        anz_routes=0;
static NW_ROUTES *nw_routes[MAX_NW_ROUTES];

typedef struct {
  uint8     *name; /* Server Name      */
  int         typ; /* Server Typ       */
  ipxAddr_t  addr; /* Server Addr      */
  uint32      net; /* routing over NET */
  int        hops;
  int       flags;
} NW_SERVERS;

static int        anz_servers=0;
static NW_SERVERS *nw_servers[MAX_NW_SERVERS];

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
    XDPRINTF((3,0, "NET 0x%x is routed", nw_routes[k]->net));
    if (freeslot < 0 && !nw_routes[k]->net) freeslot=k;
  }

  if (k == anz_routes) {    /* no route slot found */
    if (do_delete) return;  /* nothing to delete   */
    if (freeslot < 0) {
      if (anz_routes == MAX_NW_ROUTES) {
        XDPRINTF((1, 0, "too many routes=%d, increase MAX_NW_ROUTES in config.h", anz_routes));
        return;
      }
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
    if (nr->rnet == rnet && IPXCMPNODE(nr->rnode, rnode) ) {
      /* only delete the routes, which we have inserted */
      XDPRINTF((2,0,"ROUTE DEL NET=0x%x over Router NET 0x%x",
                nr->net, rnet));
      ipx_route_del(nr->net);
      nr->net = 0L;
    } else {
      XDPRINTF((3,0,"ROUTE NOT deleted NET=0x%x, RNET=0x%x",
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


NW_NET_DEVICE *find_netdevice(uint32 network)
/* return the device over which the network is routed, I hope */
{
  uint32 net=network;
  int l=2;
  XDPRINTF((3, 0, "find_netdevice of network=%lX", net));
  while (l--) {
    int  k=-1;
    while (++k < anz_net_devices) {
      NW_NET_DEVICE *nd=net_devices[k];
      if (nd->net == net) {
        XDPRINTF((3, 0, "found netdevive %s, frame=%d, ticks=%d",
          nd->devname, nd->frame, nd->ticks));
        return(nd);
      }
    }
    if (!l) return(NULL);
    k=-1;
    while (++k < anz_routes && nw_routes[k]->net != network);;
    if (k < anz_routes) net=nw_routes[k]->rnet;
    else return(NULL);
  }
  return(NULL);
}

void insert_delete_server(uint8  *name,                 /* Server Name */
                                 int        styp,       /* Server Typ  */
                                 ipxAddr_t *addr,       /* Server Addr */
                                 ipxAddr_t *from_addr,
                                 int        hops,
                                 int        do_delete,  /* delete = 1 */
                                 int        flags)
{
  int         k=-1;
  int         freeslot=-1;
  uint32      net;
  uint8       sname[MAX_SERVER_NAME+2];
  NW_SERVERS *nr=NULL;
  strmaxcpy(sname, name, MAX_SERVER_NAME);
  upstr(sname);
  XDPRINTF((3,0,"%s %s %s,0x%04x",
     visable_ipx_adr(addr),
    (do_delete) ? "DEL" : "INS", sname, (int) styp));
  k=-1;

  if (!*sname) return;

  while (++k < anz_servers && (nw_servers[k]->typ != styp ||
     !nw_servers[k]->name || strcmp(nw_servers[k]->name, sname)) ) {
    if (nw_servers[k]->name) {
      XDPRINTF((10,0, "Server %s = typ=0x%04x",
              nw_servers[k]->name, nw_servers[k]->typ));
    }
    if (freeslot < 0 && !nw_servers[k]->typ) freeslot=k;
  }

  if (k == anz_servers) {   /* server not found    */
    if (do_delete) return;  /* nothing to delete   */

    if (freeslot < 0) {
      if (anz_servers == MAX_NW_SERVERS) {
        XDPRINTF((1, 0, "too many servers=%d, increase MAX_NW_SERVERS in config.h", anz_servers));
        return;
      }
      nw_servers[k] = (NW_SERVERS*)xcmalloc(sizeof(NW_SERVERS));
      anz_servers++;
    } else k=freeslot;
    nr        = nw_servers[k];
    new_str(nr->name, sname);
    nr->typ   = styp;
    nr->hops  = 0xffff;
  } else if (do_delete) {
    nr=nw_servers[k];
#if !FILE_SERVER_INACTIV
    if (!IPXCMPNODE(nr->addr.node, my_server_adr.node) ||
        !IPXCMPNET (nr->addr.net,  my_server_adr.net) )
#endif
    {
      ins_del_bind_net_addr(nr->name, nr->typ, NULL);
      xfree(nr->name);
      memset(nr, 0, sizeof(NW_SERVERS));
    }
    return;
  } else nr=nw_servers[k];
  /* here now i perhaps must change the entry */
  if (nr->hops > 16 || memcmp(&(nr->addr), addr, sizeof(ipxAddr_t))) {
    ins_del_bind_net_addr(nr->name, nr->typ, addr);
    memcpy(&(nr->addr), addr, sizeof(ipxAddr_t));
#if !FILE_SERVER_INACTIV
    if (IPXCMPNODE(from_addr->node, my_server_adr.node) &&
        IPXCMPNET (from_addr->net,  my_server_adr.net)
        && GET_BE16(from_addr->sock) == SOCK_SAP) {
      hops = 0;
    }
#endif
  }
  if (hops <= nr->hops && 0 != (net = GET_BE32(from_addr->net)) ) {
    nr->net  = net;
    nr->hops = hops;
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

static void build_rip_buff(uint32 destnet, int to_internal_net)
/* to_internal_net = request from dosemu etc. */
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
#if 0
    if ( (is_wild || nr->net == destnet) &&
      (rmode==1 || nr->hops < 2 || to_internal_net) )
#else
    if (is_wild || (nr->net == destnet))
#endif
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
#if 0
        uint32   net     = GET_BE32(p);
#endif
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

static void send_rip_broadcast(int mode)
/* mode=0, standard broadcast */
/* mode=1, first trie         */
/* mode=2, shutdown	      */
{
  int k=-1;
  while (++k < anz_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (nd->ticks < 7) { /* isdn devices should not get RIP broadcasts everytime */
      init_rip_buff(nd->net, (mode == 2) ? 1 : 0);
      build_rip_buff(MAX_U32, 0);
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
      build_rip_buff(net, GET_BE32(from_addr->net)==internal_net);
      if (net == MAX_U32) break;
    }
    p+=8;
  }
  if (!is_response)  /* rip request */
    send_rip_buff(from_addr);
}

/* <========================= SAP ============================> */
void send_server_response(int respond_typ,
                                 int styp, ipxAddr_t *to_addr)
/* respond_typ 2 = general, 4 = nearest service respond */
{
  IPX_DATA   ipx_data;
  int           j=-1;
  int        tics=99;
  int        hops=15;
  int        entry = -1;
  memset(&ipx_data, 0, sizeof(ipx_data.sip));
  while (++j < anz_servers) {
    NW_SERVERS *nw=nw_servers[j];
    if (nw->typ == styp && nw->name && *(nw->name)) {
      int xtics=999;
      if (nw->net != internal_net) {
        NW_NET_DEVICE *nd=find_netdevice(nw->net);
        if (nd) xtics = nd->ticks;
      } else xtics =0;
      if (xtics < tics || (xtics == tics && nw->hops <= hops)) {
        tics  = xtics;
        hops  = nw->hops;
        entry = j;
      }
    }
  }
  if (entry > -1) {
    NW_SERVERS *nw=nw_servers[entry];
    strcpy((char*)ipx_data.sip.server_name, nw->name);
    memcpy(&ipx_data.sip.server_adr, &nw->addr, sizeof(ipxAddr_t));
    XDPRINTF((4, 0, "NEAREST SERVER=%s, typ=0x%x, tics=%d, hops=%d",
                  nw->name, styp, tics, hops));
    U16_TO_BE16(respond_typ, ipx_data.sip.response_type);
    U16_TO_BE16(styp, ipx_data.sip.server_type);
    U16_TO_BE16(hops, ipx_data.sip.intermediate_networks);
    send_ipx_data(sockfd[SAP_SLOT],
                       4,  /* this is the official packet typ for SAP's */
	               sizeof(ipx_data.sip),
	               (char *)&(ipx_data.sip),
	               to_addr, "Nearest Server Response");
  }
}

static void send_sap_broadcast(int mode)
/* mode=0, standard broadcast */
/* mode=1, first trie         */
/* mode=2, shutdown	      */
{
  int k=-1;
  while (++k < anz_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (nd->ticks < 7 || mode) {
    /* isdn devices should not get SAP broadcasts everytime */
      IPX_DATA      ipx_data;
      ipxAddr_t     wild;
      int           j=-1;
      memset(&wild, 0, sizeof(ipxAddr_t));
      U32_TO_BE32(nd->net,    wild.net);
      memset(wild.node, 0xFF, IPX_NODE_SIZE);
      U16_TO_BE16(SOCK_SAP,   wild.sock);
      while (++j < anz_servers) {
        NW_SERVERS *nw=nw_servers[j];
        if (!nw->typ || (nw->net == nd->net && nw->hops)
                     || (mode == 2 && nw->hops) ) continue; /* no SAP to this NET */
        memset(&ipx_data, 0, sizeof(ipx_data.sip));
        strcpy(ipx_data.sip.server_name, nw->name);
        memcpy(&ipx_data.sip.server_adr, &(nw->addr), sizeof(ipxAddr_t));
        U16_TO_BE16(2,       ipx_data.sip.response_type);  /* General    */
        U16_TO_BE16(nw->typ, ipx_data.sip.server_type);    /* Fileserver */
        if (mode == 2) {
          U16_TO_BE16(16,         ipx_data.sip.intermediate_networks);
        } else {
          U16_TO_BE16(nw->hops+1, ipx_data.sip.intermediate_networks);
         /* I hope hops are ok here */
          XDPRINTF((3, 0, "SEND SIP %s,0x%04x, hops=%d",
                   nw->name, nw->typ, nw->hops+1));
        }
        send_ipx_data(sockfd[SAP_SLOT],
                       4,  /* this is the official packet typ for SAP's */
	               sizeof(ipx_data.sip),
	               (char *)&(ipx_data.sip),
	               &wild, "SIP Broadcast");
      }
    }
  }
}

static FILE *open_route_info_fn(void)
{
  static int tacs=0;
  FILE   *f=NULL;
  if (print_route_tac > 0) {
    if (!tacs) {
      if (NULL != (f=fopen(pr_route_info_fn,
                           (print_route_mode) ? "w" : "a"))) {
        tacs = print_route_tac-1;
      } else print_route_tac=0;
    } else tacs--;
  }
  return(f);
}

void send_sap_rip_broadcast(int mode)
/* mode=0, standard broadcast */
/* mode=1, first trie         */
/* mode=2, shutdown	      */
{
static int flipflop=0;
  if (mode) {
    send_sap_broadcast(mode);
    send_rip_broadcast(mode);
  } else {
    if (flipflop) {
      send_rip_broadcast(mode);
      flipflop=0;
    } else {
      send_sap_broadcast(mode);
      flipflop=1;
    }
  }
  if (!mode && flipflop) { /* jedes 2. mal */
    FILE *f= open_route_info_fn();
    if (f) {
      int k=-1;
      fprintf(f, "<--------- Routing Table ---------->\n");
      fprintf(f, "%8s Hops Tics %9s Router Node\n", "Network", "RouterNet");
      while (++k < anz_routes) {
        NW_ROUTES *nr = nw_routes[k];
        if (nr->net) {
          fprintf(f, "%08lX %4d %4d  %08lX %02x:%02x:%02x:%02x:%02x:%02x\n",
                   nr->net, nr->hops, nr->ticks, nr->rnet,
                   (int)nr->rnode[0], (int)nr->rnode[1], (int)nr->rnode[2],
                   (int)nr->rnode[3], (int)nr->rnode[4], (int)nr->rnode[5]);
        }
      }
      k=-1;
      fprintf(f, "<--------- Server Table ---------->\n");
      fprintf(f, "%-20s %4s %9s Hops Server-Address\n","Name", "Typ", "RouterNet");
      while (++k < anz_servers) {
        NW_SERVERS *ns = nw_servers[k];
        if (ns->typ) {
          char sname[50];
          strmaxcpy(sname, ns->name, 20);
          fprintf(f, "%-20s %4d  %08lX %4d %s\n", sname, ns->typ,
               ns->net, ns->hops, xvisable_ipx_adr(&(ns->addr), 1));
        }
      } /* while */
      fclose(f);
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
    if (nd->ticks < 7) query_sap_on_net(nd->net); /* only fast routes */
  }
  if (!anz_net_devices) query_sap_on_net(internal_net);
}


int dont_send_wdog(ipxAddr_t *addr)
/* returns != 0 if tics are to high for wdogs */
{
  NW_NET_DEVICE *nd;
  if (!wdogs_till_tics) return(0);         /*  ever send wdogs */
  else if (wdogs_till_tics < 0) return(1); /* never send wdogs */
  if (NULL != (nd=find_netdevice(GET_BE32(addr->net))))
    return((nd->ticks < wdogs_till_tics) ? 0 : 1);
  return(0);
}

