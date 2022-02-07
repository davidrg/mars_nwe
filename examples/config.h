/* config.h: 18-Jul-96 */
/* some of this config is needed by make, others by cc                     */

#define DO_DEBUG      1              /* compile in debug code              */
#define FUNC_17_02_IS_DEBUG 1        /* allow debugging with mars_dosutils */

#define DO_TESTING    0              /* set this to "1" to test only       */

#if DO_TESTING
# define FILENAME_NW_INI  "./nw.ini" /* full name of ini (conf) file       */
# define PATHNAME_PROGS   "."        /* where to find the executables      */
# define PATHNAME_BINDERY "."        /* directory for bindery-files        */
#else
# define FILENAME_NW_INI  "/etc/nwserv.conf"
                                     /* full name of ini (conf) file       */
# define PATHNAME_PROGS   "/sbin"    /* where to find the executables      */
# define PATHNAME_BINDERY "/etc"     /* directory for bindery-files        */
#endif

#define PATHNAME_PIDFILES "/var/run" /* directory for 'pidfiles'           */

/* ----- logging the logins via "mars_nwe" in utmp/wtmp ------------------ */
#define FILENAME_UTMP UTMP_FILE      /* use "NULL" instead of UTMP_FILE    */
                                     /* to disable logging via utmp        */
#define FILENAME_WTMP WTMP_FILE      /* use "NULL" instead of WTMP_FILE    */
                                     /* to disable logging via wtmp        */

#define NETWORK_SERIAL_NMBR 0x44444444L
                                     /* serial number (4 byte)             */
#define NETWORK_APPL_NMBR   0x2222   /* application number (2 byte)        */

#define MAX_CONNECTIONS        5     /* max. number of simultaneous        */
                                     /* connections handled by mars_nwe    */

#define IPX_DATA_GR_546        1     /* allow ipx packets > 546+30 Byte    */

#define USE_MMAP               1     /* use mmap systen call               */

#if 0
#define SOCK_EXTERN       0x8005     /* creat socket for external access   */
                                     /* i.e. Xmarsmon from H. Buchholz     */
#endif

/* <-------------------------------------------------------------------->  */
#define MAX_NW_VOLS           10     /* max. number of mars_nwe-volumes    */
#define MAX_FILE_HANDLES_CONN 80     /* max. number of open files per      */
                                     /* connection                         */
/* <---------------  new namespace services call ----------------------->  */
#define MAX_DIR_BASE_ENTRIES  50     /* max. cached base entries per       */
                                     /* connection                         */
#define WITH_NAME_SPACE_CALLS  1     /* Namespace Calls are only minimal   */
                                     /* supported so far.                  */
                                     /* To enable testing of them this     */
                                     /* entry must be changed to '1' and   */
                                     /* entry '6' in ini file should be set*/
                                     /* to > '0', too.                     */
/* <-------------------------------------------------------------------->  */
#define MAX_NW_SERVERS        40     /* max. number of nw-servers on your  */
                                     /* network                            */

/* <---------------  next is for linux only ---------------------------->  */
#define INTERNAL_RIP_SAP    1        /* use internal/own rip/sap routines  */
/* -------------------- */
#define MAX_NET_DEVICES     5        /* max. Netdevices, frames            */
#define MAX_NW_ROUTES      50        /* max. nw-networks on your network   */
                                     /* (internal + external)              */
#define MAX_RIP_ENTRIES    50        /* max. rip responses                 */
/* -------------------- */
#define SHADOW_PWD          0        /* change to '1' for shadow passwds   */

