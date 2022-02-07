/* net1.h 14-Jan-96 */

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


#ifndef LINUX
extern void print_t_info(struct t_info *t);
extern void print_ud_data(struct t_unitdata *ud);
#endif

extern char *xvisable_ipx_adr(ipxAddr_t *p, int modus);
#define visable_ipx_adr(adr) xvisable_ipx_adr((adr), 0)

extern void print_ipx_addr(ipxAddr_t   *p);
extern void print_ipx_data(IPX_DATA    *p);
extern void print_sip_data(SIP         *sip);

extern void adr_to_ipx_addr(ipxAddr_t *p, char *s);
extern void ipx_addr_to_adr(char *s, ipxAddr_t *p);

extern int send_ipx_data(int fd, int pack_typ,
	              int data_len, char *data,
	              ipxAddr_t *to_addr, char *comment);

