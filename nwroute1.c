/* nwroute1.c 08-Feb-96 */
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

/* this should be removed one time if there is a good */
/* sapdaemon <-> nwserv handling      	       	      */
typedef struct {
  uint8     *name; /* Server Name      */
  int         typ; /* Server Typ       */
  ipxAddr_t  addr; /* Server Addr      */
} NW_SERVERS;

static int        anz_servers=0;
static NW_SERVERS *nw_servers[MAX_NW_SERVERS];

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
  XDPRINTF((3, 0, "%s %s %s,0x%04x",
     visable_ipx_adr(addr),
    (do_delete) ? "DEL" : "INS", sname, (int) styp));
  k=-1;

  if (!*sname) return;
  while (++k < anz_servers && (nw_servers[k]->typ != styp ||
     !nw_servers[k]->name || strcmp((char*)nw_servers[k]->name, (char*)sname)) ) {
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
    memset(&(nr->addr), 0, sizeof(ipxAddr_t));
  } else if (do_delete) {
    nr=nw_servers[k];
    if (!IPXCMPNODE(nr->addr.node, my_server_adr.node) ||
        !IPXCMPNET (nr->addr.net,  my_server_adr.net) )  {
      ins_del_bind_net_addr(nr->name, nr->typ, NULL);
      xfree(nr->name);
      memset(nr, 0, sizeof(NW_SERVERS));
    }
    return;
  } else nr=nw_servers[k];
  /* here now i perhaps must change the entry */
  if (memcmp(&(nr->addr), addr, sizeof(ipxAddr_t))) {
    ins_del_bind_net_addr(nr->name, nr->typ, addr);
    memcpy(&(nr->addr), addr, sizeof(ipxAddr_t));
  }
}

void rip_for_net(uint32 net)
{
;
}

void handle_rip(int fd,       int ipx_pack_typ,
	        int data_len, IPX_DATA *ipxdata,
	        ipxAddr_t     *from_addr)
{
;
}


void send_server_response(int respond_typ,
                                 int styp, ipxAddr_t *to_addr)
{
 ;; /* dummy */
}

void get_servers(void)
{
#if 1
  SQP               sqp;
  ipxAddr_t         wild;
  memset(&wild, 0,  sizeof(ipxAddr_t));
#ifdef xxxLINUX
  U32_TO_BE32(internal_net, wild.net);
  memcpy(wild.node, my_server_adr.node, IPX_NODE_SIZE);
#else
  memset(wild.node, 0xFF, IPX_NODE_SIZE);
#endif
  U16_TO_BE16(SOCK_SAP, wild.sock);
  U16_TO_BE16(1,        sqp.query_type);
  U16_TO_BE16(4,        sqp.server_type);
  send_ipx_data(sockfd[SAP_SLOT], 17, sizeof(SQP),
              (char*)&sqp, &wild, "SERVER Query");
#endif
}

void print_routing_info(int force)
{
;; /* DUMMY */
}

void send_sap_rip_broadcast(int mode)
/* mode=0, standard broadcast */
/* mode=1, first trie         */
/* mode=2, shutdown	      */
{
  IPX_DATA    ipx_data;
  ipxAddr_t   wild;
  memset(&wild, 0, sizeof(ipxAddr_t));
#ifdef xxxLINUX
  U32_TO_BE32(internal_net, wild.net);
  memcpy(wild.node, my_server_adr.node, IPX_NODE_SIZE);
#else
  memset(wild.node, 0xFF, IPX_NODE_SIZE);
#endif
  U16_TO_BE16(SOCK_SAP,   wild.sock);
  memset(&ipx_data, 0, sizeof(ipx_data.sip));
  strcpy((char *)ipx_data.sip.server_name, my_nwname);
  memcpy(&ipx_data.sip.server_adr, &my_server_adr, sizeof(ipxAddr_t));
  U16_TO_BE16(SOCK_NCP, ipx_data.sip.server_adr.sock);
  U16_TO_BE16(2,        ipx_data.sip.response_type);
  U16_TO_BE16(4,        ipx_data.sip.server_type);
  U16_TO_BE16(mode == 2 ? 16 : 0, ipx_data.sip.intermediate_networks);
  send_ipx_data(sockfd[SAP_SLOT],
                 4,  /* this is the official packet typ for SAP's */
                 sizeof(ipx_data.sip),
                 (char *)&(ipx_data.sip),
                 &wild, "SIP Broadcast");
  if (!mode) get_servers();
  if (mode == 1) {
    U16_TO_BE16(SOCK_SAP,   wild.sock);
    memset(&ipx_data, 0, sizeof(ipx_data.sip));
    strcpy((char *)ipx_data.sip.server_name, my_nwname);
    memcpy(&ipx_data.sip.server_adr, &my_server_adr, sizeof(ipxAddr_t));
    U16_TO_BE16(SOCK_NCP, ipx_data.sip.server_adr.sock);
    U16_TO_BE16(2,        ipx_data.sip.response_type);
    U16_TO_BE16(4,        ipx_data.sip.server_type);
    U16_TO_BE16(mode == 2 ? 16 : 0, ipx_data.sip.intermediate_networks);
  }
}

int dont_send_wdog(ipxAddr_t *addr)
/* returns != 0 if tics are to high for wdogs */
{
  return(0);
}
