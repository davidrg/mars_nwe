/* config.h: 08-Feb-96 */
/* some of this config is needed by make, others by cc */
#define FILENAME_NW_INI  "/etc/nwserv.conf" /* full name of ini (conf) file */
#define PATHNAME_PROGS   "/sbin"     /* path location of progs       */
#define PATHNAME_BINDERY "/etc"      /* path location of bindery     */

#define NETWORK_SERIAL_NMBR 0x44444444L /* Serial Number 4 Byte      */
#define NETWORK_APPL_NMBR   0x2222      /* Applikation Number 2 Byte */

#define MAX_CONNECTIONS       5         /* max. Number  of Connections  */
                                        /* must be < 256   !!!          */
#define MAX_NW_VOLS          10         /* max. Volumes                 */
#define IPX_DATA_GR_546       1         /* allow ipx packets > 546+30 Byte  */

#define WITH_NAME_SPACE_CALLS 0         /* Namespace Calls are only badly    */
                                        /* supported till now.               */
                                        /* to enable testing of them this    */
                                        /* entry must be changed to '1' and  */
                                        /* entry '6' in ini file must be set */
                                        /* to '1', too.	    	      	     */

#define MAX_NW_SERVERS        40        /* max. count of servers          */

/* <---------------  next is for linux only ------------------->  */
#define INTERNAL_RIP_SAP    1          /* use internal/own rip/sap process */
#define MAX_NET_DEVICES     5          /* max. Netdevices, frames      */
#define MAX_NW_ROUTES      50          /* max. networks (internal + external) */

/* this is for very special use of mars_nwe to only act as a router */
#define FILE_SERVER_INACTIV 0          /* 1 = ncpserv will not be startet  */


