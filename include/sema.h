/* sema.h: 07-Aug-97 */

#ifndef _SEMA_H_
#define _SEMA_H_

extern void clear_conn_semas(CONNECTION *c);
extern int handle_func_0x20(CONNECTION *c, uint8 *p, int ufunc, uint8 *responsedata);

#endif
