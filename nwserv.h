/* nwserv.h 10-Apr-97 */
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
#ifndef _M_NWSERV_H_
#define _M_NWSERV_H_
extern uint32    internal_net;        /* NETWORKNUMMER INTERN (SERVER) */
extern int       no_internal;         /* no use of internal net        */
extern int       auto_detect_interfaces;
extern ipxAddr_t my_server_adr;       /* Address of this server        */
extern char      my_nwname[50];       /* Name of this server           */
extern int       print_route_tac;     /* every x broadcasts print it   */
extern int       print_route_mode;    /* append                        */
extern char      *pr_route_info_fn;   /* filename                      */
extern int       wdogs_till_tics;

typedef struct {
  char     *devname;   /* "eth0" or "isdnX"  or ??    */
  int      frame;      /* frametyp                    */
  int      ticks;      /* ether:ticks=1, isdn:ticks=7 */
  uint32   net;        /* NETWORK NUMBER              */
  int      is_up;      /* Is this device up ?         */
  int      wildmask;   /* wildmask, 1=name, 2=frame, 4=net */
} NW_NET_DEVICE;

/* <========== DEVICES ==========> */
extern  int anz_net_devices;
extern  NW_NET_DEVICE *net_devices[];

/* <======== SOCKETS =========> */
#if !IN_NWROUTED
# define WDOG_SLOT          0           /* Watchdog send + recv     */

# if SOCK_EXTERN
#  define EXTERN_SLOT       (WDOG_SLOT+1)
#  ifdef _MAR_TESTS_1
#   define PSERVER_SLOT     (EXTERN_SLOT+1)
#   define SAP_SLOT         (PSERVER_SLOT+1)
#  else
#   define SAP_SLOT         (EXTERN_SLOT+1)
#  endif
# else
#  ifdef _MAR_TESTS_1
#   define PSERVER_SLOT     (WDOG_SLOT+1)
#   define SAP_SLOT         (PSERVER_SLOT+1)
#  else
#   define SAP_SLOT         (WDOG_SLOT+1)
#  endif
# endif
#else
# define SAP_SLOT           0
#endif

#if INTERNAL_RIP_SAP
# define RIP_SLOT           (SAP_SLOT   +1)
# if 0
#   define ROUTE_SLOT       (RIP_SLOT   +1)
#   define DIAG_SLOT        (ROUTE_SLOT +1)
#   define ECHO_SLOT        (DIAG_SLOT  +1)
#   define ERR_SLOT         (ECHO_SLOT  +1)
# endif
#endif

extern  int     sockfd[];

extern void ins_del_bind_net_addr(uint8 *name, int styp, ipxAddr_t *adr);
extern void send_server_response(int respond_typ,
                                 int styp, ipxAddr_t *to_addr);

extern void print_routing_info(int force);
extern void send_sap_rip_broadcast(int mode);
extern void rip_for_net(uint32 net);
extern void get_servers(void);

extern void handle_rip(int fd, int ipx_pack_typ,
                int data_len, IPX_DATA *ipxdata,
                ipxAddr_t *from_addr);


extern  void insert_delete_server(uint8  *name,
                                 int        styp,
                                 ipxAddr_t *addr,
                                 ipxAddr_t *from_addr,
                                 int        hops,
                                 int        do_delete,  /* delete = 1 */
                                 int        flags);

extern int dont_send_wdog(ipxAddr_t *addr);
extern int test_ins_device_net(uint32 rnet);
#endif
