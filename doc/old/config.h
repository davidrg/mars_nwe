/* config.h: 22-Jun-00 */
/* some of this config is needed by make, others by cc                     */

#define DO_DEBUG      1              /* compile in debug code              */
#define FUNC_17_02_IS_DEBUG 0        /* allow debugging with mars_dosutils */

#define DO_TESTING    0              /* set this to "1" to test only       */

#if DO_TESTING
# define FILENAME_NW_INI  "./nw.ini" /* full name of ini (conf) file       */
# define PATHNAME_PROGS   "."        /* where to find the executables      */
# define PATHNAME_BINDERY "."        /* directory for bindery-files        */
#else
# define FILENAME_NW_INI  "/etc/nwserv/nwserv.conf"
                                     /* full name of ini (conf) file       */
# define PATHNAME_PROGS   "/usr/sbin" /* where to find the executables     */
# define PATHNAME_BINDERY "/var/lib/nwserv/bindery"  /* directory for bindery-files        */
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

#define MAX_CONNECTIONS        50    /* max. number of simultaneous        */
                                     /* connections handled by mars_nwe    */
 /* !! NOTE !! */
 /* If set > 255 some NCP calls will probably not work, try it with caution */
 /* and you should apply examples/kpatch2.0.29 to kernels prior 2.0.32      */


#define IPX_DATA_GR_546        2     /* 0 = max. IPX Packets = 546  +30 Byte ( 512 Byte RWBuff) */
                                     /* 1 = max. IPX packets = 1058 +30 Byte (1024 Byte RWBuff) */
                                     /* 2 = max. IPX packets = 1470 +30 Byte (1444 Byte RWBuff) */
                                     /* 3 = max. IPX packets = 4130 +30 Byte (4096 Byte RWBuff) */

#define ENABLE_BURSTMODE       0     /* 0 = disable burstmode, 1 = enable burstmode */
  /* still NOT working correct !!!!!                           */
  /* to get Burstmode really enabled, section '6' in conf-file */
  /* must be set to a value > 1 (3.12 Server)                  */
  /* and kernel-patch examples/kpatch2.0.29 should be used for */
  /* kernels prior 2.0.32                                      */


#define USE_MMAP               1     /* use mmap systen call, not always best choice */

#if 0
#define SOCK_EXTERN       0x8005     /* creat socket for external access   */
                                     /* i.e. Xmarsmon from H. Buchholz     */
#endif

/* <-------------------------------------------------------------------->  */
#define MAX_NW_VOLS           10     /* max. number of mars_nwe-volumes    */
#define MAX_FILE_HANDLES_CONN 255    /* max. number of open files per      */
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
#define HANDLE_ALL_SAP_TYPS    1     /* if set to 0 only SAP-Typ 4 Servers */
                                     /* will be put into routing table and */
                                     /* if set to 1 all SAP Typs will be   */
                                     /* used.                              */

#define PERSISTENT_SYMLINKS    0     /* change to '1' for persistent symlinks */
                                     /* main idea from Victor Khimenko */
  /* still NOT working !! */

/* <---------------  next is for linux only ---------------------------->  */
#define INTERNAL_RIP_SAP    1        /* use internal/own rip/sap routines  */

#define SHADOW_PWD          1        /* change to '1' for shadow passwds   */
#define QUOTA_SUPPORT       0        /* change to '1' for quota support    */

/* for sending 'Request being serviced' replys, /lenz */
#define CALL_NWCONN_OVER_SOCKET  0 

