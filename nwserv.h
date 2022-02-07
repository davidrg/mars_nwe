/* nwserv.h 09-Jan-96 */
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

extern uint32    internal_net;  /* NETWORKNUMMER INTERN (SERVER) */
extern ipxAddr_t my_server_adr; /* Address of this server    */
extern char      my_nwname[50]; /* Name of this server       */


typedef struct {
  char     *devname;   /* "eth0" or "isdnX"         */
  int      frame;      /* frametyp 		    */
  int      ticks;      /* ether:ticks=1, isdn:ticks=7 */
  uint32   net;        /* NETWORK NUMBER 	    */
} NW_NET_DEVICE;

/* <========== DEVICES ==========> */
extern  int anz_net_devices;
extern  NW_NET_DEVICE *net_devices[];

/* <======== SOCKETS =========> */
#if 0
#define MY_BROADCAST_SLOT  0     /* Server Broadcast OUT */
#endif

#define WDOG_SLOT          0      /* Watchdog send + recv */
#define SAP_SLOT           1
#define RIP_SLOT           (SAP_SLOT   +1)
#define ROUTE_SLOT         (RIP_SLOT   +1)
#define DIAG_SLOT          (ROUTE_SLOT +1)
#if 0
#define ECHO_SLOT          (DIAG_SLOT  +1)
#define ERR_SLOT           (ECHO_SLOT  +1)
#endif

extern  int     sockfd[];

extern void send_rip_broadcast(int mode);
extern void send_sap_broadcast(int mode);
extern void rip_for_net(uint32 net);
extern void get_servers(void);

extern void handle_rip(int fd, int ipx_pack_typ,
	        int data_len, IPX_DATA *ipxdata,
	        ipxAddr_t *from_addr);

