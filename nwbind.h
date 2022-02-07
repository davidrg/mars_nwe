/* nwbind.h 23-Apr-98 */

#ifndef _NWBIND_H_
#define _NWBIND_H_

#define MAX_SEMA_CONN    10    /* 10 Semaphore pre connection */


typedef struct {
   int handle;     /* semahore handle */
   int opencount;  /* times open      */
} SEMA_CONN;

typedef struct {
   ipxAddr_t   client_adr;      /* address remote client */
   uint32      object_id;       /* logged object         */
                                /* 0 = not logged in     */
   int         id_flags;        /* &1 == supervisor (equivalence) */
                                /* flags are also availible in */
                                /* connection based routines */

   uint8       crypt_key[8];    /* password generation   */
   time_t      t_login;         /* login time            */
   uint8       message[60];     /* saved BCastmessage    */
   int         active;          /* 0=closed, 1= active   */
   int         send_to_sock;    /* this is the receiving sock */
   int         pid_nwconn;      /* pid of user process nwconn */
   int         count_semas;     /* open semahores */
   SEMA_CONN   semas[MAX_SEMA_CONN]; 
} CONNECTION;

#endif
