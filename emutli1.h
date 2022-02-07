/* emutli1.h 28-Apr-96 */

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

#ifndef _EMUTLI1_H_
#define _EMUTLI1_H_

extern void set_locipxdebug(int debug);
extern int  get_interface_frame_name(char *name, uint32 net);
extern int  get_frame_name(uint8 *framename, int frame);
extern int  init_ipx(uint32 network, uint32 node, int ipx_debug);
extern void exit_ipx(int full);
extern int  init_dev(char  *devname, int frame, uint32 network);
extern void exit_dev(char  *devname, int frame);

#if 0
extern int get_ipx_addr(ipxAddr_t *addr);
#endif

extern void ipx_route_add(uint32  dest_net,
                          uint32  route_net,
                          uint8   *route_node);

extern void ipx_route_del(uint32  net);
#endif
