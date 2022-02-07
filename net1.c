/* net1.c,  20-Mar-96 */

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

#if HAVE_TLI
void print_t_info(struct t_info *t)
{
  XDPRINTF((2,0, "T_INFO:addr %ld, options %ld, tsdu %ld, etsdu %ld",
	 t->addr, t->options,t->tsdu, t->etsdu));
  XDPRINTF((2,0, "connect %ld, discon %ld, servtype %ld",
	 t->connect, t->discon,t->servtype));
#if 0
   struct t_info {
         long addr;      /* size of protocol address                */
         long options;   /* size of protocol options                */
         long tsdu;      /* size of max transport service data unit */
         long etsdu;     /* size of max expedited tsdu              */
         long connect;   /* max data for connection primitives      */
         long discon;    /* max data for disconnect primitives      */
         long servtype;  /* provider service type                   */
#endif
}
#endif

char *xvisable_ipx_adr(ipxAddr_t *p, int modus)
{
static char str[200];
  if (p) {
    if (!modus) {
    sprintf(str,"net=%x:%x:%x:%x, node=%x:%x:%x:%x:%x:%x, sock=%02x:%02x",
       (int)p->net[0],  (int)p->net[1],  (int)p->net[2],  (int)p->net[3],
       (int)p->node[0], (int)p->node[1], (int)p->node[2], (int)p->node[3],
       (int)p->node[4], (int)p->node[5], (int)p->sock[0], (int)p->sock[1]);
    } else if (modus== 1) {
      sprintf(str,"%02X:%02X:%02X:%02X,%02x:%02x:%02x:%02x:%02x:%02x,%02x:%02x",
         (int)p->net[0],  (int)p->net[1],  (int)p->net[2],  (int)p->net[3],
         (int)p->node[0], (int)p->node[1], (int)p->node[2], (int)p->node[3],
         (int)p->node[4], (int)p->node[5], (int)p->sock[0], (int)p->sock[1]);
    } else strcpy(str, "??");
  } else
    strcpy(str, "net=UNKOWN(NULLPOINTER)");
  return(str);
}

void print_ipx_addr(ipxAddr_t *p)
{
  XDPRINTF((2,0,"%s", visable_ipx_adr(p)));
}

void print_ud_data(struct t_unitdata *ud)
{
  int packet_typ = *(int*)(ud->opt.buf);
  int data_len   = ud->udata.len;
  IPX_DATA *ipxdata = (IPX_DATA *)(ud->udata.buf);
  XDPRINTF((2,0,"DATA-LEN %d, PACKET-TYPE %d von: %s",
	data_len,  packet_typ, visable_ipx_adr((ipxAddr_t *)(ud->addr.buf))));
      /* hierin steht nun Addresse u. Node des Senders */
  if (packet_typ == PACKT_CORE) {
    XDPRINTF((2,0, "Query Type 0x%x, Server Type 0x%xd",
       GET_BE16(ipxdata->sqp.query_type),
       GET_BE16(ipxdata->sqp.server_type)));
  } else  if (data_len > sizeof(SIP)){
    SAP   *sap      = &(ipxdata->sap);
    SAPS  *saps     = &(ipxdata->sap.saps);
    int sap_operation = GET_BE16(sap->sap_operation);
    XDPRINTF((2,0, "SAP-OPERATION %d", sap_operation));
    while (data_len >= sizeof(SAPS)){
      XDPRINTF((2,0, "Name:%s:, typ:0x%x",saps->server_name,
	   GET_BE16(saps->server_type)));
      print_ipx_addr(&(saps->server_adr));
      saps++;
      data_len  -= sizeof(SAPS);
    }
  } else print_ipx_data(ipxdata);
}

void print_ipx_data(IPX_DATA *p)
{
  print_sip_data(&(p->sip));
}

void print_sip_data(SIP *sip)
{
   XDPRINTF((2,0,"Name:%s:,response_type:0x%x,server_typ:0x%x",sip->server_name,
      GET_BE16(sip->response_type), GET_BE16(sip->server_type)));
   print_ipx_addr(&(sip->server_adr));
}

void adr_to_ipx_addr(ipxAddr_t *p, char *s)
{
  int net0, net1, net2, net3;
  int node0, node1, node2, node3, node4, node5;
  int sock0, sock1;
  sscanf(s, "%x.%x.%x.%x:%x.%x.%x.%x.%x.%x:%x.%x",
	  &net0, &net1, &net2, &net3,
	  &node0, &node1, &node2, &node3, &node4, &node5,
	  &sock0, &sock1);
    p->net[0]  = net0;
    p->net[1]  = net1;
    p->net[2]  = net2;
    p->net[3]  = net3;
    p->node[0] = node0;
    p->node[1] = node1;
    p->node[2] = node2;
    p->node[3] = node3;
    p->node[4] = node4;
    p->node[5] = node5;
    p->sock[0] = sock0;
    p->sock[1] = sock1;
}

void ipx_addr_to_adr(char *s, ipxAddr_t *p)
{
  sprintf(s, "%x.%x.%x.%x:%x.%x.%x.%x.%x.%x:%x.%x",
	     (int)p->net[0] ,
	     (int)p->net[1] ,
	     (int)p->net[2] ,
	     (int)p->net[3] ,
	     (int)p->node[0],
	     (int)p->node[1],
	     (int)p->node[2],
	     (int)p->node[3],
	     (int)p->node[4],
	     (int)p->node[5],
	     (int)p->sock[0],
	     (int)p->sock[1]);
}


int send_ipx_data(int fdx, int pack_typ,
	              int data_len, char *data,
	              ipxAddr_t *to_addr, char *comment)
{
  int                fd=fdx;
  int                result=0;
  struct             t_unitdata ud;
  uint8              ipx_pack_typ = (uint8) pack_typ;
  ud.opt.len       = sizeof(ipx_pack_typ);
  ud.opt.maxlen    = sizeof(ipx_pack_typ);
  ud.opt.buf       = (char*)&ipx_pack_typ;
  ud.addr.len      = sizeof(ipxAddr_t);
  ud.addr.maxlen   = sizeof(ipxAddr_t);
  ud.udata.buf     = (char*)data;
  ud.udata.len     = data_len;
  ud.udata.maxlen  = data_len;
  ud.addr.buf      = (char*)to_addr;
  if (comment != NULL) XDPRINTF((2,0,"%s TO: ", comment));
  if (nw_debug > 1) print_ipx_addr(to_addr);
  if (fd < 0) {
    struct t_bind   bind;
    ipxAddr_t       addr;
    fd=t_open("/dev/ipx", O_RDWR, NULL);
    if (fd < 0) {
      t_error("t_open !Ok");
      return(-1);
    }
    memset(&addr,0, sizeof(ipxAddr_t));
    bind.addr.len    = sizeof(ipxAddr_t);
    bind.addr.maxlen = sizeof(ipxAddr_t);
    bind.addr.buf    = (char*)&addr;
    bind.qlen        = 0; /* ever */
    if (t_bind(fd, &bind, &bind) < 0){
      t_error("t_bind in send_ipx_data");
      t_close(fd);
      return(-1);
    }
  }
  if ((result=t_sndudata(fd, &ud)) < 0){
    if (nw_debug > 1) t_error("t_sndudata !OK");
  }
  if (fdx < 0 && fd > -1) {
    t_unbind(fd);
    t_close(fd);
  }
  return(result);
}

#if 0
int get_ipx_addr(ipxAddr_t *addr)
{
  int fd=t_open("/dev/ipx", O_RDWR, NULL);
  struct t_optmgmt optb;
  int result = -1;
  if (fd < 0) {
    t_error("t_open !Ok");
    return(-1);
  }
  optb.opt.maxlen = optb.opt.len = sizeof(ipxAddr_t);
  optb.opt.buf    = (char*)addr;
  optb.flags      = 0;
  result = t_optmgmt(fd, &optb, &optb);
  if (result < 0) t_error("t_optmgmt !Ok");
  else result=0;
  t_close(fd);
  return(result);
}
#else

int get_ipx_addr(ipxAddr_t *addr)
{
  int fd=t_open("/dev/ipx", O_RDWR, NULL);
  struct t_bind   bind;
  int result = -1;
  if (fd < 0) {
    t_error("t_open !Ok");
    return(-1);
  }
  bind.addr.len    = sizeof(ipxAddr_t);
  bind.addr.maxlen = sizeof(ipxAddr_t);
  bind.addr.buf    = (char*)addr;
  bind.qlen        = 0; /* ever */
  memset(addr, 0, sizeof(ipxAddr_t));
  if (t_bind(fd, &bind, &bind) < 0)
    t_error("tbind:get_ipx_addr");
  else {
    result=0;
    t_unbind(fd);
  }
  t_close(fd);
  return(result);
}
#endif
