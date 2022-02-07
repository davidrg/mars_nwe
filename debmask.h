/* debmask.h: 23-Jul-97 */
#ifndef _DEBMASK_H_
#define _DEBMASK_H_
/*
 * several debug masks.
 * second paramter in debug entries 100 .. 1xx
 *
*/

/* NWCONN */
#define D_FH_OPEN               1   /* file open/close  */
#define D_FH_LOCK      		2   /* file lock/unlock */
#define D_FH_FLUSH     		4   /* file flushes     */

#define D_FN_NAMES     		8   
#define D_FN_SEARCH 	     0x10   /* file search */  

#endif

