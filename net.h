/* net.h 22-Jan-96 */

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

#ifndef _M_NET_H_
#define _M_NET_H_
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#ifndef LINUX
/*  z.B. USL */
#  include "sys/tiuser.h"
#endif

#include "sys/fcntl.h"
#include "sys/types.h"
#include "unistd.h"
#include <sys/stat.h>
#include <time.h>
#include <sys/wait.h>

#ifndef LINUX
#  include "stropts.h"
#  include "poll.h"
#  include "sys/nwctypes.h"
#  include "sys/stream.h"
/* #  include "common.h" */
/* #  include "portable.h" , needed ???   */
#  include "sys/ipx_app.h"
#else
#  include "emutli.h"      /* TLI-EMULATION */
#endif

#include <pwd.h>

#ifndef max
#define max(a,b)        (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b)        (((a) < (b)) ? (a) : (b))
#endif

#define U16_TO_BE16(u, b) { uint16 a=(u); \
               *(  (uint8*) (b) )    = *( ((uint8*) (&a)) +1); \
               *( ((uint8*) (b)) +1) = *(  (uint8*) (&a)); }


#define U32_TO_BE32(u, ar) { uint32 a= (u); uint8 *b= ((uint8*)(ar))+3; \
               *b-- = (uint8)a; a >>= 8;  \
               *b-- = (uint8)a; a >>= 8;  \
               *b-- = (uint8)a; a >>= 8;  \
               *b   = (uint8)a; }

#define U16_TO_16(u, b) { uint16 a=(u); memcpy(b, &a, 2); }
#define U32_TO_32(u, b) { uint32 a=(u); memcpy(b, &a, 4); }

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


/* ===================>  config.h  <======================= */
#include "config.h"

#ifndef MAX_CONNECTIONS
# define MAX_CONNECTIONS  5 /* maximum Number of Connections */
#endif

#ifndef MAX_NW_VOLS
# define MAX_NW_VOLS     10
#endif

#ifndef MAX_NET_DEVICES
# define MAX_NET_DEVICES  5
#endif


#ifndef FILENAME_NW_INI
# define FILENAME_NW_INI "./nw.ini"  /* location of ini (conf) file */
#endif

#ifndef PATHNAME_BINDERY
# define PATHNAME_BINDERY "."  /* location of bindery files */
#endif

#ifndef IPX_DATA_GR_546
# define IPX_DATA_GR_546 1
#endif

#ifndef MAX_NW_ROUTES
# define MAX_NW_ROUTES 50
#endif

#ifndef MAX_NW_SERVERS
# define MAX_NW_SERVERS MAX_NW_ROUTES
#endif

#if IPX_DATA_GR_546
#  define IPX_MAX_DATA      1058
#else
#  define IPX_MAX_DATA       546
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
    uint8   reserved;        /* high connection */
    uint8   completition;    /* bzw. ERROR CODE */
    uint8   connect_status;
  } ncpresponse;
  struct S_NCPREQUEST {      /* size = 7 */
    uint8   type[2];         /* 0x1111 od 0x2222 */
    uint8   sequence;
    uint8   connection;      /* low connection */
    uint8   task;
    uint8   reserved;        /* high connection */
    uint8   function;        /* Function  */
  } ncprequest;
  char data[IPX_MAX_DATA];
} IPX_DATA;

typedef struct S_SIP  SIP;
typedef struct S_SQP  SQP;
typedef struct S_SAP  SAP;
typedef struct S_SAPS SAPS;
typedef struct S_RIP  RIP;

typedef struct S_CONFREQ       CONFREQ;
typedef struct S_DIAGRESP      DIAGRESP;
typedef struct S_NCPRESPONSE   NCPRESPONSE;
typedef struct S_NCPREQUEST    NCPREQUEST;

/*  SOCKETS  */
#define SOCK_ROUTE       0x0001  /* Routing Information */
#define SOCK_ECHO        0x0002  /* Echo Protokoll Packet */
#define SOCK_ERROR       0x0003  /* Error Handler Packet */
#define SOCK_NCP         0x0451  /* File Service CORE  */
#define SOCK_SAP         0x0452  /* SAP Service Advertising Packet */
#define SOCK_RIP         0x0453  /* Routing Information Packet */
#define SOCK_NETBIOS     0x0455  /* NET BIOS Packet */
#define SOCK_DIAGNOSE    0x0456  /* Diagnostic Packet */
#define SOCK_NVT         0x8063  /* NVT (Netzerk Virtual Terminal) */
/* PACKET TYPES */

#define PACKT_0       0 /* unknown */
#define PACKT_ROUTE   1 /* Routing Information */
#define PACKT_ECHO    2 /* Echo Packet */
#define PACKT_ERROR   3 /* Error Packet */
#define PACKT_EXCH    4 /* Packet Exchange Packet */
#define PACKT_SPX     5 /* SPX Packet  */
                        /* 16 - 31 Experimental */
#define PACKT_CORE   17 /*  Core Protokoll (NCP) */


#define FD_NWSERV     3  /* one after stderr */



#include "net1.h"
/* connect.c */
typedef struct {
  uint8   name[14];              /* filename in DOS format */
  uint8   attrib;                /* Attribute  */
  uint8   ext_attrib;            /* File Execute Type */
  uint8   size[4];               /* size of file     */
  uint8   create_date[2];
  uint8   acces_date[2];
  uint8   modify_date[2];
  uint8   modify_time[2];
} NW_FILE_INFO;

typedef struct {
  uint8   name[14];              /* dirname */
  uint8   attrib;
  uint8   ext_attrib;
  uint8   create_date[2];
  uint8   create_time[2];
  uint8   owner_id[4];
  uint8   access_right_mask;
  uint8   reserved; /* future use */
  uint8   next_search[2];
} NW_DIR_INFO;

typedef struct {
  uint8   record_in_use[2];
  uint8   record_previous[4];
  uint8   record_next[4];
  uint8   client_connection[4];
  uint8   client_task[4];
  uint8   client_id[4];

  uint8   target_id[4];           /* 0xff, 0xff, 0xff, 0xff */
  uint8   target_execute_time[6]; /* all 0xff               */
  uint8   job_entry_time[6];      /* all zero               */
  uint8   job_id[4];              /* ?? alles 0 HI-LOW   */
  uint8   job_typ[2];             /* z.B. Printform HI-LOW */
  uint8   job_position[2];        /* ?? alles 0  low-high ? */
  uint8   job_control_flags[2];   /* z.B  0x10, 0x00   */
              /* 0x80 operator hold flag */
              /* 0x40 user hold flag     */
              /* 0x20 entry open flag    */
              /* 0x10 service restart flag */
              /* 0x08 autostart flag */

  uint8   job_file_name[14];      /* len + DOS filename */
  uint8   job_file_handle[4];
  uint8   server_station[4];
  uint8   server_task[4];
  uint8   server_id[4];
  uint8   job_bez[50];             /* "LPT1 Catch"  */
  uint8   client_area[152];
} QUEUE_JOB;

typedef struct {
  uint8   client_connection;
  uint8   client_task;
  uint8   client_id[4];
  uint8   target_id[4];           /* 0xff, 0xff, 0xff, 0xff */
  uint8   target_execute_time[6]; /* all 0xff               */
  uint8   job_entry_time[6];      /* all zero               */
  uint8   job_id[2];              /* ?? alles 0 HI-LOW   */
  uint8   job_typ[2];             /* z.B. Printform HI-LOW */
  uint8   job_position;           /* zero */
  uint8   job_control_flags;       /* z.B  0x10       */
              /* 0x80 operator hold flag */
              /* 0x40 user hold flag     */
              /* 0x20 entry open flag    */
              /* 0x10 service restart flag */
              /* 0x08 autostart flag */

  uint8   job_file_name[14];       /* len + DOS filename */
  uint8   job_file_handle[6];
  uint8   server_station;
  uint8   server_task;
  uint8   server_id[4];
  uint8   job_bez[50];             /* "LPT1 Catch"  */
  uint8   client_area[152];
} QUEUE_JOB_OLD;                   /* before 3.11 */

typedef struct {
  uint8   version;                /* normal 0x0       */
  uint8   tabsize;                /* normal 0x8       */
  uint8   anz_copies[2];          /* copies 0x0, 0x01 */
  uint8   print_flags[2];         /*        0x0, 0xc0  z.B. with banner */
  uint8   max_lines[2];           /*        0x0, 0x42 */
  uint8   max_chars[2];           /*        0x0, 0x84 */
  uint8   form_name[16];          /*        "UNKNOWN" */
  uint8   reserved[6];            /*        all zero  */
  uint8   banner_user_name[13];   /*        "SUPERVISOR"  */
  uint8   bannner_file_name[13];  /*        "LST:"        */
  uint8   bannner_header_file_name[14];  /* all zero      */
  uint8   file_path_name[80];            /* all zero      */
} QUEUE_PRINT_AREA;


extern int nw_init_connect(void);
extern int nw_free_handles(int task);

extern int nw_creat_open_file(int dir_handle, uint8 *data, int len,
                NW_FILE_INFO *info, int attrib, int access, int mode);

extern int nw_read_datei(int  fhandle, uint8 *data, int size, uint32 offset);
extern int nw_seek_datei(int  fhandle, int modus);
extern int nw_write_datei(int fhandle, uint8 *data, int size, uint32 offset);
extern int nw_lock_datei(int  fhandle, int offset, int size, int do_lock);
extern int nw_close_datei(int fhandle);

extern int nw_server_copy(int qfhandle, uint32 qoffset,
                          int zfhandle, uint32 zoffset,
                          uint32 size);

extern int nw_delete_datei(int dir_handle,  uint8 *data, int len);
extern int nw_chmod_datei(int dir_handle, uint8 *data, int len, int modus);

extern int mv_file(int qdirhandle, uint8 *q, int qlen,
            int zdirhandle, uint8 *z, int zlen);



extern int nw_mk_rd_dir(int dir_handle, uint8 *data, int len, int mode);

extern int nw_search(uint8 *info,
              int dirhandle, int searchsequence,
              int search_attrib, uint8 *data, int len);

extern int nw_dir_search(uint8 *info,
              int dirhandle, int searchsequence,
              int search_attrib, uint8 *data, int len);


extern int nw_find_dir_handle( int dir_handle,
                               uint8      *data, /* zus„tzlicher Pfad  */
                               int         len); /* L„nge Pfad        */

extern int nw_alloc_dir_handle(
                      int dir_handle,  /* Suche ab Pfad dirhandle   */
                      uint8  *data,       /* zus„tzl. Pfad             */
                      int    len,         /* L„nge DATA                */
                      int    driveletter, /* A .. Z normal             */
                      int    is_temphandle, /* tempor„res Handle 1     */
                                               /* spez. temp Handle  2    */
                      int    task);          /* Prozess Task            */


extern int nw_open_dir_handle( int        dir_handle,
                        uint8      *data,     /* zus„tzlicher Pfad  */
                        int        len,       /* L„nge DATA         */
                        int        *volume,   /* Volume             */
                        int        *dir_id,   /* „hnlich Filehandle */
                        int        *searchsequence);


extern int nw_free_dir_handle(int dir_handle);

extern int nw_set_dir_handle(int targetdir, int dir_handle,
                             uint8 *data, int len, int task);

extern int nw_get_directory_path(int dir_handle, uint8 *name);

extern int nw_get_vol_number(int dir_handle);



extern int nw_get_eff_dir_rights(int dir_handle, uint8 *data, int len, int modus);

extern int nw_set_fdate_time(uint32 fhandle, uint8 *datum, uint8 *zeit);
extern int nw_scan_dir_info(int dir_handle, uint8 *data, int len,
                            uint8 *subnr, uint8 *subname,
                            uint8 *subdatetime, uint8 *owner);

#include "tools.h"


#endif
