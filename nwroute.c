/* nwroute.c 12-May-96 */
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
  uint32  net;                  /* destnet               */
  uint16  hops;                 /* hops to net over rnet */
  uint16  ticks;                /* ticks to net, ether 1/hop, isdn 7/hop */
  uint32  rnet;                 /* net  of forw. router  */
  uint8   rnode[IPX_NODE_SIZE]; /* node of forw. router  */
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
  int        k          = -1;
  int        freeslot   = -1;
  NW_ROUTES *nr         = NULL;
  NW_NET_DEVICE *nd_dev = NULL;
  int       ndticks     = 99;

  XDPRINTF((3,0,"Beg: %s net:0x%X, over 0x%X, 0x%02x:%02x:%02x:%02x:%02x:%02x",
    (do_delete) ? "DEL" : "INS", destnet, rnet,
    (int)rnode[0], (int)rnode[1], (int)rnode[2],
    (int)rnode[3], (int)rnode[4], (int)rnode[5]));

  if (!destnet || destnet == internal_net) return;

  while (++k < anz_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if  (nd->is_up) {
      if (nd->net == destnet) {
        if (!do_delete) return; /* don't route device */
        nd_dev = nd;
      }
      if (nd->net == rnet)    ndticks=nd->ticks;
    }
  }
  if (!do_delete && nd_dev && nd_dev->ticks <= ndticks) return;

  k=-1;
  while (++k < anz_routes && nw_routes[k]->net != destnet) {
     if (freeslot < 0 && !nw_routes[k]->net) freeslot=k;
  }

  if (k == anz_routes) {    /* no route slot found */
    if (do_delete) return;  /* nothing to delete   */
    if (freeslot < 0) {
      if (anz_routes == MAX_NW_ROUTES) {
        XDPRINTF((1, 0, "too many routes > %d, increase MAX_NW_ROUTES in config.h", anz_routes));
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
      if (nd_dev != NULL) { /* this is net to our device */
        /* I must delete and setup new, because there is */
        /* no direct way to delete this route from interface :( */
        exit_dev(nd_dev->devname, nd_dev->frame);
        init_dev(nd_dev->devname, nd_dev->frame, nd_dev->net);
      }
      nr->net = 0L;
    } else {
      XDPRINTF((3,0,"ROUTE NOT deleted NET=0x%x, RNET=0x%x",
                nr->net, rnet));
    }
    return;
  } else nr=nw_routes[k];

  ticks+=ndticks;
  if (ticks <= nr->ticks) {
    if (ticks == nr->ticks && hops >= nr->hops) return;
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
  while (l--) {
    int  k=-1;
    while (++k < anz_net_devices) {
      NW_NET_DEVICE *nd=net_devices[k];
      if (nd->is_up && nd->net == net) {
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

static NW_NET_DEVICE *find_device_by_net(uint32 net)
/* return the device of this net I hope */
{
  int    k=-1;
  while (++k < anz_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (nd->is_up && nd->net == net) return(nd);
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
        XDPRINTF((1, 0, "too many servers > %d, increase MAX_NW_SERVERS in config.h", anz_servers));
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

#if !IN_NWROUTED
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
#if !IN_NWROUTED
    if (IPXCMPNODE(from_addr->node, my_server_adr.node) &&
        IPXCMPNET (from_addr->net,  my_server_adr.net)
        && GET_BE16(from_addr->sock) == SOCK_SAP) {
      hops = 0;
    }
#endif
  }
  if (hops <= nr->hops && 0 != (net = GET_BE32(from_addr->net)) ) {
    if (nr->net && nr->net != net && nr->hops >= hops) {
      NW_NET_DEVICE *nrd=find_device_by_net(nr->net);
      NW_NET_DEVICE *nnd=find_device_by_net(net);
      if (nrd && nnd && nrd->ticks < nnd->ticks) return;
    }
    nr->net  = net;
    nr->hops = hops;
  }
}

static uint32    rnet=0L;      /* Router NET                       */
static int       rmode;        /* 0=normal, 1=shutdown response    */
                               /* 10=request                       */

static int       rentries=0;
static uint8     rip_buff[2 + MAX_RIP_ENTRIES * 8];
                     /* operation + max. 50 RIPS */

static void init_rip_buff(uint32 net, int mode)
{
  rnet     = net;
  rentries = 0;
  rmode    = mode;
  U16_TO_BE16((mode > 9) ? 1 : 2, rip_buff);  /* rip request or response */
}

static void ins_rip_buff(uint32 net, uint16 hops, uint16 ticks)
{
  if (!net) return;
  if (net != rnet || (!rentries && net == internal_net)) {
    if (rentries < MAX_RIP_ENTRIES) {
      uint8  *p=rip_buff+2+(rentries*8);
      U32_TO_BE32(net,   p);
      U16_TO_BE16(hops,  p+4);
      U16_TO_BE16(ticks, p+6);
      rentries++;
    } else {
      XDPRINTF((1, 0, "too many rips > %d, increase MAX_RIP_ENTRIES in config.h", MAX_RIP_ENTRIES));
    }
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
      if (nd->is_up && (is_wild || nd->net == destnet))
        ins_rip_buff(nd->net, (rmode==1) ? 16 : 1, nd->ticks+1);
    }
  }

  k=-1;
  while (++k < anz_routes) {
    NW_ROUTES *nr=nw_routes[k];
    if (nr->rnet != rnet && (is_wild || (nr->net == destnet)) )
      ins_rip_buff(nr->net, (rmode==1) ? 16 : nr->hops+1, nr->ticks+1);
  }
}

static void send_rip_buff(ipxAddr_t *from_addr)
{
  while (rentries > 0) {
    int entries  = min(rentries, 50);
    int datasize = (entries*8)+2;
    ipxAddr_t  to_addr;
    rentries    -= entries;
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
          (operation==1) ? "Request" : "Response", entries));
      p+=2;
      while (entries--) {
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

    if (rentries > 0)
      memcpy(rip_buff+2, rip_buff+2+50*8, min(50, rentries)*8);
  } /* while */
  rentries=0;
}

static void send_rip_broadcast(int mode)
/* mode=0, standard broadcast */
/* mode=1, first trie         */
/* mode=2, shutdown           */
{
  int k=-1;
  while (++k < anz_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (nd->is_up && nd->ticks < 7) { /* isdn devices should not get RIP broadcasts everytime */
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
    if (nd->is_up && nd->ticks < 7) { /* isdn devices should not get RIP broadcasts everytime */
      init_rip_buff(nd->net, 10);
      ins_rip_buff(net, MAX_U16, MAX_U16);
      send_rip_buff(NULL);
    }
  }
}

void handle_rip(int fd,       int ipx_pack_typ,
                int data_len, IPX_DATA *ipxdata,
                ipxAddr_t     *from_addr)

/* All received rip packets reach this function  */
/* It can be a RIP Request or a RIP Respons      */
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
         from_addr->node,  hops, ticks, (hops > 15) ? 1 : 0);
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
static void send_sap_to_addr(int entry, int hops, int ticks,
                            int respond_typ, ipxAddr_t *to_addr)
{
  if (entry > -1) {
    IPX_DATA   ipx_data;
    NW_SERVERS *nw=nw_servers[entry];
    memset(&ipx_data, 0, sizeof(ipx_data.sip));
    strcpy((char*)ipx_data.sip.server_name, nw->name);
    memcpy(&ipx_data.sip.server_adr, &nw->addr, sizeof(ipxAddr_t));
    XDPRINTF((4, 0, "%s SERVER=%s, typ=0x%x, ticks=%d, hops=%d",
             (respond_typ==4) ? "NEAREST" : "GENERAL", nw->name,
                  nw->typ, ticks, hops));
    U16_TO_BE16(respond_typ, ipx_data.sip.response_type);
    U16_TO_BE16(nw->typ,     ipx_data.sip.server_type);
    U16_TO_BE16(hops,        ipx_data.sip.intermediate_networks);
    send_ipx_data(sockfd[SAP_SLOT],
                       4,  /* this is the official packet typ for SAP's */
                       sizeof(ipx_data.sip),
                       (char *)&(ipx_data.sip),
                       to_addr, "Sap Server Response");
  }
}

void send_server_response(int respond_typ,
                              int styp, ipxAddr_t *to_addr)
/* respond_typ 2 = general, 4 = nearest service respond */
{
  int        j=-1;
  int        ticks=99;
  int        hops=15;
  int        entry = -1;
  int  to_internal = (!no_internal)
          && (GET_BE32(to_addr->net) == internal_net)
          && (GET_BE16(to_addr->sock) != SOCK_SAP);
  while (++j < anz_servers) {
    NW_SERVERS *nw=nw_servers[j];
    if (nw->typ == styp && nw->name && *(nw->name)) {
      int xticks=999;
      if (nw->net != internal_net) {
        NW_NET_DEVICE *nd=find_netdevice(nw->net);
        if (nd) xticks = nd->ticks;
        if (to_internal)
          send_sap_to_addr(j, nw->hops+1, xticks, respond_typ, to_addr);
      } else xticks = 0;
      if (xticks < ticks || (xticks == ticks && nw->hops <= hops)) {
        ticks  = xticks;
        hops  = nw->hops;
        entry = j;
      }
    }
  }
  if (!to_internal)
    send_sap_to_addr(entry, hops+1, ticks, respond_typ, to_addr);
}


static void send_sip_to_net(uint32 nd_net, int nd_ticks, int mode)
{
  ipxAddr_t     wild;
  int           j=-1;
  memset(&wild, 0, sizeof(ipxAddr_t));
  U32_TO_BE32(nd_net,    wild.net);
  memset(wild.node, 0xFF, IPX_NODE_SIZE);
  U16_TO_BE16(SOCK_SAP,   wild.sock);
  while (++j < anz_servers) {
    NW_SERVERS *nw=nw_servers[j];
    if  ( !nw->typ                           /* server has no typ       */
     || ( nw->net == nd_net && nw->hops)     /* server has same net but */
                                             /* hops                    */
     || ( mode == 2 && nw->hops) ) {         /* no SAP to this NET      */
      XDPRINTF((3, 0, "No SAP mode=%d, to net=0x%lx for server '%s'",
             mode, nd_net, nw->name));
      continue;
    }
    send_sap_to_addr(j,   (mode == 2) ? 16 : nw->hops+1,
                             nd_ticks,
                             2,     /* General    */
                             &wild);
  }
}

static void send_sap_broadcast(int mode)
/* mode=0, standard broadcast */
/* mode=1, first trie         */
/* mode=2, shutdown           */
{
  int k=-1;
  while (++k < anz_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (nd->is_up && (nd->ticks < 7 || mode)) {
    /* isdn devices should not get SAP broadcasts everytime */
       send_sip_to_net(nd->net, nd->ticks, mode);
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

void print_routing_info(void)
{
  FILE *f= open_route_info_fn();
  if (f) {
    int k=-1;
    fprintf(f, "<--------- Devices ---------------->\n");
    fprintf(f, "%-15s %-15s %5s  Network Status\n", "DevName", "Frame", "Ticks");
    while (++k < anz_net_devices) {
      uint8 frname[30];
      NW_NET_DEVICE *nd=net_devices[k];
      (void) get_frame_name(frname, nd->frame);
      fprintf(f, "%-15s %-15s %5d %08lX %s\n",
        nd->devname, frname, nd->ticks, nd->net,
              (!nd->is_up) ? "DOWN"
                           : ( (nd->is_up==1) ? "UP"
                                              : "ADDED") );
    }
    fprintf(f, "<--------- Routing Table ---------->\n");
    fprintf(f, "%8s Hops Ticks %9s Router Node\n", "Network", "RouterNet");
    k=-1;
    while (++k < anz_routes) {
      NW_ROUTES *nr = nw_routes[k];
      if (nr->net) {
        fprintf(f, "%08lX %4d %5d  %08lX %02x:%02x:%02x:%02x:%02x:%02x\n",
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

void send_sap_rip_broadcast(int mode)
/* mode=0, standard broadcast */
/* mode=1, first trie         */
/* mode=2, shutdown           */
{
static int flipflop=1;
  if (mode) {
    send_rip_broadcast(mode);
    send_sap_broadcast(mode);
  } else {
    if (flipflop) {
      send_rip_broadcast(mode);
      flipflop=0;
    } else {
      send_sap_broadcast(mode);
      flipflop=1;
    }
  }
  if (flipflop) print_routing_info(); /* every second time */
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
    if (nd->is_up && nd->ticks < 7)
        query_sap_on_net(nd->net); /* only fast routes */
  }
  if (!anz_net_devices) query_sap_on_net(internal_net);
}


int dont_send_wdog(ipxAddr_t *addr)
/* returns != 0 if ticks are to high for wdogs */
{
  NW_NET_DEVICE *nd;
  if (!wdogs_till_tics) return(0);         /*  ever send wdogs */
  else if (wdogs_till_tics < 0) return(1); /* never send wdogs */
  if (NULL != (nd=find_netdevice(GET_BE32(addr->net))))
    return((nd->ticks < wdogs_till_tics) ? 0 : 1);
  return(0);
}

/* ---------------------------------------------------- */
int test_ins_device_net(uint32 rnet)
{
  int   rnetframe;
  uint8 rnetdevname[100];
  int   k = -1;
  int   foundfree=-1; /* first matching/free entry */
  NW_NET_DEVICE *nd;
  if (!rnet || rnet == internal_net) return(0);
  while (++k < anz_net_devices) {
    nd=net_devices[k];
    if (!nd->is_up) {
      if (nd->net == rnet) {
        foundfree = k;
        break;
      } else if (foundfree < 0 && !nd->net)
        foundfree = k;
    } else if (nd->net == rnet) return(0);
  }
  if ((rnetframe=get_interface_frame_name(rnetdevname, rnet)) < 0)
    return(0);

  if (foundfree > -1 && (net_devices[foundfree])->net != rnet) {
    int devfound   = -1;
    int framefound = -1;
    k = foundfree - 1;
    foundfree      = -1;
    while (++k < anz_net_devices) {
      nd = net_devices[k];
      if (!nd->is_up && !nd->net) {
        int dfound = !strcmp(nd->devname, rnetdevname);
        int ffound = nd->frame == rnetframe;
        if (dfound && ffound) {
          devfound   = k;
          framefound = k;
          break;
        } else {
          if (dfound) {
            if (devfound   < 0 && nd->frame < 0)
              devfound  =k;
          } else if (ffound) {
            if (framefound < 0 && nd->devname[0] == '*')
              framefound=k;
          } else if (nd->frame < 0 && nd->devname[0] == '*') {
            if (foundfree < 0)
              foundfree = k;
          }
        }
      }
    }
    if (devfound > -1)        foundfree = devfound;
    else if (framefound > -1) foundfree = framefound;
  }

  if ( foundfree < 0 ) {
    if (anz_net_devices < MAX_NET_DEVICES) {
      NW_NET_DEVICE **pnd=&(net_devices[anz_net_devices++]);
      nd=*pnd= (NW_NET_DEVICE*)xmalloc(sizeof(NW_NET_DEVICE));
      memset(nd, 0, sizeof(NW_NET_DEVICE));
      nd->ticks  = 1;
    } else {
      XDPRINTF((1, 0, "too many devices > %d, increase MAX_NET_DEVICES in config.h", anz_net_devices));
      return(0);
    }
  } else {
    nd = net_devices[foundfree];
  }
  nd->net   = rnet;
  nd->frame = rnetframe;
  new_str(nd->devname, rnetdevname);
  nd->is_up = 2;
  /* now perhaps i must delete an existing route over */
  /* another device */

  k = -1;
  while (++k < anz_routes) {
    NW_ROUTES *nr = nw_routes[k];
    if (nr->net == rnet) {
      ipx_route_del(nr->net);
      nr->net = 0L;
      /* I must delete and setup new, because there is */
      /* no direct way to delete this route from interface :( */
      exit_dev(nd->devname, nd->frame);
      init_dev(nd->devname, nd->frame, nd->net);
      break;
    }
  }
  return(1);
}
