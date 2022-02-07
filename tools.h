/*  tools.h : 20-Mar-96    */

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
#ifndef _TOOLS_H_
#define _TOOLS_H_

/* processes which need tools */
#define NWSERV    1
#define NCPSERV   2
#define NWCONN    3
#define NWCLIENT  4
#define NWBIND    5

extern  FILE *logfile;
extern  void x_x_xfree(char **p);
extern  int x_x_xnewstr(uint8 **p,  uint8 *s);

#define xfree(p)      x_x_xfree((char **)&(p))
#define new_str(p, s) x_x_xnewstr((uint8 **)&(p), s)

extern char  *xmalloc(uint size);
extern char  *xcmalloc(uint size);
extern int   strmaxcpy(uint8 *dest, uint8 *source, int len);
#define xstrcpy(d, s)    strmaxcpy((d), (s), sizeof(d)-1)
#define xstrmaxcpy(d, s, len) strmaxcpy((d), (s), min(sizeof(d)-1, (len)) )

extern void  dprintf(char *p, ...);
extern void  xdprintf(int dlevel, int mode, char *p, ...);
extern void  errorp(int mode, char *what, char *p, ...);
extern FILE  *open_nw_ini(void);
extern int   get_ini_entry(FILE *f, int entry, uint8 *str, int strsize);
extern char  *get_exec_path(char *buff, char *progname);
extern int   get_ini_int(int what);
extern void  get_ini_debug(int what);
extern void  init_tools(int module, int conn);

extern uint8 down_char(uint8 ch);
extern uint8 up_char(uint8 ch);
extern uint8 *downstr(uint8 *s);
extern uint8 *upstr(uint8 *s);


extern int nw_debug;
#if DO_DEBUG
#  define DPRINTF(x)  dprintf x
#  define XDPRINTF(x) xdprintf x
#  define D() XDPRINTF((3, 0, "Z: %d" , __LINE__));
#else
#  define DPRINTF(x)  /* */
#  define XDPRINTF(x) /* */
#  define D()         /* */
#endif

#endif /* _TOOLS_H_ */

