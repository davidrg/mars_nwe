/* nwroute.c 08-Feb-98 */
/* (C)opyright (C) 1993,1998  Martin Stover, Marburg, Germany
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
static int        max_nw_routes=0;
static NW_ROUTES **nw_routes=NULL;

typedef struct {
  uint8     *name; /* Server Name      */
  int         typ; /* Server Typ       */
  ipxAddr_t  addr; /* Server Addr      */
  uint32      net; /* routing over NET */
  int        hops;
  int       flags;
} NW_SERVERS;

static int        anz_servers=0;
static int        max_nw_servers=0;
static NW_SERVERS **nw_servers=NULL;

#define NEEDS_UPDATE_SAP        1
#define NEEDS_UPDATE_SAP_QUERY  2
#define NEEDS_UPDATE_RIP        4
#define NEEDS_UPDATE_RIP_NET    8
#define NEEDS_UPDATE_ALL        (8|4|2|1)

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

  while (++k < count_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if  (nd->is_up) {
      if (nd->net == destnet) {
        if (!do_delete) return; /* don't alter device */
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
      if (anz_routes == max_nw_routes) {
        int new_max_nw = max_nw_routes+5;
        NW_ROUTES **new_nwr
             =(NW_ROUTES**)xcmalloc(new_max_nw*sizeof(NW_ROUTES*));
        if (max_nw_servers)
          memcpy(new_nwr, nw_routes, max_nw_routes*sizeof(NW_ROUTES*));
        xfree(nw_routes);
        nw_routes=new_nwr;
        max_nw_routes=new_max_nw;
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
        init_dev(nd_dev->devname, nd_dev->frame, nd_dev->net, 0);
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
    while (++k < count_net_devices) {
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
  while (++k < count_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (nd->is_up && nd->net == net) return(nd);
  }
  return(NULL);
}

int activate_slow_net(uint32 net)
{
  NW_NET_DEVICE *nd=find_device_by_net(net);
  if (nd && nd->ticks > 6) { /* if 'slow net' */
    if (acttime_stamp > nd->updated_time + 600) {
      nd->needs_update=NEEDS_UPDATE_ALL;
      nd->updated_time=acttime_stamp;
      return(1);
    }
  }
  return(0);
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
      if (anz_servers == max_nw_servers) {
        int new_max_nw = max_nw_servers+5;
        NW_SERVERS **new_nws
             =(NW_SERVERS**)xcmalloc(new_max_nw*sizeof(NW_SERVERS*));
        if (max_nw_servers)
          memcpy(new_nws, nw_servers, max_nw_servers*sizeof(NW_SERVERS*));
        xfree(nw_servers);
        nw_servers=new_nws;
        max_nw_servers=new_max_nw;
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
#if !IN_NWROUTED
      ins_del_bind_net_addr(nr->name, nr->typ, NULL);
#endif
      xfree(nr->name);
      memset(nr, 0, sizeof(NW_SERVERS));
    }
    return;
  } else nr=nw_servers[k];

  /* here now i perhaps must change the entry */
  if (nr->hops > 16 || memcmp(&(nr->addr), addr, sizeof(ipxAddr_t))) {
    memcpy(&(nr->addr), addr, sizeof(ipxAddr_t));
#if !IN_NWROUTED
    ins_del_bind_net_addr(nr->name, nr->typ, addr);
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
static int       max_rip_entries=0;
static uint8     *rip_buff=NULL;

static void init_rip_buff(uint32 net, int mode)
{
  rnet     = net;
  rentries = 0;
  rmode    = mode;
  if (!rip_buff) {
    max_rip_entries=10;
    rip_buff=xcmalloc(2+max_rip_entries*8);
  }
  U16_TO_BE16((mode > 9) ? 1 : 2, rip_buff);  /* rip request or response */
}

static void ins_rip_buff(uint32 net, uint16 hops, uint16 ticks)
{
  if (!net) return;
  if (net != rnet || (!rentries && net == internal_net)) {
    uint8 *p;
    if (rentries >= max_rip_entries) {
      int    new_rip_entries=max_rip_entries+5;
      uint8 *new_ripbuf=xcmalloc(2 + new_rip_entries*8);
      if (max_rip_entries)
        memcpy(new_ripbuf, rip_buff, 2 + max_rip_entries*8);
      xfree(rip_buff);
      rip_buff=new_ripbuf;
      max_rip_entries=new_rip_entries;
    }
    p=rip_buff+2+(rentries*8);
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
    while (++k < count_net_devices) {
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
#if DO_DEBUG
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
#endif
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
/* mode=4, only to needs update*/
{
  int k=-1;
  while (++k < count_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (mode == 4 && !(nd->needs_update&NEEDS_UPDATE_RIP)) continue;
    if (nd->is_up && (nd->ticks < 7
      || mode || (nd->needs_update&NEEDS_UPDATE_RIP) )) {
      /* isdn devices should not get RIP broadcasts everytime */
      nd->needs_update&=~NEEDS_UPDATE_RIP;
      init_rip_buff(nd->net, (mode == 2) ? 1 : 0);
      build_rip_buff(MAX_U32);
      send_rip_buff(NULL);
    }
  }
}

void rip_for_net(uint32 net)
{
  int k=-1;
  while (++k < count_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (nd->is_up && (nd->ticks < 7
      || (nd->needs_update&NEEDS_UPDATE_RIP_NET) ) ) {
      /* isdn devices should not get RIP broadcasts everytime */
      nd->needs_update&=~NEEDS_UPDATE_RIP_NET;
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
/* It can be a RIP Request or a RIP Response     */
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
    XDPRINTF((4, 0, "%s SERVER=%s, typ=0x%x, ticks=%d, hops=%d, sock=0x%x",
             (respond_typ==4) ? "NEAREST" : "GENERAL", nw->name,
                  nw->typ, ticks, hops, (int)GET_BE16(nw->addr.sock)));
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
  int j     = -1;
  int ticks = 99;
  int hops  = 15;
  int entry = -1;
  while (ticks && ++j < anz_servers) {
    NW_SERVERS *nw=nw_servers[j];
    if (nw->typ == styp && nw->name && *(nw->name)) {
      int xticks=999;
      if (nw->net != internal_net) {
        NW_NET_DEVICE *nd=find_netdevice(nw->net);
        if (nd) xticks = nd->ticks;
      } else xticks = 0;
      if (xticks < ticks || (xticks == ticks && nw->hops <= hops)) {
        ticks = xticks;
        hops  = nw->hops;
        entry = j;
      }
    }
  }
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

#ifdef CONFIG_C_LEASING
    if (nd_ticks > 6
        && nw->typ != 0x0004
        && nw->typ != 0x0107
        && nw->typ != 0x023f
        && nw->typ != 0x027b
        && nw->typ != 0x044c) continue;
#endif

    send_sap_to_addr(j,   (mode == 2) ? 16 : nw->hops+1,
                             nd_ticks,
                             2,     /* General    */
                             &wild);
  } /* while */
}

static void send_sap_broadcast(int mode)
/* mode=0, standard broadcast */
/* mode=1, first trie         */
/* mode=2, shutdown           */
/* mode=4, only update*/
{
  int k=-1;
  while (++k < count_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (mode == 4 && !(nd->needs_update&NEEDS_UPDATE_SAP)) continue;
    if (nd->is_up && (nd->ticks < 7 ||
      (nd->needs_update&NEEDS_UPDATE_SAP) || mode)) {
    /* isdn devices should not get SAP broadcasts everytime */
       nd->needs_update &= ~NEEDS_UPDATE_SAP;
       send_sip_to_net(nd->net, nd->ticks, mode);
    }
  }
}

static FILE *open_route_info_fn(int force, FILE *ff, int section)
{
  static int tacs=0;
  FILE   *f=NULL;
  if (section>1 && !(print_route_mode&2))
     return(ff);
  if (print_route_tac > 0) {
    if (!tacs || force) {
      char fnbuf[300];
      char *fn;
      if (print_route_mode&2) {
        sprintf(fnbuf, "%s.%d", pr_route_info_fn, section);
        fn = fnbuf;
      } else {
        fn=pr_route_info_fn;
      }
      f=fopen(fn, (print_route_mode&0x1) ? "w" : "a");
      if (section == 1) {
        if (NULL != f)
          tacs = print_route_tac-1;
        else {
          print_route_tac=0;
          errorp(0, "route info", "Openerror of `%s`", fn);
        }
      } else {
        if (NULL != f)
          fclose(ff);
        else
          f = ff; /* we use old file */
      }
    } else tacs--;
  }
  return(f);
}

void print_routing_info(int force)
{
  FILE *f= open_route_info_fn(force, NULL, 1);
  if (f) {
    int k=-1;
    int i;
    time_t xtime;
    time(&xtime);
    fprintf(f, "%s", ctime(&xtime) );
    fprintf(f, "<--------- %d Devices ---------------->\n", count_net_devices);
    fprintf(f, "%-15s %-15s %5s  Network Status\n", "DevName", "Frame", "Ticks");
    while (++k < count_net_devices) {
      uint8 frname[30];
      NW_NET_DEVICE *nd=net_devices[k];
      (void) get_frame_name(frname, nd->frame);
      fprintf(f, "%-15s %-15s %5d %08lX %s\n",
        nd->devname, frname, nd->ticks, nd->net,
              (!nd->is_up) ? "DOWN"
                           : ( (nd->is_up==1) ? "UP"
                                              : "ADDED") );
    }
    if (print_route_mode&2) {
      f= open_route_info_fn(1, f, 2);
      fprintf(f, "%s", ctime(&xtime) );
    }
    i=0;
    k=-1;
    while (++k < anz_routes) {
      NW_ROUTES *nr = nw_routes[k];
      if (nr->net) i++;
    }
    fprintf(f, "<--------- %d Routes ---------->\n", i);
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
    if (print_route_mode&2) {
      f= open_route_info_fn(1, f, 3);
      fprintf(f, "%s", ctime(&xtime) );
    }
    i=0;
    k=-1;
    while (++k < anz_servers) {
      NW_SERVERS *ns = nw_servers[k];
      if (ns->typ) i++;
    }
    fprintf(f, "<--------- %d Servers ---------->\n", i);
    fprintf(f, "%-20s %4s %9s Hops Server-Address\n","Name", "Typ", "RouterNet");
    k=-1;
    while (++k < anz_servers) {
      NW_SERVERS *ns = nw_servers[k];
      if (ns->typ) {
        char sname[50];
        strmaxcpy(sname, ns->name, 20);
        fprintf(f, "%-20s %4x  %08lX %4d %s\n", sname, ns->typ,
             ns->net, ns->hops, xvisable_ipx_adr(&(ns->addr), 1));
      }
    } /* while */
    fclose(f);
  }
}

static int look_for_interfaces(void);


void send_sap_rip_broadcast(int mode)
/* mode=0, standard broadcast  */
/* mode=1, first trie          */
/* mode=2, shutdown            */
/* mode=3, update routes       */
/* mode=4, resend to net       */
/* mode=5, force update routes */
{
static int flipflop=1;
  int force_print_routes=(mode == 1) ? 1 : 0;
  if (mode == 5) {
    force_print_routes++;
    mode = 3;
  }
  if (auto_detect_interfaces)
    force_print_routes += look_for_interfaces();
  if (mode) {
    if (mode == 3) {
      if (force_print_routes) {
        send_rip_broadcast(1);
        send_sap_broadcast(1);
      }
    } else {
      send_rip_broadcast(mode);
      send_sap_broadcast(mode);
    }
  } else {
    if (flipflop) {
      send_rip_broadcast(mode);
      flipflop=0;
    } else {
      send_sap_broadcast(mode);
      flipflop=1;
    }
  }
  if (flipflop || force_print_routes)
    print_routing_info(force_print_routes); /* every second time */
}

static void sap_find_nearest_server(uint32 net)
/* searches for the nearest server on network net */
{
  SQP               sqp;
  ipxAddr_t         wild;
  memset(&wild, 0,  sizeof(ipxAddr_t));
  memset(wild.node, 0xFF, IPX_NODE_SIZE);
  U32_TO_BE32(net,      wild.net);
  U16_TO_BE16(SOCK_SAP, wild.sock);
  U16_TO_BE16(3,        sqp.query_type);   /* 3 nearest Server Query   */
  U16_TO_BE16(4,        sqp.server_type);  /* file server */
  send_ipx_data(sockfd[SAP_SLOT], 17, sizeof(SQP),
               (char*)&sqp, &wild, "SERVER Query");
}

void get_servers(void)
{
  int k=-1;
  while (++k < count_net_devices) {
    NW_NET_DEVICE *nd=net_devices[k];
    if (nd->is_up && (nd->ticks < 7
      || nd->needs_update&NEEDS_UPDATE_SAP_QUERY)){
      nd->needs_update &= ~NEEDS_UPDATE_SAP_QUERY;
      sap_find_nearest_server(nd->net);
    }
  }
  if (!count_net_devices) sap_find_nearest_server(internal_net);
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

void realloc_net_devices(void)
{
  int new_max_netd=max_net_devices+2;
  NW_NET_DEVICE **new_nd=(NW_NET_DEVICE**)
         xcmalloc(new_max_netd*sizeof(NW_NET_DEVICE*));
  if (max_net_devices)
    memcpy(new_nd, net_devices, max_net_devices*sizeof(NW_NET_DEVICE*));
  xfree(net_devices);
  net_devices=new_nd;
  max_net_devices=new_max_netd;
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
  while (++k < count_net_devices) {
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
    while (++k < count_net_devices) {
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
    NW_NET_DEVICE **pnd;
    int matched=0;
    k=-1;

    if (count_net_devices >= max_net_devices)
      realloc_net_devices();

    while (++k < count_net_devices) {
      nd = net_devices[k];
      if (nd->wildmask&3) {
        int dfound = !strcmp(nd->devname, rnetdevname);
        int ffound = nd->frame == rnetframe;
        if ( (dfound && ffound) || (dfound && (nd->wildmask&2) )
            || (ffound && (nd->wildmask&1))) {
          pnd=&(net_devices[count_net_devices++]);
          *pnd= (NW_NET_DEVICE*)xcmalloc(sizeof(NW_NET_DEVICE));
          (*pnd)->wildmask = nd->wildmask;
          (*pnd)->ticks    = nd->ticks;
          matched++;
          nd=*pnd;
          break;
        }
      }
    }
    if (!matched) return(0);
  } else {
    nd = net_devices[foundfree];
  }
  nd->net   = rnet;
  nd->frame = rnetframe;
  new_str(nd->devname, rnetdevname);
  nd->is_up = 2;
  nd->needs_update=NEEDS_UPDATE_ALL;
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
      init_dev(nd->devname, nd->frame, nd->net, 0);
      break;
    }
  }
  return(1);
}

static int look_for_interfaces(void)
{
  FILE *f=fopen("/proc/net/ipx_interface", "r");
  int  find_diffs=0;
  if (f) {
    char buff[200];
    NW_NET_DEVICE *nd;
    int  k = -1;

    while (++k < count_net_devices) {
      nd=net_devices[k];
      if (nd->is_up == 2) nd->is_up = -2; /* this will be put DOWN */
    }

    while (fgets((char*)buff, sizeof(buff), f) != NULL){
      uint32   rnet;
      uint8    dname[25];
      int      flags;
      int fframe = read_interface_data((uint8*) buff, &rnet, NULL, &flags, dname);
      if (fframe < 0) continue;
      if (rnet > 0L && !(flags & 2)) { /* not internal */
        int found=0;
        k=-1;
        while (++k < count_net_devices) {
          nd=net_devices[k];
          if (nd->net == rnet) {
            found++;
            break;
          }
        }
        if (found && nd->is_up) {
          if (nd->is_up == -2) nd->is_up=2; /* reset  */
        } else find_diffs+=test_ins_device_net(rnet);
      }
    }
    fclose(f);

    k = -1;
    while (++k < count_net_devices) {
      nd=net_devices[k];
      if (nd->is_up < 0) {
        int j;
        find_diffs++;
        nd->is_up = 0; /* this will be put DOWN */
        XDPRINTF((1,0,"Device %s net=0x%x removed",
          nd->devname ? nd->devname : "?", nd->net));
        for (j=0; j < anz_routes; j++){
          NW_ROUTES *nr=nw_routes[j];
          if (nr && nr->rnet == nd->net) {
            nr->net = 0L; /* remove route */
            XDPRINTF((1,0,"Route to net=0x%x removed", nr->net));
          }
        }
        if (nd->wildmask & 1)
          new_str(nd->devname, "*");
        if (nd->wildmask & 2)
          nd->frame = -1;
        if (nd->wildmask & 4)
          nd->net = 0;
      }
    }
  }
  return(find_diffs);
}
