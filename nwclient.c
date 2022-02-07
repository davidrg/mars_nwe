/* nwclient.c: 24-Dec-95 */
/*
 * Einfacher Testclient, wird von nwserv (im Client Modus) gestartet
 * Dieses Modul hilft dabei, NCP Responses eines
 * echten NW Servers zu analysieren.
 * Beim 'echten' NW Server muá 'allow unencryted passwords' gesetzt
 * sein.
 */

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

#include "net.h"

static ipxAddr_t  serv_addr;
static ipxAddr_t  my_addr;
static int fd_ipx;
static int fd_wdog;

static int open_socket()
{
  int ipx_fd=t_open("/dev/ipx", O_RDWR, NULL);
  struct t_bind  bind;
  if (ipx_fd < 0) {
     t_error("t_open !Ok");
     return(-1);
  }
  U16_TO_BE16(0,     my_addr.sock);
  bind.addr.len    = sizeof(ipxAddr_t);
  bind.addr.maxlen = sizeof(ipxAddr_t);
  bind.addr.buf    = (char*)&my_addr;
  bind.qlen        = 0; /* immer */
  if (t_bind(ipx_fd, &bind, &bind) < 0){
    t_error("t_bind !OK");
    t_close(ipx_fd);
    return(-1);
  }
  XDPRINTF((1,0, "socket bound TO %s", visable_ipx_adr(&my_addr) ));
  return(ipx_fd);
}

static int init_client()
{
  return( (fd_ipx = open_socket()) > -1
       && (fd_wdog = open_socket()) > -1   ? 0 : 1);
}

/*   DATA OUT */
static IPX_DATA    ipxdata_out;
static NCPREQUEST  *ncprequest =(NCPREQUEST*)&ipxdata_out;
static uint8       *requestdata=((uint8*)&ipxdata_out)+sizeof(NCPREQUEST);

static void ncp_request(int type, int  sequence,
	        int connection,   int  task,
                int reserved,     int  function,
                int data_len,     char *komment)

{
  U16_TO_BE16(type, ncprequest->type);
  ncprequest->sequence       = (uint8) sequence;
  ncprequest->connection     = (uint8) connection;
  ncprequest->task           = (uint8) task;
  ncprequest->reserved       = (uint8) reserved;
  ncprequest->function       = (uint8) function;
  {
    int j = data_len;
    XDPRINTF((1, 0, "NCP REQUEST: type:0x%x, seq:%d, conn:%d, task:%d, reserved:0x%x, func:0x%x",
       type, sequence, connection, task, reserved, function));
     if (j > 0){
      uint8  *p=requestdata;
      XDPRINTF((1, 2, "len %d, DATA:", j));
      while (j--) {
	int c = *p++;
	if (c > 32 && c < 127)  XDPRINTF((1, 3,",\'%c\'", (char) c));
	else XDPRINTF((1, 3, ",0x%x", c));
      }
      XDPRINTF((1, 1, NULL));
    }
  }
  send_ipx_data(fd_ipx, 17, sizeof(NCPREQUEST) + data_len,
	               (char *) ncprequest,
	               &serv_addr, komment);
}

/*  DATA  IN  */

static IPX_DATA    ipxdata_in;
static NCPRESPONSE *ncpresponse  = (NCPRESPONSE*)&ipxdata_in;
static uint8       *responsedata = ((uint8*)&ipxdata_in) + sizeof(NCPRESPONSE);

/* -------------------------------------- */
static int sequence=0;

static int handle_event(void)
{
  struct           t_unitdata ud;
  ipxAddr_t        source_adr;
  uint8            ipx_pack_typ;
  int              flags = 0;

  ud.opt.len       = sizeof(ipx_pack_typ);
  ud.opt.maxlen    = sizeof(ipx_pack_typ);
  ud.opt.buf       = (char*)&ipx_pack_typ; /* bekommt aktuellen Typ */

  ud.addr.len      = sizeof(ipxAddr_t);
  ud.addr.maxlen   = sizeof(ipxAddr_t);

  ud.addr.buf      = (char*)&source_adr;

  ud.udata.len     = sizeof(IPX_DATA);
  ud.udata.maxlen  = sizeof(IPX_DATA);
  ud.udata.buf     = (char*)&ipxdata_in;

  if (t_rcvudata(fd_ipx, &ud, &flags) < 0){
    struct t_uderr uderr;
    ipxAddr_t  erradr;
    uint8      err_pack_typ;
    uderr.addr.len      = sizeof(ipxAddr_t);
    uderr.addr.maxlen   = sizeof(ipxAddr_t);
    uderr.addr.buf      = (char*)&erradr;
    uderr.opt.len       = sizeof(err_pack_typ);
    uderr.opt.maxlen    = sizeof(err_pack_typ);
    uderr.opt.buf       = (char*)&err_pack_typ; /* bekommt aktuellen Typ */
    ud.addr.buf         = (char*)&source_adr;
    t_rcvuderr(fd_ipx, &uderr);
    XDPRINTF((1, 0, "Error from %s, Code = 0x%lx", visable_ipx_adr(&erradr), uderr.error));
    if (nw_debug) t_error("t_rcvudata !OK");
    return(-1);
  } else {
    int responselen  = ud.udata.len - sizeof(NCPRESPONSE);
    int j = responselen;

    int    sequence          = (int)ncpresponse->sequence;
    int    connection        = (int)ncpresponse->connection;
    int    task              = (int)ncpresponse->task;
    int    reserved          = (int)ncpresponse->reserved;
    int    completition      = (int)ncpresponse->completition;
    int    connect_status    = (int)ncpresponse->connect_status;
    int    type 	     = GET_BE16(ncpresponse->type);

    XDPRINTF((1,0, "Ptyp:%d von: %s, len=%d", (int)ipx_pack_typ, visable_ipx_adr(&source_adr), responselen));
    XDPRINTF((1,0, "RESPONSE:t:0x%x, seq:%d, conn:%d, task:%d, res:0x%x, complet.:0x%x, connect:0x%x",
       type, sequence, connection, task, reserved, completition, connect_status));

     if (j > 0){
      uint8  *p=responsedata;
      XDPRINTF((1, 2, "len %d, DATA:", j));
      while (j--) {
        int c = *p++;
        if (c > 32 && c < 127)  XDPRINTF((1, 3, ",\'%c\'", (char) c));
        else XDPRINTF((1, 3, ",0x%x", c));
      }
      XDPRINTF((1, 1, NULL));
    }
  }

  if (sequence == ncpresponse->sequence) {
    sequence++;
    return((int)(ncpresponse->completition));
  }
  return(-1);
}

/* ------------------------------------------------------ */
static int connection=0;

#define RDATA(xdata, xfunc, xcomment)  \
memcpy(requestdata, (xdata), sizeof(xdata)); \
ncp_request(0x2222, sequence, connection, 1, 0, \
                (xfunc), sizeof(data), (xcomment))

#define ODATA(xfunc, xcomment)  \
ncp_request(0x2222, sequence, connection, 1, 0, \
                (xfunc), 0, (xcomment))

#define VDATA(xfunc, xsize, xcomment)  \
ncp_request(0x2222, sequence, connection, 1, 0, \
                (xfunc), (xsize), (xcomment))


static int get_conn_nr(void)
{
  ncp_request(0x1111, sequence, 0xff, 0, 0xff, 0,
                      0, "Get Connection Nr.");
  if (!handle_event()) {
    connection = ncpresponse->connection;
    XDPRINTF((1, 0, "NWCLIENT GOT CONNECTION NR:%d", connection));
    return(0);
  }
  return(-1);
}

static int get_pkt_size(void)
{
  uint8 data[] = {0x4, 0};  /* wanted ?? SIZE */
  RDATA(data, 0x21, "Get Pktsize");
  if (!handle_event()) {
    XDPRINTF((1,0, "NWCLIENT GOT PACKET SIZE =:%d", (int)GET_BE16(responsedata)));
    return(0);
  }
  return(-1);
}

static int get_server_info(void)
{
  uint8  data[] = {0, 1, 0x11};
  RDATA(data, 0x17, "Get FileServer Info");
  if (!handle_event()) {
    XDPRINTF((1,0, "NWCLIENT GOT SERVER INFO von=:%s", responsedata ));
    return(0);
  }
  return(-1);
}

static int do_17_17(void)
{
  uint8  data[] = {0, 1, 0x17};
  RDATA(data, 0x17, "Do_17_17");
  if (!handle_event()) {
    return(0);
  }
  return(-1);
}

static int get_network_serial_number(void)
{
  uint8  data[] = {0, 1, 0x12};
  RDATA(data, 0x17, "Get Network Serial Number");
  if (!handle_event()) return(0);
  return(-1);
}


static int get_connect(void)
{
  ODATA(0x19, "get CONNECT ??");
  if (!handle_event()) {
    return(0);
  }
  return(-1);
}

static int get_server_time(void)
{
  ODATA(0x14, "get SERVER TIME");
  if (!handle_event()) {
    return(0);
  }
  return(-1);
}

typedef struct {
  uint8  volume;
  uint8  dir_id[2];
} DIR_IDS;

static int file_search_init(DIR_IDS *di, int dirhandle, char *path)
{
  uint8  *p=requestdata;
  int    pathlen=path ? strlen(path) : 0;
  *p++  = (uint8) dirhandle;
  *p++  = (uint8) pathlen;
  if (pathlen) memcpy(p, path, pathlen);
  VDATA(0x3e, pathlen+2, "FILE SEARCH INIT");
  if (!handle_event()) {
    if (di) memcpy(di, responsedata, 3);
    XDPRINTF((1,0, "NWCLIENT GOT FILES SEARCH INIT HANDLE=:%d",
         (int)GET_BE16(responsedata+1) ));
    return( (int) *(responsedata+3) );     /* access */
  }
  return(-1);
}

static int file_search_cont(DIR_IDS *di, int seq,
                                         int attrib, char *path)
{
  uint8  *p=requestdata;
  int    pathlen=path ? strlen(path) : 0;
  memcpy(p, di, 3);
  p+=3;
  U16_TO_BE16((uint16)seq, p);
  p+=2;
  *p++  = (uint8)  attrib;
  *p++  = (uint8)  pathlen;
  if (pathlen) memcpy(p, path, pathlen);
  VDATA(0x3f, pathlen+7, "FILE SEARCH CONT");
  if (!handle_event()) {
    int dir_id = GET_BE16(responsedata+2);
    seq        = GET_BE16(responsedata);
    XDPRINTF((1, 0, "GOT SEARCH CONT dir_id=%d, seq=%d", dir_id, seq));
    return(seq);
  }
  return(-1);
}

static int allocate_dir_handle(int dirhandle,
       	   		      int drive,
       	   		      char *path,
       	   		      int temp)
{
  uint8  *p=requestdata;
  uint8  pathlen= (path) ? strlen(path) : 0;
  *p++ = 0;
  *p++ = pathlen+4;

  if (!temp) *p++ = 0x12;          /* permanent  */
  else if (temp == 1) *p++=0x13;   /* temp       */
  else *p++ = 0x16;     	  /* spez. temp */
  *p++      = dirhandle;
  *p++      = drive;
  *p++      = pathlen;
  memcpy(p, path, pathlen);
  VDATA(0x16, pathlen+6 , "ALLOCATE DIR HANDLE");
  if (!handle_event()) return((int) *responsedata);
  return(-1);
}

static void scan_irgendwas(int dirhandle, int attrib, char *name)
{
   uint8  *p  = requestdata;
   int namlen = strlen(name);
   *p++       = 0;
   *p++       = namlen+6;
   *p++       = 0x0f;
   U16_TO_BE16(MAX_U16, p);
   p+=2;
   *p++       = dirhandle;
   *p++       = attrib;
   *p++       = namlen;
   memcpy(p, name, namlen);
   VDATA(0x16, namlen+8,  "SCAN IRGENDWAS");
   if (!handle_event()) {
     ;
   }
}

static void scan_file_trustees(int dirhandle, int attrib, char *name)
{
   uint8  *p  = requestdata;
   int namlen = strlen(name);
   *p++       = 0;
   *p++       = namlen+8;
   *p++       = 0x1e;
   *p++       = dirhandle;
   *p++       = attrib;
   U32_TO_BE32(MAX_U32, p);
   p+=4;
   *p++       = namlen;
   memcpy(p, name, namlen);
   VDATA(0x16, namlen+10,  "SCAN FILE TRUST");
   if (!handle_event()) {
     ;
   }
}


static int get_dir_path(int dirhandle)
{
  uint8  *p=requestdata;
  *p++ = 0;
  *p++ = 2;
  *p++ = 1;
  *p   = (uint8) dirhandle;
  VDATA(0x16, 4, "GET DIR PATH");
  if (!handle_event()) {
    ;
  }
  return(0);
}

static void get_connection_info(int conn)
/* liefert Connection INFO */
{
  uint8  *p=requestdata;
  *p++ = 0;
  *p++ = 2;
  *p++ = 0x16;
  *p   = (uint8) conn;
  VDATA(0x17, 4, "GET CONNECTION INFO");
  if (!handle_event()) {
    ;;
  }
}


static int get_bindery_access(void)
/* liefert ACCESS LEVEL und CONNECTION BIND ID */
{
  uint8  data[] = {0, 1, 0x46};
  RDATA(data, 0x17, "Get Bindery ACCESS ??");
  if (!handle_event()) {
    return(0);
  }
  return(-1);
}

static int scan_bindery_object(int type, char *name, uint32 lastid)
{
   uint8  *p  = requestdata;
   int namlen = strlen(name);
   *p++       = 0;
   *p++       = namlen+8;
   *p++       = 0x37;
   U32_TO_BE32(lastid, p);
   p+=4;
   U16_TO_BE16(type, p);
   p+=2;
   *p++=namlen;
   memcpy(p, name, namlen);
   VDATA(0x17, namlen+10,"Scan Bindery Object");
   if (!handle_event()) {
     ;
   }
   return(0);
}

static int scan_bindery_property(int type, char *name, char *propname, uint32 *lastid)
{
   uint8  *p      = requestdata;
   int namlen     = strlen(name);
   int propnamlen = strlen(propname);
   *p++       	  = 0;
   *p++       	  = namlen+9 + propnamlen;
   *p++       	  = 0x3c;
   U16_TO_BE16(type, p);
   p+=2;
   *p++       	  = namlen;
   memcpy(p, name, namlen);
   U32_TO_BE32(*lastid, (p+=namlen));

   *(p+=4)   = propnamlen;
   memcpy(++p, propname, propnamlen);

   VDATA(0x17, namlen+propnamlen+11,"Scan Bindery Property");

   if (!handle_event()) {
     /*
     *lastid = GET_BE32(responsedata + 20);
     */
     *lastid = GET_BE32(responsedata + 18);

     return(0);
   } else return(-1);
}



static int get_bindery_object_id(int type, char *name)
{
   uint8  *p  = requestdata;
   int namlen = strlen(name);
   *p++       = 0;
   *p++       = namlen+4;
   *p++       = 0x35;
   U16_TO_BE16(type, p);
   p+=2;
   *p++=namlen;
   memcpy(p, name, namlen);
   VDATA(0x17, namlen+6, "GET BINDERY OBJECT ID");
   if (!handle_event()) {
     XDPRINTF((1, 0, "GOT BIND OBJ ID=0x%lx", GET_BE32(responsedata)));
   }
   return(0);
}

static void send_console_broadcast(char *message)
{
  uint8  *p  = requestdata;
  int    len = strlen(message);
  *p++       = 0;
  *p++       = len+3;
  *p++       = 0xd1;
  U16_TO_BE16(len, p);
  p+=2;
  memcpy(p, message, len);
  VDATA(0x17, len+5, "SEND CONSOLE BROADCAST");
  if (!handle_event()) {
    ;;
  }
}



static int get_bindery_object_name(uint32 id)
{
   uint8  *p = requestdata;
   *p++      = 0;
   *p++      = 5;
   *p++      = 0x36;
   U32_TO_BE32(id, p);
   VDATA(0x17, 7, "GET BINDERY OBJECT NAME");
   if (!handle_event()) {
     ;
   }
   return(0);
}


static int get_volume_restriction_for_obj(uint32 id, int volnr)
{
   uint8  *p  = requestdata;
   *p++       = 0;
   *p++       = 6;
   *p++       = 0x29;
   *p++       = (uint8)volnr;
   U32_TO_BE32(id, p);
   VDATA(0x16, 8, "GET VOLUME RESTRICTION FOR OBJ");
   if (!handle_event()) {
     ;
   }
   return(0);
}


static int login_object(int type, char *name, char *password)
{
   uint8  *p    = requestdata;
   int namlen   = strlen(name);
   int passlen  = (password) ? strlen(password) : 0;
   *p++      	= 0;
   *p++      	= namlen+passlen+5;
   *p++      	= 0x14;
   U16_TO_BE16(type, p);
   p+=2;
   *p++ = namlen;
   memcpy(p, name, namlen);
   p += namlen;
   if (passlen) memcpy(p, password, passlen);
   else *p=0;
   VDATA(0x17, namlen+7+passlen, "LOGIN OBJECT");
   if (!handle_event()) {
     ;
   }
   return(0);
}


static void test_xx()
{
  uint8 data[] = {0x0,0x1c,0xf,0xff,0xff,0x0,0x0,0x16,'S','Y','S',':','S','Y','S','T','E','M','\\','N','E','T','$','O','B','J','.','O','L','D'} ;
  RDATA(data, 0x17, "test_xx");
  if (!handle_event()) {
    ;
  }
}



static int open_datei(int dirhandle, int attrib, int ext_attrib, char *name)
{
   uint8  *p  = requestdata;
   int namlen = strlen(name);
   *p++       = dirhandle;
   *p++       = attrib;
   *p++       = ext_attrib;
   *p++       = namlen;
   memcpy(p, name, namlen);
   VDATA(0x4c, namlen+4,  "OPEN_DATEI");
   if (!handle_event()) {
     return( (int) GET_BE32(responsedata));
   } else return(-1);
}

static int read_datei(int fh, int offs, int size)
{
   uint8  *p  = requestdata;
   *p++       = 0;
   U32_TO_BE32(fh, p);
   p+=6;
   U32_TO_BE32(offs, p);
   p+=4;
   U16_TO_BE16(size, p);
   VDATA(0x48, 13,  "READ_DATEI");
   if (!handle_event()) {
     return( (int) GET_BE16(responsedata));
   } else return(-1);
}




static void test1(void)
{
  int dirhandle = allocate_dir_handle(0, 'F', "SYS:PUBLIC", 0);
  if (dirhandle > -1) {
    scan_file_trustees(dirhandle, 6, "NET$LOG.DAT");
    scan_irgendwas(dirhandle, 6,     "NET$LOG.DAT");
  }
}

static void test2(void)
{
   DIR_IDS di;
   if (file_search_init(&di, 1, "\\MAIL") > -1) {
       file_search_cont(&di, 0xffff, 0x10, "\252");
   }
}


static void teste_reads(void)
{
  int fh = open_datei(0, 0x4e, 0x11, "SYS:/LOGIN/SLIST.EXE");
  int gelesen=0;
  if (fh > -1) {
    int offs=0;
    int size = read_datei(fh, offs, 0x200);
    while (size > 0) {
      offs +=size;
      gelesen+=size;
      size = read_datei(fh, offs, 0x200);
    }
  }
  XDPRINTF((1,0, "%d Bytes readed", gelesen));
}

static void test_wdog(void)
{
  struct           t_unitdata ud;
  ipxAddr_t        source_adr;
  IPX_DATA    	   ipx_data_buff;
  uint8            ipx_pack_typ;
  int              flags = 0;

  ud.opt.len       = sizeof(ipx_pack_typ);
  ud.opt.maxlen    = sizeof(ipx_pack_typ);
  ud.opt.buf       = (char*)&ipx_pack_typ; /* bekommt aktuellen Typ */

  ud.addr.len      = sizeof(ipxAddr_t);
  ud.addr.maxlen   = sizeof(ipxAddr_t);

  ud.addr.buf      = (char*)&source_adr;

  ud.udata.len     = sizeof(IPX_DATA);
  ud.udata.maxlen  = sizeof(IPX_DATA);
  ud.udata.buf     = (char*)&ipx_data_buff;
  while (1) {
    if (t_rcvudata(fd_wdog, &ud, &flags) < 0){
      struct t_uderr uderr;
      ipxAddr_t  erradr;
      uint8      err_pack_typ;
      uderr.addr.len      = sizeof(ipxAddr_t);
      uderr.addr.maxlen   = sizeof(ipxAddr_t);
      uderr.addr.buf      = (char*)&erradr;
      uderr.opt.len       = sizeof(err_pack_typ);
      uderr.opt.maxlen    = sizeof(err_pack_typ);
      uderr.opt.buf       = (char*)&err_pack_typ; /* bekommt aktuellen Typ */
      ud.addr.buf         = (char*)&source_adr;
      t_rcvuderr(fd_ipx, &uderr);
      XDPRINTF((1,0, "Error from %s, Code = 0x%lx", visable_ipx_adr(&erradr), uderr.error));
      if (nw_debug) t_error("t_rcvudata !OK");
      return;
    } else {
      XDPRINTF((1,0, "WDOG Packet von:%s, len=%d connid=%d, status=%d",
                 visable_ipx_adr(&source_adr),
               (int)ud.udata.len, (int) ipx_data_buff.wdog.connid,
               (int)ipx_data_buff.wdog.status));
      if (ipx_data_buff.wdog.status == '?') {
        ipx_data_buff.wdog.status  = 'Y';
        send_ipx_data(fd_wdog, 17, 2,
	               (char *) &ipx_data_buff,
	               &source_adr, "WDOG REPLY");
      }
    }
  }
}

/* --------------------------------------------------------- */
int main(int argc, char **argv)
{
  nw_debug = 1; /* dieses Modul dient nur zum Debuggen !! */

  if (argc != 3) {
    fprintf(stderr, "usage: nwclient MY_ADDR SERVER_ADDR\n");
    exit(1);
  }

  XDPRINTF((1, 0, "NWCLIENT MYADDR=%s, SERVER=%s", *(argv+1), *(argv+2) ));

  adr_to_ipx_addr(&my_addr,   *(argv+1));
  adr_to_ipx_addr(&serv_addr, *(argv+2));

  if (init_client()) exit(1);
  /* ------------------------------------------ */

  get_conn_nr();
  get_server_info();
  get_pkt_size();
  get_connect();
  get_server_time();

  file_search_init(NULL, 1, NULL);
  get_bindery_access();
  get_bindery_object_id(1, "SUPERVISOR");

  do_17_17();

  login_object(1, "SUPERVISOR", NULL);

  get_network_serial_number();
  get_bindery_access();

  scan_bindery_object(1, "*", MAX_U32);
  scan_bindery_object(1, "*", 1);

  {
    uint32 lastid = MAX_U32;
    while (!scan_bindery_property(1, "NOBODY", "*", &lastid));;
  }
  get_volume_restriction_for_obj(1, 0);

  test1();
  test2();

  get_connection_info(0);
  get_connection_info(1);
  get_connection_info(2);
  get_connection_info(3);

  send_console_broadcast("Hello Console !!!!");

  teste_reads();


  test_wdog();
  /*-----------------------------------------------*/
  t_unbind(fd_ipx);
  t_close(fd_ipx);
  t_unbind(fd_wdog);
  t_close(fd_wdog);
  return(0);
}

