/* net.h 26-Feb-98 */

/* (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany
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

#ifndef _M_NET_H_
#define _M_NET_H_
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>  /* moved 12-May-98 0.99.pl9 */

/* we want sysv signal handling, used for glibc */
#define _XOPEN_SOURCE 
/* perhaps this define is better */
#define __USE_XOPEN

#include <signal.h>
#include <string.h>

#ifndef LINUX
/*  z.B. USL */
#  include <sys/tiuser.h>
#endif

#include <sys/fcntl.h>
/* #include <sys/types.h> moved 12-May-98 0.99.pl9 */
#include <unistd.h>
#include <sys/stat.h>
#ifndef S_ISLNK
#  define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#endif
#include <time.h>
#include <sys/wait.h>
#include <utmp.h>
#include <grp.h>

#include <sys/errno.h>
extern int errno;

#ifndef LINUX
#  include <stropts.h>
#  include <poll.h>
#  include <sys/nwctypes.h>
#  include <sys/stream.h>
/* #  include "common.h" */
/* #  include "portable.h" , needed ???   */
#  include <sys/ipx_app.h>
#else
#  include <sys/ioctl.h>
#  include "emutli.h"      /* TLI-EMULATION */
#  include "emutli1.h"     /* TLI-EMULATION */
#endif

#include <pwd.h>

#ifndef max
#define max(a,b)        (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b)        (((a) < (b)) ? (a) : (b))
#endif

#ifndef LINUX
# define inline /**/
#endif

#ifdef SPARC
# define  U16_TO_BE16  X_U16_TO_16
# define  U32_TO_BE32  X_U32_TO_32
# define  U16_TO_16    X_U16_TO_BE16
# define  U32_TO_32    X_U32_TO_BE32
#else
# define  U16_TO_BE16  X_U16_TO_BE16
# define  U32_TO_BE32  X_U32_TO_BE32
# define  U16_TO_16    X_U16_TO_16
# define  U32_TO_32    X_U32_TO_32
#endif

#define X_U16_TO_BE16(u, b) { uint16 a=(uint16)(u); \
               *(  (uint8*) (b) )    = *( ((uint8*) (&a)) +1); \
               *( ((uint8*) (b)) +1) = *(  (uint8*) (&a)); }

#define X_U32_TO_BE32(u, b) { uint32 a=(uint32)(u); \
               *( (uint8*) (b))  = *( ((uint8*) (&a))+3); \
               *( ((uint8*) (b)) +1) = *( ((uint8*) (&a))+2); \
               *( ((uint8*) (b)) +2) = *( ((uint8*) (&a))+1); \
               *( ((uint8*) (b)) +3) = *(  (uint8*) (&a)); }

#define X_U16_TO_16(u, b) { uint16 a=(uint16)(u); \
                      ((uint8*)b)[0] = ((uint8*)&a)[0]; \
                      ((uint8*)b)[1] = ((uint8*)&a)[1]; }

#define X_U32_TO_32(u, b) { uint32 a=(uint32)(u); \
                      ((uint8*)b)[0] = ((uint8*)&a)[0]; \
                      ((uint8*)b)[1] = ((uint8*)&a)[1]; \
                      ((uint8*)b)[2] = ((uint8*)&a)[2]; \
                      ((uint8*)b)[3] = ((uint8*)&a)[3]; }

#define GET_BE16(b)  (     (int) *(((uint8*)(b))+1)  \
                     | ( ( (int) *( (uint8*)(b)   )  << 8) ) )

#define GET_BE32(b)  (   (uint32)   *(((uint8*)(b))+3)  \
                   | (  ((uint32)   *(((uint8*)(b))+2) ) << 8)  \
                   | (  ((uint32)   *(((uint8*)(b))+1) ) << 16) \
                   | (  ((uint32)   *( (uint8*)(b)   ) ) << 24) )


#define GET_16(b)    (     (int) *( (uint8*)(b)   )  \
                     | ( ( (int) *(((uint8*)(b))+1)  << 8) ) )

#define GET_32(b)    (   (uint32)   *( (uint8*)(b)   )  \
                   | (  ((uint32)   *(((uint8*)(b))+1) ) << 8)  \
                   | (  ((uint32)   *(((uint8*)(b))+2) ) << 16) \
                   | (  ((uint32)   *(((uint8*)(b))+3) ) << 24) )


#define MAX_U32    ((uint32)0xffffffffL)
#define MAX_U16    ((uint16)0xffff)

#define MAX_I32     0x7fffffff

/* ===================>  config.h  <======================= */
#ifdef CALL_NWCONN_OVER_SOCKET
# undef CALL_NWCONN_OVER_SOCKET
#endif

#include "config.h"

#ifndef CALL_NWCONN_OVER_SOCKET
# ifdef LINUX
#   ifdef SIOCIPXNCPCONN
#     define CALL_NWCONN_OVER_SOCKET 1
#   else
#     define CALL_NWCONN_OVER_SOCKET 0
#   endif
# else
#   define CALL_NWCONN_OVER_SOCKET   0
# endif
#endif

#ifndef DO_DEBUG
# define DO_DEBUG  1
#endif

#if DO_DEBUG
#  ifndef FUNC_17_02_IS_DEBUG
#    define FUNC_17_02_IS_DEBUG 0
#  endif
#else
#  undef  FUNC_17_02_IS_DEBUG
#  define FUNC_17_02_IS_DEBUG   0
#endif

#ifndef MAX_CONNECTIONS
# define MAX_CONNECTIONS  5 /* maximum Number of connections */
#endif

#ifndef MAX_NW_VOLS
# define MAX_NW_VOLS     10 /* maximum Number of volumes */
#endif

#ifndef MAX_FILE_HANDLES_CONN
# define MAX_FILE_HANDLES_CONN 80
#endif


#ifndef MAX_NET_DEVICES
# define MAX_NET_DEVICES  5
#endif


#ifndef FILENAME_NW_INI
# define FILENAME_NW_INI "./nw.ini"  /* location of ini (conf) file */
#endif

#ifndef PATHNAME_BINDERY
# define PATHNAME_BINDERY "/etc"  /* location of bindery files */
#endif

#ifndef PATHNAME_PIDFILES
# define PATHNAME_PIDFILES "/var/run"  /* location of pidfiles */
#endif

#ifndef FILENAME_UTMP
# define FILENAME_UTMP UTMP_FILE
#endif

#ifndef FILENAME_WTMP
# define FILENAME_WTMP WTMP_FILE
#endif

#ifndef NETWORK_SERIAL_NMBR
# define NETWORK_SERIAL_NMBR  0x44444444L /* Serial Number 4 Byte      */
#endif
#ifndef NETWORK_APPL_NMBR
# define NETWORK_APPL_NMBR    0x2222      /* Applikation Number 2 Byte */
#endif

#ifndef IPX_DATA_GR_546
# define IPX_DATA_GR_546 2
#endif

#ifndef USE_MMAP
# define USE_MMAP 1
#endif

#ifndef WITH_NAME_SPACE_CALLS
# define WITH_NAME_SPACE_CALLS 0
#endif

#ifndef MAX_DIR_BASE_ENTRIES
# define MAX_DIR_BASE_ENTRIES   50
#endif

#if MAX_DIR_BASE_ENTRIES < 10
# define MAX_DIR_BASE_ENTRIES   10
#endif

#ifndef HANDLE_ALL_SAP_TYPS
# define HANDLE_ALL_SAP_TYPS 1
#endif

#if IPX_DATA_GR_546
#  if IPX_DATA_GR_546 == 3
#    define IPX_MAX_DATA      4130
#    define RW_BUFFERSIZE     4096
#  elif IPX_DATA_GR_546 == 2
#    define IPX_MAX_DATA      1470
#    define RW_BUFFERSIZE     1444
#  else
#    define IPX_MAX_DATA      1058
#    define RW_BUFFERSIZE     1024
#  endif
#else
#  define IPX_MAX_DATA         546
#  define RW_BUFFERSIZE        512
#endif

#ifndef ENABLE_BURSTMODE
#  define ENABLE_BURSTMODE        0  /* no Burst mode by default */
#endif

#ifndef PERSISTENT_SYMLINKS
#  define PERSISTENT_SYMLINKS     0
#endif

#ifndef SOCK_EXTERN
#  define SOCK_EXTERN          0  /* no external SOCKET */
#endif

#ifndef DO_TESTING
# define DO_TESTING            0
#endif

#ifndef NEW_ATTRIB_HANDLING
# define NEW_ATTRIB_HANDLING   1
#endif


#ifdef LINUX
# ifndef QUOTA_SUPPORT
#  define QUOTA_SUPPORT        0
# endif
#else
# undef  QUOTA_SUPPORT
# define QUOTA_SUPPORT         0
#endif

#ifdef LINUX
# ifdef IN_NWROUTED
#  undef   INTERNAL_RIP_SAP
#  define  INTERNAL_RIP_SAP    1
# endif
# ifndef   INTERNAL_RIP_SAP
#  define  INTERNAL_RIP_SAP    1
# endif
#else
/* USL has rip/sap router builtin */
# undef  INTERNAL_RIP_SAP
# define INTERNAL_RIP_SAP      0
#endif

#define MAX_SERVER_NAME   48

typedef union {
  struct S_SIP { /* Server Identification Packet, siehe auch SAP */
     uint8      response_type[2]; /*hi-lo */
                                  /* 2 periodic bzw. Shutdown */
                                  /*   bzw. General Service Response */
                                  /* 4 nearest Service Response      */
     uint8      server_type[2];   /*hi-lo */
                                  /* 0x0    unknown      */
                                  /* 0x1    user         */
                                  /* 0x2    user/group   */
                                  /* 0x3    Print Queue */
                                  /* 0x4    File Server   */
                                  /* 0x5    Job Server   */
                                  /* 0x6    Gateway      */
                                  /* 0x7    Printserver    */
                                  /* 0x9    Archive Server */
                                  /* 0x24   Remote Bridge Server */
                                  /* 0x47   Advertising Print Server */
                                  /* 0x107  Netware 386  */
                                  /* 0xFFFF (-1) WILD    */

     uint8      server_name[MAX_SERVER_NAME];
     ipxAddr_t  server_adr;
     uint8      intermediate_networks[2]; /* hi-lo */
                                          /* normal  0  */
                                          /* down    16 */
  } sip; /* Server Identifikation Packet */
  struct S_SQP {  /* Service Query Packet */
     uint8       query_type[2];  /* hi low */
                                 /* 1 general  Service Query */
                                 /* 3 nearest Server Query   */
     uint8       server_type[2]; /* hi low  s.o. */
  } sqp;
  struct S_SAP {
     uint8        sap_operation[2];    /* hi-low */
     struct S_SAPS {
       uint8      server_type[2];
       uint8      server_name[MAX_SERVER_NAME];
       ipxAddr_t  server_adr;
       uint8      server_hops[2];
     } saps;
  } sap;
  struct S_WDOG {       /* Watchdog      */
     uint8 connid;      /* connection ID */
     uint8 status;      /* STATUS        */
  } wdog;
  struct S_CONFREQ {  /* IPX Diagnose */
     uint8 count;
     uint8 ex_node[6];
  } confreq;
  struct S_RIP { /* ROUTING */
     uint8 operation[2];    /* 1 request, 2 response */
     uint8 network[4];
     uint8 hops[2];       /* Anzahl Routerspassagen um Netzwerk zu Erreichen  */
     uint8 ticks[2];      /* Zeit in 1/18 sec. um Netzwerk Nummer zu erreichen */
  } rip;
  struct S_DIAGRESP {
     uint8 majorversion;
     uint8 minorversion;
     uint8 spx_diag_sock[2];   /* SPX Diagnose SOCKET */
     uint8 anz;                /* Anzahl Componente   */
     /*  ....  Componente
      *  uint8 id;      0:IPX/SPX, 1: BRIGDE Driver, 2: Shell driver
      *                 3: Shell,  4: VAP Shell
      *
      * extented        5: external Bridge, 6 Files Server/Bridge
      *                 7: non dedicated IPX/SPX
      *
      * extented haben folgende Zusatzfelder
      * uint8   count; Anzahl Local Networks
      *     jetzt pro Network
      * uint8   type;  0: LAN-Board,
      *                1: non dedicated File/Sever(virtuelles Board)
      *                2: redirected remote Line;
      *
      * uint8   net;   Netwerk Adresse
      * uint8   node;  Node
      *
      *
      */
  } diaresp;
  struct S_NCPRESPONSE {     /* size = 8 */
    uint8   type[2];         /* 0x3333 */
    uint8   sequence;
    uint8   connection;      /* low connection */
    uint8   task;
    uint8   high_connection; /* high connection */
    uint8   completition;    /* bzw. ERROR CODE */
    uint8   connect_status;
  } ncpresponse;
  struct S_NCPREQUEST {      /* size = 7 */
    uint8   type[2];         /* 0x1111 od 0x2222 */
    uint8   sequence;
    uint8   connection;      /* low connection */
    uint8   task;
    uint8   high_connection; /* high connection */
    uint8   function;        /* Function  */
  } ncprequest;
  struct S_BURSTPACKET {        /* size = 36 */
    uint8   type[2];            /* 0x7777 */
    uint8   flags;              /* 0x10 = EOB (EndOfBurst) */
                                /* 0x80 = SYS (Systemflag) */
    uint8   streamtyp;          /* 2 = BIG_SEND_BURST stream typ */
    uint8   source_conn[4];
    uint8   dest_conn[4];
    uint8   packet_sequence[4]; /* hi-lo, incr. by every packet */
    uint8   delaytime[4];       /* hi-lo, statistik */
    uint8   burst_seq[2];       /* akt_sequence ?   */
    uint8   ack_seq[2];         /* next_sequnce ?   */

    uint8   burstsize[4];       /* hi-lo, complete burstsize  */
    uint8   burstoffset[4];     /* hi-lo */

    uint8   datasize[2];        /* hi-lo, number of data byte's in this packet */
    uint8   missing[2];         /* 0,0 ,  number of missing fragments, follows */
  } burstpacket;
  struct S_OWN_DATA {
    struct {
      uint8   type[2];       /* 0xeeee  */
      uint8   sequence;
      uint8   reserved;      /* its good for alignement  */
    } h;                     /* header */
    struct {
      int     size;          /* size of next two entries */
      int     function;
      uint8   data[1];
    } d;
  } owndata;
  struct S_OWN_REPLY {
    uint8   type[2];         /* 0xefef  */
    uint8   sequence;
    uint8   result;          /* perhaps we need it */
  } ownreply;
  char data[IPX_MAX_DATA];
} IPX_DATA;

typedef struct S_SIP           SIP;
typedef struct S_SQP           SQP;
typedef struct S_SAP           SAP;
typedef struct S_SAPS          SAPS;
typedef struct S_RIP           RIP;

typedef struct S_CONFREQ       CONFREQ;
typedef struct S_DIAGRESP      DIAGRESP;
typedef struct S_NCPRESPONSE   NCPRESPONSE;
typedef struct S_NCPREQUEST    NCPREQUEST;
typedef struct S_BURSTPACKET   BURSTPACKET;
typedef struct S_OWN_DATA      OWN_DATA;
typedef struct S_OWN_REPLY     OWN_REPLY;

/*  SOCKETS  */
#define SOCK_AUTO        0x0000  /* Autobound Socket               */
#define SOCK_ROUTE       0x0001  /* Routing Information            */
#define SOCK_ECHO        0x0002  /* Echo Protokoll Packet          */
#define SOCK_ERROR       0x0003  /* Error Handler Packet           */
#define SOCK_NCP         0x0451  /* File Service CORE              */
#define SOCK_SAP         0x0452  /* SAP Service Advertising Packet */
#define SOCK_RIP         0x0453  /* Routing Information Packet     */
#define SOCK_NETBIOS     0x0455  /* NET BIOS Packet                */
#define SOCK_DIAGNOSE    0x0456  /* Diagnostic Packet              */
#define SOCK_PSERVER     0x8060  /* Print Server's Socket          */
#define SOCK_NVT         0x8063  /* NVT (Network Virtual Terminal) */


/* PACKET TYPES */
#define PACKT_0       0 /* unknown                */
#define PACKT_ROUTE   1 /* Routing Information    */
#define PACKT_ECHO    2 /* Echo Packet            */
#define PACKT_ERROR   3 /* Error Packet           */
#define PACKT_EXCH    4 /* Packet Exchange Packet */
#define PACKT_SPX     5 /* SPX Packet             */
                        /* 16 - 31 Experimental   */
#define PACKT_CORE   17 /*  Core Protokoll (NCP)  */

#define FD_NWSERV     3  /* one after stderr      */

#include "net1.h"

#include "tools.h"


#endif
