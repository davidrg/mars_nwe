/* nwbind.c */
#define REVISION_DATE "09-Mar-98"
/* NCP Bindery SUB-SERVER */
/* authentification and some message handling */

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
#include "net.h"
#include "nwdbm.h"
#include "unxlog.h"
#include "nwbind.h"
#include "nwqueue.h"
#include "sema.h"

/* next should be '1', is for testing only */
#define USE_PERMANENT_OUT_SOCKET  1

static  char       my_nwname[50];
static  ipxAddr_t  my_addr;
static  time_t     akttime;
static  int        ipx_out_fd=-1;

static  int        ncp_fd      = 0; /* stdin */
static  struct     t_unitdata ud;
static  uint8      ipx_in_data[IPX_MAX_DATA];
static  uint8      ipx_pack_typ = 17;
static  int        rcv_flags    =  0;
static  ipxAddr_t  from_addr;    /* actual calling address  */
static  NCPREQUEST *ncprequest  = (NCPREQUEST*)&ipx_in_data;
static  int         server_goes_down=0;

static void write_to_nwserv(int what, int connection, int mode,
                                char *data, int size)
{
  switch (what) {
    case 0x4444  : /* tell the wdog there's no need to look  0 */
                   /* activate wdogs to free connection      1 */
                   /* the connection ist closed        	    99 */
                   write(FD_NWSERV, &what,       sizeof(int));
                   write(FD_NWSERV, &connection, sizeof(int));
                   write(FD_NWSERV, &mode,       sizeof(int));
                   break;

    case 0x6666  : /* send to client that server holds message */
                   write(FD_NWSERV, &what,       sizeof(int));
                   write(FD_NWSERV, &connection, sizeof(int));
                   break;

    case 0xffff  : /* tell nwserv to down the server */
                   write(FD_NWSERV, &what, sizeof(int));
                   write(FD_NWSERV, &what, sizeof(int));
                   break;

    default     :  break;
  }
}

#define nwserv_reset_wdog(connection) \
   write_to_nwserv(0x4444, (connection), 0,  NULL, 0)

#define nwserv_handle_msg(connection) \
   write_to_nwserv(0x6666, (connection), 0, NULL, 0)

#define nwserv_down_server() \
   write_to_nwserv(0xffff, 0, 0, NULL, 0)

static int        max_nw_vols=MAX_NW_VOLS;
static int        max_connections=MAX_CONNECTIONS;
static CONNECTION *connections=NULL;
static CONNECTION *act_c=(CONNECTION*)NULL;
static int        act_connection;
static int        internal_act=0;

int b_acc(uint32 obj_id, int security, int forwrite)
{
  /* security levels
   * 0 = anyone have access.
   * 1 = all logged  have access
   * 2 = object logged have access
   * 3 = only supervisor has access
   * 4 = only internal access.
   */
  char *acc_what=NULL;
  char *acc_typ=NULL;
  int  errcode =0;
  XDPRINTF((5,0, "b_acc for id=0x%lx, security=0x%x, forwrite=0x%x",
          obj_id, security, forwrite));
  if (internal_act || !act_c) return(0); /* allways full access to internal routines */
  if (forwrite & 0xf) security >>= 4; /* writesecurity */
  security &= 0xf;
  if (!security) return(0);     /* rights for all */
  else if (security == 1) {
    if (act_c->object_id > 0) return(0);  /* rights for all logged */
  } else if (security == 2) {
    if (  act_c->object_id == obj_id
       || act_c->object_id == 1 ) return(0); /* rights for the user */
  } else if (security == 3 && act_c->object_id == 1) return(0);

  switch (forwrite&0xf) {
    case 0 : acc_what = "read";   break;
    case 1 : acc_what = "write";  break;
    case 2 : acc_what = "creat";  break;
    case 3 : acc_what = "delete"; break;
    case 4 : acc_what = "rename"; break;
    case 5 : acc_what = "change security"; break;
    default : acc_what = "?" ; break;
  }

  switch ( (forwrite >> 4) & 0xf) {
    case 0  : acc_typ  = "obj" ;   break;
    case 1  : acc_typ  = "prop";   break;
    default : acc_typ  = "?";      break;
  }

  switch (forwrite) {
    case 0x00 : errcode = -0xf2; break;
    case 0x01 : errcode = -0xf8; break;  /* should be changed */
    case 0x02 : errcode = -0xf5; break;
    case 0x03 : errcode = -0xf4; break;
    case 0x04 : errcode = -0xf3; break;

    case 0x10 : errcode = -0xf9; break;
    case 0x11 : errcode = -0xf8; break;
    case 0x12 : errcode = -0xf7; break;
    case 0x13 : errcode = -0xf6; break;

    default   : errcode = -0xff; break;
  }
  XDPRINTF((2, 0, "b_acc no rights for 0x%x to %s %s",
                  act_c->object_id, acc_what, acc_typ));
  return(errcode);
}

static void sent_down_message(void)
{
  int k = -1;
  server_goes_down++;
  while (++k < max_connections) {
    CONNECTION *cn=&connections[k];
    if (cn->active) {
      strmaxcpy(cn->message, "SERVER IS GOING DOWN", 58);
      nwserv_handle_msg(k+1);
    }
  } /* while */
}

static void open_clear_connection(int conn, int activate, uint8 *addr)
{
  if (conn > 0 && --conn < max_connections) {
    CONNECTION *c = &connections[conn];
    c->active     = activate;
    c->message[0] = '\0';
    c->t_login    = 0;
    
    if (activate && addr) {
      memcpy(&(c->client_adr), addr, sizeof(ipxAddr_t));
      c->send_to_sock = GET_BE16(addr+sizeof(ipxAddr_t));
      c->pid_nwconn   = GET_BE32(addr+sizeof(ipxAddr_t)+sizeof(uint16));
    } else { /* down connection */
      if (c->object_id)
        write_utmp(0, conn+1, c->pid_nwconn,  &(c->client_adr), NULL);
      if (c->count_semas) {
        clear_conn_semas(c);
      }
    }
    c->object_id  = 0;
  }
}

static void get_login_time(uint8 login_time[], CONNECTION *cx)
{
  struct tm *s_tm = localtime(&(cx->t_login));

  login_time[0] = s_tm->tm_year;
  login_time[1] = s_tm->tm_mon+1;
  login_time[2] = s_tm->tm_mday;

  login_time[3] = s_tm->tm_hour;
  login_time[4] = s_tm->tm_min;
  login_time[5] = s_tm->tm_sec;
  login_time[6] = s_tm->tm_wday;
}

static int build_login_response(uint8 *responsedata, uint32 obj_id)
{
  uint8 pw_name[40];
  int result;
  act_c->object_id = obj_id;     /* actuell Object ID  */
  act_c->t_login   = akttime;    /* and login Time     */
  
  internal_act=1;
  get_guid((int*) responsedata,
           (int*) (responsedata+sizeof(int)),
           obj_id, pw_name);
  *((uint32*) (responsedata+2*sizeof(int))) = obj_id;

  result = get_home_dir(responsedata + 3 * sizeof(int)+1, obj_id);
  *(responsedata + 3 * sizeof(int)) = (uint8) result;
  result = 3 * sizeof(int) + 1 + (int) *(responsedata+ 3 * sizeof(int));
  write_utmp(1, act_connection, act_c->pid_nwconn,
                 &(act_c->client_adr), pw_name);
  internal_act=0;
  return(result);
}

static void handle_fxx(int gelen, int func)
{
  IPX_DATA     ipxoutdata;
  NCPRESPONSE  *ncpresponse  = (NCPRESPONSE*)&ipxoutdata;
  uint8        *responsedata = ((uint8*)&ipxoutdata)+sizeof(NCPRESPONSE);
  uint8        *requestdata  = ((uint8*)ncprequest)+sizeof(NCPREQUEST);
#if 0
  uint8        len           = *(requestdata+1);
#endif

  uint8        ufunc; 
  uint8        *rdata;
  uint8        completition  = 0;
  uint8        connect_status= 0;
  int          data_len      = 0;

  if (func==0x19) {
    ufunc   = 0;
    rdata   = requestdata;
  } else if (func==0x20) {
    ufunc   = *requestdata;
    rdata   = requestdata+1;
  } else {
    ufunc   = *(requestdata+2);
    rdata   = requestdata+3;
  }

  MDEBUG(D_BIND_REQ, {
    int j = gelen - sizeof(NCPREQUEST);
    XDPRINTF((1, 0, "NCP 0x%x REQUEST:ufunc:0x%x", func, ufunc));
    if (j > 0){
      uint8  *p=requestdata;
      XDPRINTF((1, 2, "len %d, DATA:", j));
      while (j--) {
        int c = *p++;
        if (c > 32 && c < 127)  XDPRINTF((1, 3, ",\'%c\'", (char) c));
        else XDPRINTF((1, 3, ",0x%x", c));
      }
      XDPRINTF((1, 1, NULL));
    }
  })

  if (0x15 == func) {
    switch (ufunc) {  /* Messages */
      case 0x0 :  {   /* Send Broadcast Message (old) */
        int count_conns   = (int)*(rdata);        /* Number of connections */
        uint8  *conns   = rdata+1;              /* connectionslist */
        int msglen      = *(conns+count_conns);
        uint8 *msg      = conns+count_conns+1;
        uint8 *p        = responsedata;
        int   one_found = 0;
        int   k         = -1;
        *p++            = (uint8) count_conns;
        while (++k < count_conns) {
          int connr   =  (int) (*conns++);
          int result  =  0xff; /* target not ok */
          CONNECTION *cn;
          if (connr > 0 && --connr < max_connections
            && ((cn = &connections[connr]))->active ) {
            if (!cn->message[0]) {
              strmaxcpy(cn->message, msg, min(58, msglen));
              result      = 0;    /* Ok */
            } else result = 0xfc; /* server holds message */
            nwserv_handle_msg(connr+1);
            one_found++;
          }
          *p++ = (uint8)result;
        }
        if (one_found) data_len = count_conns+1;
        else completition=0xff;
      }
      break;

      case 0x01:  { /* Get Broadcast Message (old) */
        *responsedata = (uint8) strmaxcpy(responsedata+1, act_c->message, 58);
        act_c->message[0] = '\0';
        data_len = (int)(*responsedata) + 1;
      }
      break;

      case 0x03:  { /* Enable Broadcasts */
        ;;;
        XDPRINTF((2, 0, "TODO: enable Broadcasts"));
      }
      break;

      case 0x09:  { /* Broadcast to CONSOLE */
        char message[60];
        strmaxcpy(message, rdata+1, min(59, *rdata));
        fprintf(stderr, "\n:%s\n", message);
      }
      break;

      case 0xa:  /* Send Broadcast Message (new) */
      case 0xb:  /* Get Broadcast Message (new) */
      default :   completition=0xfb; /* not handled  */
    } /* switch */
  } else if (0x16 == func) {
    switch (ufunc) {
      /*  QUOTA support from: Matt Paley  */
      case 0x21 :    /* Change volume restrictions */
      case 0x22 :    /* Remove volume restrictions */
      case 0x29 : {  /* Read volume restrictions */
        /* Returns 3 integers, uid, gid, 0=OK/1=Permission denied */
        uint32 id = GET_BE32(rdata+1);
        internal_act=1;
        if (get_guid((int*) responsedata, (int*)(responsedata+sizeof(int)),
		     id, (char *) NULL) != 0) {
	  completition = 0xff;
          XDPRINTF((2, 0, "quota id-uid mapping failure %d 0x%x", ufunc, id));
        }
        internal_act=0;
        /* OK if supervisor or trying to read (0x29) own limits */
        if (act_c->object_id == 1 ||
	    (act_c->object_id == id && ufunc == 0x29))
	  ((int *) responsedata)[2] = 0; /* OK */
        else
	  ((int *) responsedata)[2] = 1; /* Fail */
        data_len = sizeof(int)*3;
      }
      break;
      default :   completition=0xfb; /* not handled  */
    }
  } else if (0x17 == func) { /* Fileserver Enviro */
    switch (ufunc) {
     case 0x01 :  { /* Change User Password OLD */
                     completition=0xff;
                  }
                  break;

#if FUNC_17_02_IS_DEBUG
     case 0x02 :  { /* I hope this is call isn't used    */
                    /* now missused as a debug switch :) */

                     struct XDATA {
                       uint8  nw_debug;   /* old level */
                     } *xdata = (struct XDATA*) responsedata;

                     if (*rdata == NWBIND) {
                       xdata->nw_debug = (uint8) nw_debug;
                       nw_debug = (int) *(rdata+1);
                       data_len = 1;
                     } else completition=0xff;
                  }
                  break;
#endif

     case 0x0c :  { /* Verify Serialization */
                     completition=0xff;
                  }
                  break;

     case 0x0e :  { /* Get Disk Utilization */
                       completition=0xff;
                    }
                    break;
#if 0
     case 0x10 :  set file information. handled in nwconn.
#endif

     case 0x11 :  { /* Get FileServer Info */
                     struct XDATA {
                       uint8 servername[48];
                       uint8 version;    /* 2  or   3 */
                       uint8 subversion; /* 15 or  11 */
                       uint8 maxconnections[2];
                       uint8 connection_in_use[2];
                       uint8 max_volumes[2];
                       uint8 os_revision;  /* 0 */
                       uint8 sft_level;    /* 2 */
                       uint8 tts_level;    /* 1 */
                       uint8 peak_connection[2];
                       uint8 accounting_version;  /* 1 */
                       uint8 vap_version;         /* 1 */
                       uint8 queuing_version;     /* 1 */
                       uint8 print_server_version;  /* 0 */
                       uint8 virtual_console_version; /* 1 */
                       uint8 security_level;          /* 1 */
                       uint8 internet_bridge_version;  /* 1 */
                       uint8 reserved[60];
                     } *xdata = (struct XDATA*) responsedata;
                     int k, i, h;
                     memset(xdata, 0, sizeof(struct XDATA));
                     strcpy(xdata->servername, my_nwname);
                     if (!tells_server_version) {
                       xdata->version    =  2;
                       xdata->subversion = 15;
                     } else {
                       xdata->version    =  3;
                       xdata->subversion = (tells_server_version == 2)
                                              ? 12
                                              : 11;
                     }

                     i=0;
                     h=0;
                     for (k=0; k < max_connections; k++) {
                       if (connections[k].active) {
                         i++;
                         h = k+1;
                       }
                     }
                     U16_TO_BE16(i, xdata->connection_in_use);
                     U16_TO_BE16(max_connections, xdata->maxconnections);
                     U16_TO_BE16(h,               xdata->peak_connection);
                     U16_TO_BE16(max_nw_vols,     xdata->max_volumes);
                     xdata->security_level=1;
                       /*
                        * if this level is 0
                        * you cannot install access restrictions.
                        */

#ifdef _MAR_TESTS_1
                     xdata->sft_level=2;
                     xdata->tts_level=1;
                     xdata->accounting_version=1;
                     xdata->vap_version=1;
                     xdata->queuing_version=1;
                     xdata->virtual_console_version=1;
                     xdata->security_level=1;
                     xdata->internet_bridge_version=1;
#endif
                     data_len = sizeof(struct XDATA);
                   }
                   break;

     case 0x12 :  { /* Get Network Serial Number */
                     struct XDATA {
                       uint8 serial_number[4];
                       uint8 appl_number[2];
                     } *xdata = (struct XDATA*) responsedata;
                     /* serial-number 4-Byte */
                     U32_TO_BE32(network_serial_nmbr, xdata->serial_number);
                     /* application-number 2-Byte */
                     U16_TO_BE16(network_appl_nmbr,  xdata->appl_number);
                     data_len = sizeof(struct XDATA);
                    }
                  break;

     case 0x13 :    /* Get Connection Internet Address, old */
     case 0x1a :  { /* Get Connection Internet Address, new */
                    int conn  = (ufunc == 0x13)
                          ? ((max_connections < 256) 
                              ? (int) *rdata
                              : act_connection)
                          : GET_32(rdata);
                    if (conn >0 && conn <= max_connections
                      && connections[conn-1].active ) {
                      CONNECTION *cx=&(connections[conn-1]);
                      data_len = sizeof(ipxAddr_t);
                      memcpy(responsedata, (char*)&(cx->client_adr), data_len);
                      if (ufunc==0x1a) {
                        *(responsedata+data_len)=0x02; /* NCP connection */
                        data_len++;
                      }
                    } else {
                      XDPRINTF((1, 0, "Get Connection Internet Adress, Conn:%d of %d failed",
                               conn, max_connections));
                      completition = 0xff;
                    }
                  } break;

     case 0x14 :  { /* Login Objekt, unencrypted passwords */
                    uint8  *p    =  rdata;
                    uint8  *p1   =  p+3 + *(p+2);  /* here is password */
                    int    result;
                    NETOBJ obj;
                    char   password[80];
                    obj.type     =  GET_BE16(p);
                    xstrmaxcpy(obj.name, p+3, (int) *(p+2));
                    upstr(obj.name);
                    xstrmaxcpy(password, p1+1, (int) *p1);
                    if (*p1) memset(p1+1, 0, *p1);
                    XDPRINTF((10, 0, "LOGIN unencrypted PW NAME='%s', PASSW='%s'",
                             obj.name, password));
                    if (0 == (result = find_obj_id(&obj))) {
                      if (password_scheme & PW_SCHEME_LOGIN) {
#if 0
                        if (obj.id == 1) {
                          result=-0xff; /* SUPERVISOR ever encryted !! */
                          XDPRINTF((1, 0, "Supervisor tried unencrypted LOGIN"));
                        } else
#endif
                        {
                          internal_act = 1;
                          result=nw_test_unenpasswd(obj.id, password);
                          internal_act = 0;
                        }
                      } else {
                        XDPRINTF((1, 0, "unencryted logins are not enabled"));
                        result=-0xff;
                      }
                    }
                    if (!result) {
                      internal_act = 1;
                      result = nw_test_adr_time_access(obj.id, &(act_c->client_adr));
                      internal_act = 0;
                    }
                    memset(password, 0, 50);
                    if (!result)
                      data_len = build_login_response(responsedata, obj.id);
                    else
                      completition = (uint8) -result;
                  } break;

     case 0x15 :  { /* Get Object Connection List (old) */
                    uint8  *p           =  rdata;
                    int    result;
                    NETOBJ obj;
                    obj.type            =  GET_BE16(p);
                    p+=2;
                    strmaxcpy((char*)obj.name,  (char*)(p+1), (int) *(p));
                    upstr(obj.name);
                    result = find_obj_id(&obj);
                    if (!result){
                      int k=-1;
                      int count  = 0;
                      p = responsedata+1;
                      while (++k < max_connections && count < 255) {
                        CONNECTION *cn= &connections[k];
                        if (cn->active && cn->object_id == obj.id) {
                          *p++=(uint8)k+1;
                          count++;
                        }
                      } /* while */
                      *responsedata = count;
                      data_len = 1 + count;
                    } else completition=(uint8)-result;
                  }
                  break;

     case 0x16 :    /* Get Connection Info, old  */
     case 0x1c :  { /* Get Connection Info, new ( > 255 connections) */
                    struct XDATA {
                      uint8 object_id[4];
                      uint8 object_type[2];
                      uint8 object_name[48];
                      uint8 login_time[7];
                      uint8 reserved;
                    } *xdata = (struct XDATA*) responsedata;
                    int conn  = (ufunc == 0x16)
                          ? ((max_connections < 256) 
                              ? (int) *rdata
                              : act_connection)
                          : GET_32(rdata);
                    memset(xdata, 0, sizeof(struct XDATA));
                    data_len = sizeof(struct XDATA);
                    if (conn && conn <= max_connections
                           && connections[conn-1].active ) {
                      CONNECTION *cx=&(connections[conn-1]);
                      NETOBJ    obj;
                      int       result;
                      obj.id =  cx->object_id;
                      result =  nw_get_obj(&obj);
                      if (!result) {
                        memset(xdata, 0, sizeof(struct XDATA));
                        U32_TO_BE32(obj.id,   xdata->object_id);
                        U16_TO_BE16(obj.type, xdata->object_type);
                        strncpy(xdata->object_name, obj.name, 48);
                        get_login_time(xdata->login_time, cx);
                      } /*  else completition = (uint8)(-result); */
                    } else if (!conn || conn > max_connections) {
                      data_len     = 0;
                      completition = 0xfd;
                    }
                  } break;

     case 0x17 :  { /* get crypt key */
                    int    k   = sizeof(act_c->crypt_key);
                    uint8 *p   = act_c->crypt_key;
                    uint8 *pp  = responsedata;
                    data_len   = k;
                    while (k--) *pp++ = *p++ = (uint8) rand();

                    /* if all here are same (1 or 2) then the resulting key is */
                    /* 00000000  */
                    if (password_scheme & PW_SCHEME_GET_KEY_FAIL)
                       completition=0xfb;
                  }
                  break;

     case 0x18 :  { /* crypt_keyed LOGIN */
                    uint8 *p     =  rdata+sizeof(act_c->crypt_key);
                    NETOBJ obj;
                    int    result;
                    obj.type     =  GET_BE16(p);
                    obj.id       =  0;
                    xstrmaxcpy(obj.name, (char*)(p+3), *(p+2));
                    upstr(obj.name);
                    XDPRINTF((2, 0, "LOGIN CRYPTED PW NAME='%s'",obj.name));

                    if (0 == (result = find_obj_id(&obj))) {
                      internal_act = 1;
                      result=nw_test_passwd(obj.id, act_c->crypt_key, rdata);
                      internal_act = 0;
                    }
                    if (result > -1) {
                      internal_act = 1;
                      result = nw_test_adr_time_access(obj.id, &(act_c->client_adr));
                      internal_act = 0;
                    }
                    if (result > -1)
                      data_len = build_login_response(responsedata, obj.id);
                    else
                      completition = (uint8) -result;
                    /*
                     * completition = 0xde means login time has expired
                     * completition = 0xdf means good login, but
                     * login time has expired
                     * perhaps I will integrate it later.
                     */
                  }
                  break;


     case 0x1B :  { /* Get Object Connection List */
                    uint8  *p           =  rdata;
                    int    result;
                    NETOBJ obj;
                    int    searchnr     =  (int) GET_BE32(p);
                    p+=4;
                    obj.type            =  GET_BE16(p);
                    p+=2;
                    strmaxcpy((char*)obj.name,  (char*)(p+1), (int) *(p));
                    upstr(obj.name);
                    result = find_obj_id(&obj);
                    if (!result){
                      int k    = max(-1, searchnr-1);
                      int count  = 0;
                      p = responsedata+1;
                      while (++k < max_connections && count < 255) {
                        CONNECTION *cn= &connections[k];
                        if (cn->active && cn->object_id == obj.id) {
                          U16_TO_16(k+1, p);  /* LO-HI !! */
                          p+=2;
                          count++;
                        }
                      } /* while */
                      *responsedata = count;
                      data_len = 1 + count*2;
                    } else completition=(uint8)-result;
                  }
                  break;


     case 0x32 :  {  /* Create Bindery Object */
                    NETOBJ obj;
                    int    result;
                    uint8  *p           =  rdata;
                    obj.flags           =  *p++;
                    obj.security        =  *p++;
                    obj.type            =  GET_BE16(p);
                    strmaxcpy((char*)obj.name, (char*)p+3, (int) *(p+2));
                    result = nw_create_obj(&obj, 0);
                    if (result < 0) completition = (uint8) -result;
                  } break;

     case 0x33 :  {  /* delete OBJECT */
                    uint8  *p           =  rdata;
                    int    result;
                    NETOBJ obj;
                    obj.type            =  GET_BE16(p);
                    strmaxcpy((char*)obj.name,  (char*)(p+3), (int) *(p+2));
                    result = nw_delete_obj(&obj);
                    if (result < 0) completition = (uint8) -result;
                  } break;

     case 0x34 :  {  /* rename OBJECT, only SU */
                    int result=-0xff;
                    if (1 == act_c->object_id) {
                      uint8  *p           =  rdata;
                      NETOBJ obj;
                      uint8  newname[256];
                      uint8  *p1  =  p+3 + *(p+2); /* new Name Length */
                      obj.type    =  GET_BE16(p);
                      strmaxcpy((char*)obj.name, (char*)(p+3),  (int) *(p+2));
                      strmaxcpy((char*)newname,  (char*)(p1+1), (int) *(p1));
                      result = nw_rename_obj(&obj, newname);
                    }
                    if (result) completition = (uint8) -result;
                  } break;


     case 0x35 :  {  /* get Bindery Object ID */
                    struct XDATA {
                      uint8 object_id[4];
                      uint8 object_type[2];
                      uint8 object_name[48];
                    } *xdata = (struct XDATA*) responsedata;
                    uint8  *p           =  rdata;
                    int    result;
                    NETOBJ obj;
                    obj.type            =  GET_BE16(p);
                    strmaxcpy((char*)obj.name,  (char*)(p+3), (int) *(p+2));
                    upstr(obj.name);
                    result = find_obj_id(&obj);
                    if (!result){
                      U32_TO_BE32(obj.id,   xdata->object_id);
                      U16_TO_BE16(obj.type, xdata->object_type);
                      strncpy(xdata->object_name, obj.name, 48);
                      data_len          = sizeof(struct XDATA);
                    } else completition = (uint8) -result;
                  } break;

     case 0x36 :  {  /* get Bindery Object Name */
                    struct XDATA {
                      uint8 object_id[4];
                      uint8 object_type[2];
                      uint8 object_name[48];
                    } *xdata = (struct XDATA*) responsedata;
                    uint8 *p = rdata;
                    int    result;
                    NETOBJ obj;
                    obj.id = GET_BE32(p);
                    result = nw_get_obj(&obj);
                    if (!result){
                      U32_TO_BE32(obj.id,   xdata->object_id);
                      U16_TO_BE16(obj.type, xdata->object_type);
                      strncpy(xdata->object_name, obj.name, 48);
                      data_len = sizeof(struct XDATA);
                    } else completition = (uint8) -result;
                  } break;

     case 0x37 :  {  /* Scan Bindery Object */
                    struct XDATA {
                      uint8 object_id[4];
                      uint8 object_type[2];
                      uint8 object_name[48];
                      uint8 object_flag;
                      uint8 object_security;
                      uint8 object_has_properties;
                    } *xdata = (struct XDATA*) responsedata;
                    uint32 last_obj_id  =  GET_BE32(rdata);
                    uint8  *p           =  rdata+4;
                    int    result;
                    NETOBJ obj;
                    obj.type            =  GET_BE16(p);
                    strmaxcpy((char*)obj.name, (char*)(p+3),(int) *(p+2));
                    upstr(obj.name);
                    result = scan_for_obj(&obj, last_obj_id, 0);
                    if (!result){
                      U32_TO_BE32(obj.id,    xdata->object_id);
                      U16_TO_BE16(obj.type,  xdata->object_type);
                      strncpy(xdata->object_name, obj.name, 48);
                      xdata->object_flag       = obj.flags;
                      xdata->object_security   = obj.security;
                      if (nw_obj_has_prop(&obj) > 0)
                         xdata->object_has_properties = 0xff;
                      else xdata->object_has_properties = 0;
                      data_len = sizeof(struct XDATA);
                    } else completition = (uint8) -result;
                  }
                  break;

     case 0x38 :  {  /* change Bindery Objekt Security */
                     /* only SU ! */
                    int result= -0xff;
                    if (1 == act_c->object_id) {
                      uint8  *p           =  rdata;
                      NETOBJ obj;
                      obj.type            =  GET_BE16(p+1);
                      xstrmaxcpy(obj.name,  (char*)(p+4), (int) *(p+3));
                      result = nw_change_obj_security(&obj, (int)*p);
                    }
                    if (result < 0) completition = (uint8) -result;
                  } break;

     case 0x39 :  {  /* create Property */
                    uint8  *p             = rdata;
                    int object_type       = GET_BE16(p);
                    int object_namlen     = (int) *(p+=2);
                    uint8 *object_name    = ++p;
                    int prop_flags        = (int) *(p+=object_namlen);
                    int prop_security     = (int) *(++p);
                    int prop_namlen       = (int) *(++p);
                    uint8 *prop_name      = ++p;
                    int result = nw_create_prop( object_type,
                                 object_name, object_namlen,
                                 prop_name,   prop_namlen,
                                 prop_flags,  prop_security);
                    if (result) completition = (uint8) -result;
                  } break;


     case 0x3a :  {  /* delete property */
                    uint8  *p         =  rdata;
                    int object_type   =  GET_BE16(p);
                    int object_namlen =  (int) *(p+2);
                    uint8 *object_name=  p+3;
                    int prop_namlen   =  (int) *(p+3+object_namlen);
                    uint8 *prop_name  =  p+4+object_namlen;
                    int result = nw_delete_property( object_type,
                                 object_name, object_namlen,
                                 prop_name, prop_namlen);
                    if (result < 0) completition = (uint8) -result;
                  } break;

     case 0x3b :  {  /* Change Prop Security */
                    uint8  *p         =  rdata;
                    int object_type   =  GET_BE16(p);
                    int object_namlen =  (int) *(p+=2);
                    uint8 *object_name=  ++p;
                    int prop_security =  (int) *(p+=object_namlen);
                    int prop_namlen   =  (int) *(++p);
                    uint8 *prop_name  =  ++p;
                    int result = nw_change_prop_security(object_type,
                                 object_name, object_namlen,
                                 prop_name, prop_namlen, prop_security);
                    if (result) completition = (uint8) -result;
                  } break;

     case 0x3c :  {  /* Scan Property */
                    struct XDATA {
                      uint8 prop_name[16];
                      uint8 flags;       /* set=2, dynamic=1 */
                      uint8 security;
                      uint8 akt_scan[4];
                      uint8 has_value;   /* ff, if there are Prop's Values */
                      uint8 weisnicht;
                    } *xdata = (struct XDATA*) responsedata;
                    uint8  *p = rdata;
                    int object_type    =  GET_BE16(p);
                    int object_namlen  =  (int) *(p+2);
                    uint8 *object_name =  (p+=3);
                    uint32 last_scan   =  GET_BE32((p+object_namlen));
                    uint8  prop_namlen =  (int)*   (p+=object_namlen+4);
                    uint8 *prop_name   =  ++p;
                    NETPROP prop;
                    int result = nw_scan_property(&prop,
                                 object_type, object_name, object_namlen,
                                 prop_name, prop_namlen, &last_scan);
                    if (result > -1) {
                      strncpy(xdata->prop_name,
                                     prop.name, sizeof(xdata->prop_name));
                      U32_TO_BE32(last_scan, xdata->akt_scan);
                      xdata->flags      = prop.flags;
                      xdata->security   = prop.security;
                      xdata->has_value  = (uint8) result;
                      xdata->weisnicht  = 0x0;
                      data_len          = sizeof(struct XDATA);
                    } else completition = (uint8) -result;
                  } break;

     case 0x3d :  {  /* read Bindery Property Value */
                    struct XDATA {
                      uint8 property_value[128];
                      uint8 more_segments;
                      uint8 property_flags;
                    } *xdata = (struct XDATA*) responsedata;
                    uint8  *p         =  rdata;
                    int object_type   =  GET_BE16(p);
                    int object_namlen =  (int) *(p+2);
                    uint8 *object_name=  p+3;
                    int segment_nr    =  (int) *(p+3+object_namlen);
                    int prop_namlen   =  (int) *(p+4+object_namlen);
                    uint8 *prop_name  =  p+5+object_namlen;
                    int result = nw_get_prop_val( object_type,
                                 object_name, object_namlen,
                                 segment_nr,
                                 prop_name, prop_namlen,
                                 xdata->property_value,
                                 &(xdata->more_segments),
                                 &(xdata->property_flags));
                    if (!result){
                      data_len = sizeof(struct XDATA);
                    } else completition = (uint8) -result;
                  } break;

     case 0x3e :  {  /* write Bindery Property Value */
                    uint8  *p         =  rdata;
                    int object_type   =  GET_BE16(p);
                    int object_namlen =  (int) *(p+2);
                    uint8 *object_name=  p+3;
                    int segment_nr    = (int) *(p+3+object_namlen);
                    int erase_segment = (int) *(p+4+object_namlen);
                    int prop_namlen   = (int) *(p+5+object_namlen);
                    uint8 *prop_name  =  p+6+object_namlen;
                    uint8 *valdata    =  p+6+object_namlen+prop_namlen;
                    int result = nw_write_prop_value( object_type,
                                 object_name, object_namlen,
                                 segment_nr, erase_segment,
                                 prop_name, prop_namlen,
                                 valdata);
                    if (result) completition = (uint8) -result;
                  } break;

     case 0x40:   {  /* change object password  */
                    if (password_scheme & PW_SCHEME_CHANGE_PW) {
                      uint8    *p    =  rdata;
                      uint8    oldpassword[50];
                      uint8    newpassword[50];
                      NETOBJ   obj;
                      int      result;
                      obj.type        =  GET_BE16(p);
                      p+=2;
                      xstrmaxcpy(obj.name,     p+1, (int) *p);
                      upstr(obj.name);
                      
                      p +=   ((*p)+1);
                      xstrmaxcpy(oldpassword,  p+1, (int) *p);
                      if (*p) memset(p+1, 0, *p);
                      
                      p +=   ((*p)+1);
                      xstrmaxcpy(newpassword,  p+1, (int) *p);
                      if (*p) memset(p+1, 0, *p);
                      
                      if (0 == (result = find_obj_id(&obj))) {
                        XDPRINTF((6, 0, "CHPW: OLD=`%s`, NEW=`%s`", oldpassword,
                                         newpassword));

                        internal_act = 1;
                        if (act_c->object_id == 1 ||
                           (0 == (result=test_allow_password_change(act_c->object_id))
                           &&
                           0 == (result=nw_test_unenpasswd(obj.id, oldpassword)))){
                          if ( (act_c->object_id != 1)
                            || *newpassword
                            || !(password_scheme & PW_SCHEME_LOGIN))
                             result=nw_set_passwd(obj.id, newpassword, 0);
                          else result = -0xff;
                        }
                        internal_act = 0;
                      }
                      if (result < 0) completition = (uint8) -result;
                      memset(oldpassword, 0, 50);
                      memset(newpassword, 0, 50);
                    } else {
                      XDPRINTF((1, 0, "Change object password unencryted not enabled"));
                      completition=0xff;
                    }
                  } break;

     case 0x41 :  {  /* add Bindery Object to Set */
                    uint8  *p         =  rdata;
                    int object_type   =  GET_BE16(p);
                    int object_namlen =  (int) *(p+=2);
                    uint8 *object_name=  ++p;
                    int prop_namlen   =  (int) *(p+=object_namlen);
                    uint8 *prop_name  =  ++p;
                    int member_type   =  GET_BE16(p+prop_namlen);
                    int member_namlen =  (int) *(p+=(prop_namlen+2));
                    uint8 *member_name=  ++p;
                    int result = nw_add_obj_to_set( object_type,
                                 object_name, object_namlen,
                                 prop_name, prop_namlen,
                                 member_type,
                                 member_name, member_namlen);
                    if (result) completition = (uint8) -result;
                  } break;

     case 0x42 :  {  /* delete Bindery Object from Set */
                    uint8  *p         =  rdata;
                    int object_type   =  GET_BE16(p);
                    int object_namlen =  (int) *(p+=2);
                    uint8 *object_name=  ++p;
                    int prop_namlen   =  (int) *(p+=object_namlen);
                    uint8 *prop_name  =  ++p;
                    int member_type   =  GET_BE16(p+prop_namlen);
                    int member_namlen =  (int) *(p+=(prop_namlen+2));
                    uint8 *member_name=  ++p;
                    int result = nw_delete_obj_from_set( object_type,
                                 object_name, object_namlen,
                                 prop_name, prop_namlen,
                                 member_type,
                                 member_name, member_namlen);
                    if (result) completition = (uint8) -result;
                  } break;

     case 0x43 :  {  /* is Bindery Object in Set */
                    uint8  *p         =  rdata;
                    int object_type   =  GET_BE16(p);
                    int object_namlen =  (int) *(p+=2);
                    uint8 *object_name=  ++p;
                    int prop_namlen   =  (int) *(p+=object_namlen);
                    uint8 *prop_name  =  ++p;
                    int member_type   =  GET_BE16(p+prop_namlen);
                    int member_namlen =  (int) *(p+=(prop_namlen+2));
                    uint8 *member_name=  ++p;
                    int result = nw_is_obj_in_set( object_type,
                                 object_name, object_namlen,
                                 prop_name, prop_namlen,
                                 member_type,
                                 member_name, member_namlen);
                    if (result) completition = (uint8) -result;
                  } break;


     case 0x44 :  { /* CLOSE BINDERY */
                    ;
                  } break;

     case 0x45 :  { /* OPEN BINDERY */
                    ;
                  } break;


     case 0x46 :  {   /* GET BINDERY ACCES LEVEL */
#if 0
                    struct XDATA {
                      uint8 acces_level;
                      uint8 object_id[4];
                    }
#endif
                    uint8    *xdata = responsedata;
                    if (!act_c->object_id) {
                      *xdata  = (uint8) 0;
                      memset(xdata+1, 0xff, 4);
                    } else {
                      *xdata  = (act_c->object_id == 1) ? (uint8) 0x33
                                                        : (uint8) 0x22;
                      U32_TO_BE32(act_c->object_id, (xdata+1));
                    }
                    data_len = 5;
                    XDPRINTF((2,0, "ACCESS LEVEL:=0x%x, obj=0x%lx",
                                  (int) *xdata, act_c->object_id));
                  }
                  break;

     case 0x47 :  { /* SCAN BINDERY OBJECT TRUSTEE PATH */
                    /* TODO !!! */
                    struct XDATA {
                      uint8 nextsequence[2];
                      uint8 id[4];
                      uint8 access_mask;
                      uint8 pathlen;
                      uint8 path[1];
                    } *xdata = (struct XDATA*) responsedata;
                    memset(xdata, 0, 8);
                    data_len = 8;
                    }
                  break;

     case 0x48 :  { /* GET BINDERY ACCES LEVEL from OBJECT ??? */
                    struct XDATA {
                      uint8 acces_level;
                    } *xdata = (struct XDATA*) responsedata;
                    NETOBJ    obj;
                    int       result;
                    obj.id =  GET_BE32(rdata);
                    result =  nw_get_obj(&obj);
                    if (!result) {
                      /* don't know whether this is ok ?? */
                      if (act_c->object_id == 1) {
                        xdata->acces_level = 0x33;
                      } else if (act_c->object_id == obj.id) {
                        xdata->acces_level = 0x22;
                      } else {
                        xdata->acces_level = 0x11;
                      }
                      data_len = sizeof(struct XDATA);
                    } else completition = (uint8)-result;
                  }
                  break;

     case 0x49 :  { /* IS CALLING STATION A MANAGER */
                    completition = (act_c->object_id == 1) ? 0 : 0xff;
                     /* here only SU = Manager  */
                    /* not manager, then completition = 0xff */
                  }
                  break;

     case 0x4a :  { /* keyed verify password  */
                    uint8    *p  =  rdata+sizeof(act_c->crypt_key);
                    NETOBJ   obj;
                    int      result;
                    obj.type     =  GET_BE16(p);
                    strmaxcpy((char*)obj.name, (char*)(p+3), *(p+2));
                    upstr(obj.name);
                    if (0 == (result = find_obj_id(&obj)))  {
                      internal_act = 1;
                      result=nw_test_passwd(obj.id, act_c->crypt_key, rdata);
                      internal_act = 0;
                    }
                    if (result < 0) completition = (uint8) -result;
                    XDPRINTF((2,0, "Keyed Verify PW from OBJECT='%s', result=%d",
                       obj.name, result));
                  }
                  break;

     case 0x4b :  { /* keyed change pasword  */
                    uint8  *p     =  rdata+sizeof(act_c->crypt_key);
                    NETOBJ obj;
                    int    result;
                    obj.type =  GET_BE16(p);
                    p+=2;
                    strmaxcpy((char*)obj.name, (char*)(p+1), *p);
                    upstr(obj.name);

                    /* from Guntram Blohm  */
                    p += (*p+1); /* here is crypted password length */
                    if (0 == (result = find_obj_id(&obj)))  {
		      internal_act=1;
                      if (obj.id != 1)
                        result=test_allow_password_change(obj.id);
                      if (!result)
  		        result=nw_keychange_passwd(obj.id, act_c->crypt_key,
				rdata, (int)*p, p+1, act_c->object_id);
                      if (!result) test_ins_unx_user(obj.id);
		      internal_act = 0;
		    }

                    if (result< 0) completition = (uint8) -result;
                    XDPRINTF((2, 0, "Keyed Change PW from OBJECT='%s', result=0x%x",
                      obj.name, result));
                  }
                  break;

     case 0x4c :  { /* List Relations of an Object  */
                   XDPRINTF((1, 0, "TODO:List Relations of an Object"));
                   completition=0xfb;
                  } break;

     case 0x64 :  {  /* Create Queue, prehandled by nwconn  */
                   int   q_typ      = GET_BE16(rdata);
                   int   q_name_len = *(rdata+2);
                   uint8 *q_name    = rdata+3;
                   /* inserted by nwconn !!! */
#if 0
                   int    dummy     = *(rdata+3+q_name_len);
#endif
                   int    pathlen   = *(rdata+3+q_name_len+1);
                   uint8  *path     = rdata+3+q_name_len+2;
                   uint32 q_id;
                   int  result  =  nw_creat_queue(q_typ, 
                       q_name, q_name_len,
                       path, pathlen, &q_id);
                   if (result > -1) {
                     U32_TO_BE32(q_id, responsedata);
                     data_len=4;
                   } else
                     completition=(uint8)(-result);
                  } break;

     case 0x65 :  {   /* Destroy Queue */
                   uint32 q_id =  GET_BE32(rdata);
                   int result=-0xd3;  /* no rights */
                   if (1 == act_c->object_id)
                     result=nw_destroy_queue(q_id);
                   if (result < 0)
                      completition=(uint8)(-result);
                  } break;

     case 0x66 :  { /* Read Queue Current Status,old */
                   struct XDATA {
                      uint8 id[4];
                      uint8 status;
                      uint8 entries;
                      uint8 servers;
                      uint8 data[1];
                   } *xdata = (struct XDATA*) responsedata;
                   uint32 q_id =  GET_BE32(rdata);
                   int status;
                   int entries;
                   int servers;
                   int server_ids[25];
                   int server_conns[25];
                   int result=nw_get_queue_status(q_id, &status, &entries, 
                                  &servers, server_ids, server_conns);
                   if (result>-1) {
                     int k;
                     uint8 *p=&(xdata->data[0]);
                     U32_TO_BE32(q_id, xdata->id);
                     xdata->status=status;
                     xdata->entries=entries;
                     xdata->servers=servers;
                     k=-1;
                     while (++k < servers) {
                       U32_TO_BE32(server_ids[k], p);
                       p+=4;
                     }
                     k=-1;
                     while (++k < servers) {
                       *p=(uint8) server_conns[k];
                       ++p;
                     }
                     data_len=sizeof(struct XDATA)-1+5*servers;
                   } else
                     completition=(uint8)-result;
                  }
                  break;

     case 0x6A :    /* Remove Job from Queue OLD */
     case 0x80 :  { /* Remove Job from Queue NEW */
                   uint32 q_id   = GET_BE32(rdata);
                   uint32 job_id = (ufunc == 0x6A)
                                      ? GET_BE16(rdata+4)
                                      : GET_BE16(rdata+4);
                   int result=nw_remove_job_from_queue(
                                           act_c->object_id,
                                           q_id, job_id);
                   if (result < 0)
                     completition=(uint8)-result;
                  }
                  break;

     case 0x6B :  {  /* Get Queue Job List, old */
                   uint32 q_id=GET_BE32(rdata);
                   int result=nw_get_queue_job_list_old(q_id, responsedata);
                   if (result > -1) 
                     data_len=result;
                   else
                     completition=(uint8)-result;
                  }
                  break;


     case 0x68:     /* creat queue job and file old */
     case 0x79:   { /* creat queue job and file new */
                    uint32 q_id    = GET_BE32(rdata);
                    uint8 *q_job   = rdata+4; /* jobsize = 256(old) or 280 */ 
                    int result     =  nw_creat_queue_job(
                                      act_connection, ncprequest->task,  
                                      act_c->object_id,
                                      q_id, q_job, 
                                      responsedata,
                                      ufunc==0x68);
                    if (result > -1) 
                      data_len=result;
                    else
                      completition = (uint8) -result; 
                      /*0xd3  err no queue rights */
                  }
                  break;

     case 0x6C :  {   /* Get Queue Job Entry old */
                    uint32 q_id = GET_BE32(rdata);
                    int job_id  = GET_BE16(rdata+4);
                     /* added by nwconn */
                    uint32 fhandle = GET_BE32(rdata+8);
                    int result=nw_get_q_job_entry(q_id, job_id, fhandle,
                                   responsedata, 1);
                    if (result > -1) 
                      data_len=result;
                    else completition=(uint8)-result;
                  }
                  break;

     case 0x69:      /* close file and start queue old ?? */
     case 0x7f:   {  /* close file and start queue */
                    uint32 q_id      = GET_BE32(rdata);
     		    uint32 job_id    = (ufunc==0x69) 
     		                         ? GET_BE16(rdata+4)
     		                         : GET_BE16(rdata+4);
                    int result       = nw_close_queue_job(q_id, job_id, 
                                                         responsedata);
                    if (result > -1) 
                      data_len=result;
                    else 
                      completition  = (uint8) -result; 
                  }
                  break;

     case 0x6f :  {  /* attach server to queue */
                     /* from pserver */
                    uint32 q_id      = GET_BE32(rdata);
                    int result=nw_attach_server_to_queue(
                             act_c->object_id,
                             act_connection,
                             q_id);
                    if (result < 0) 
                      completition  = (uint8) -result; 
                    /* NO REPLY */
                  }
                  break;

     case 0x70 :  {  /* detach server from queue */
                     /* from pserver */
                    uint32 q_id      = GET_BE32(rdata);
                    int result=nw_detach_server_from_queue(
                                act_c->object_id,
                                act_connection,
                                q_id);
                    if (result < 0) 
                      completition  = (uint8) -result; 
                    /* NO REPLY */
                  }
                  break;

     case 0x78:   /* Get Queue Job File Size (old) */
     case 0x87:   /* Get Queue Job File Size       */
                  {
                    uint32 q_id   = GET_BE32(rdata);
     		    uint32 job_id = (ufunc==0x78) 
     		                    ? GET_BE16(rdata+4)
     		                    : GET_BE16(rdata+4);
                    int result = nw_get_queue_job_file_size(q_id, job_id);
                    if (result > -1) {
                      uint8 *p=responsedata;
                      U32_TO_BE32(q_id, p); p+=4;
                      if (ufunc==0x78) {
                        U16_TO_BE16(job_id, p); p+=2;
                      } else {
                       /* U32_TO_BE32(job_id, p); p+=4; */
                        U16_TO_BE16(job_id, p); p+=2;
                        *(p++)=0;
                        *(p++)=0;
                      }
                      U32_TO_BE32(result, p); p+=4;
                      data_len=(int)(p-responsedata);
                    } else 
                      completition  = (uint8) -result; 
                  }
                  break;

     case 0x71 :     /* service queue job old */
     case 0x7c :  {  /* service queue job */
                    uint32 q_id      = GET_BE32(rdata);
                    int    type      = GET_BE16(rdata+4);
                    int result=nw_service_queue_job(
                         act_c->object_id,
                         act_connection, ncprequest->task,  
                         q_id, type, responsedata, 
                              ufunc==0x71 );
                    if (result > -1)
                      data_len=result;
                    else
                     completition=(uint8)-result;
                  }
                  break;


     case 0x7d :  { /* Read Queue Current Status, new */
                   struct XDATA {
                     uint8  id[4];     /* queue id */
                     uint8  status[4]; /* &1 no station allowed */
                                       /* &2 no other queue server allowed */
                                       /* &4 no queue server allowed get entries */
                     uint8 entries[4]; /* current entries */
                     uint8 servers[4]; /* current servers */ 
                   } *xdata = (struct XDATA*) responsedata;
                   uint32 q_id =  GET_BE32(rdata);
                   int status;
                   int entries;
                   int servers;
                   int server_ids[25];
                   int server_conns[25];
                   int result=nw_get_queue_status(q_id, &status, &entries, 
                                  &servers, server_ids, server_conns);
                   if (result>-1) {
                     int k;
                     uint8 *p=responsedata+sizeof(*xdata);
                     U32_TO_BE32(q_id, xdata->id);
                     U32_TO_32(status, xdata->status);
                     U32_TO_32(entries, xdata->entries);
                     U32_TO_32(servers, xdata->servers);
                     k=-1;
                     while (++k < servers) {
                       U32_TO_BE32(server_ids[k], p);
                       p+=4;
                     }
                     k=-1;
                     while (++k < servers) {
                       U32_TO_32(server_conns[k], p);
                       p+=4;
                     }
                     data_len=sizeof(struct XDATA)+8*servers;
                   } else
                     completition=(uint8)-result;
                  } break;

     case 0x81 :  { /* Get Queue Job List */
                   NETOBJ    obj;
                   struct XDATA {
                      uint8 total_jobs[4];
                      uint8 reply_numbers[4];
                      uint8 job_list[4]; /* this is repeated */
                   } *xdata = (struct XDATA*) responsedata;
                   obj.id =  GET_BE32(rdata);
                   XDPRINTF((2, 0, "TODO:GET QUEUE JOB List of Q=0x%lx", obj.id));
                   memset(xdata, 0, sizeof(struct XDATA));
                   data_len=sizeof(struct XDATA);
                  }break;

     case 0x72:      /* finish servicing queue job  (old)*/
     case 0x83:   {  /* finish servicing queue job */
                    uint32 q_id       = GET_BE32(rdata);
     		    uint32 job_id     = GET_BE16(rdata+4);
#if 0
                    uint32 chargeinfo = GET_BE32(rdata+8);
#endif
                    int result        = nw_finish_abort_queue_job(0,
                                           act_c->object_id,
                                           act_connection, 
                                           q_id, job_id);
                    if (result <0)
                      completition=(uint8) -result;
                  }break;

     case 0x73:      /* abort servicing queue job (old) */
     case 0x84:   {  /* abort servicing queue job */
                    uint32 q_id       = GET_BE32(rdata);
     		    uint32 job_id     = GET_BE16(rdata+4);
                    int result        = nw_finish_abort_queue_job(1,
                                           act_c->object_id,
                                           act_connection, 
                                           q_id, job_id);
                    if (result <0)
                      completition=(uint8) -result;
                    else {
                      memset(responsedata, 0, 2);
                      data_len=2;
                    }
                  }break;



     case 0xc8 :  { /* CHECK CONSOLE PRIVILEGES */
                   XDPRINTF((1, 0, "TODO: CHECK CONSOLE PRIV"));
                   /*  !!!!!! TODO completition=0xc6 (no rights) */
                    if (act_c->object_id != 1) completition=0xc6; /* no rights */
                  } break;

     case 0xc9 :  { /* GET FILE SERVER DESCRIPTION STRINGs */
                   char *company       = "Mars :-)";
                   char *revision      = "Version %d.%d.pl%d";
                   char *revision_date = REVISION_DATE;
                   char *copyright     = "(C)opyright Martin Stover";
                   int  k=strlen(company)+1;
                   int  l;
                   memset(responsedata, 0, 512);
                   strcpy(responsedata,   company);

                   l = 1 + sprintf(responsedata+k, revision,
                                     _VERS_H_, _VERS_L_, _VERS_P_ );
#if 0
                   k+=l;
#else
                   /* BUG in LIB */
                   k += (1 + strlen(responsedata+k));
#endif
                   strcpy(responsedata+k, revision_date);
                   k += (strlen(revision_date)+1);
                   strcpy(responsedata+k, copyright);
                   k += (strlen(copyright)+1);
                   data_len = k;
                  } break;

     case 0xcd :  { /* GET FILE SERVER LOGIN STATUS  */
                    struct XDATA {
                      uint8 login_allowed; /* 0 NO , 1 YES */
                    } *xdata = (struct XDATA*) responsedata;
                    xdata->login_allowed = 1;
                    data_len             = 1;
                  }
                  break;

     case 0xd1 :  /* Send Console Broadcast (old) */
                  {
                    uint8      *p = rdata;
                    int count_conns = (int) *p++;
                    uint8    *co  = p;
                    int msglen    = (int) *(p+count_conns);
                    char  *msg    = (char*) p+count_conns+1;
                    int k = -1;
                    if (count_conns) {
                      while (++k < count_conns) {
                        int conn= (int) *co++;
                        if (conn == act_connection) {
                          strmaxcpy(act_c->message, msg, min(58, msglen));
                          connect_status = 0x40;  /* don't know why */
                        } else if (conn && --conn < max_connections) {
                          CONNECTION *cc= &(connections[conn]);
                          if (cc->object_id) {  /* if logged */
                            strmaxcpy(cc->message, msg, min(58, msglen));
                              nwserv_handle_msg(conn+1);
                          }
                        }
                      }
                    } else {
                      strmaxcpy(act_c->message, msg, min(58, msglen));
                      connect_status = 0x40;  /* don't know why */
                    }
                  }
                  break;

     case 0xd3 :  { /* down File Server */
                    if (act_c->object_id == 1) { /* only SUPERVISOR */
                       /* inform nwserv */
                       nwserv_down_server();
                    } else completition = 0xff;
                  }
                  break;

#if 0
     case 0xfd :  /* Send Console Broadcast (new) */
                  return(-1); /* nicht erkannt */
                  break;
#endif
       default : completition = 0xfb; /* not known here */
                  break;
    }  /* switch */
  } else if (func == 0x19) {  /* logout */
    write_utmp(0, act_connection, act_c->pid_nwconn, &(act_c->client_adr), NULL);
    act_c->object_id  = 0; /* not LOGIN  */
  } else if (0x20 == func) { /* Semaphore */
    int result = handle_func_0x20(act_c, rdata, ufunc, responsedata);
    if (result > -1) data_len = result;
    else completition=(uint8)-result;
  } else completition = 0xfb;

  U16_TO_BE16(0x3333,           ncpresponse->type);
  ncpresponse->sequence       = ncprequest->sequence;
  ncpresponse->task           = ncprequest->task;
  ncpresponse->connection     = ncprequest->connection;
  ncpresponse->high_connection= ncprequest->high_connection;
  ncpresponse->completition   = completition;

  if (act_c->message[0]) connect_status |= 0x40;
  ncpresponse->connect_status = connect_status;
  data_len+=sizeof(NCPRESPONSE);
  U16_TO_BE16(act_c->send_to_sock, my_addr.sock);

  send_ipx_data(ipx_out_fd, 17, data_len, (char*)ncpresponse,
                 &my_addr, NULL);

  XDPRINTF((2, 0, "func=0x%x ufunc=0x%x compl:0x%x, written count = %d",
             (int)func, (int)ufunc, (int) completition, data_len));
}

static void handle_bind_calls(uint8 *p)
{
  int func = (int) *p;
  p       += 2;
  switch (func) {
     case  0x01 :
       {  /* insert NET_ADDRESS */
         NETOBJ obj;
         obj.type = GET_BE16(p);
         p += 2;
         strmaxcpy(obj.name, p+1, *p);
         p += (*p+1);
         nw_new_obj_prop(0, obj.name, obj.type, O_FL_DYNA, 0x40,
                           "NET_ADDRESS", P_FL_DYNA|P_FL_ITEM,  0x40,
                           (char *)p, sizeof(ipxAddr_t), 1);
       }
       break;

     case  0x02 :
       {  /* delete complete Object */
         NETOBJ obj;
         obj.type = GET_BE16(p);
         p += 2;
         strmaxcpy(obj.name, p+1, *p);
         nw_delete_obj(&obj); /* also deletes all properties */
       }
       break;

    default : break;
  }
}

static void reinit_nwbind(void)
{
  int org_internal_act=internal_act;
  get_ini_debug(NWBIND);
  internal_act=1;
  (void)nw_fill_standard(NULL, NULL);
  internal_act=org_internal_act;
  sync_dbm();
}

static int xread(IPX_DATA *ipxd, int *offs, uint8 *data, int size)
{
  if (*offs == 0) memcpy(ipxd, &ipx_in_data, ud.udata.len);
  if (size > -1 && *offs + size > ipxd->owndata.d.size) {
    errorp(1, "xread:", "prog error: *offs=%d + size=%d > d.size=%d",
                  *offs, size, ipxd->owndata.d.size);
    size = -1;
  }
  if (size > -1) {
    memcpy(data,  ((uint8 *)&(ipxd->owndata.d.function)) + *offs, size);
    *offs+=size;
  } else {
    errorp(1, "xread:", "readerror");
  }
  return(size);
}

static void handle_ctrl()
/* reads packets from nwserv/ncpserv */
{
  IPX_DATA ipxd;
  int   what;
  int   conn;
  int   offs=0;
  int   data_len =  xread(&ipxd, &offs, (uint8*)&(what), sizeof(int));

  if   (data_len == sizeof(int)) {
    XDPRINTF((2, 0, "GOT CTRL what=0x%x, len=%d",
                     what, ipxd.owndata.d.size));

    (void)send_own_reply(ipx_out_fd, 0, ipxd.owndata.h.sequence, &from_addr);

    switch (what) {
      case  0x2222 :  /* insert connection */
        if (sizeof (int) ==
          xread(&ipxd, &offs, (uint8*)&(conn), sizeof(int))) {
          int size  = 0;
          if (sizeof (int) ==
             xread(&ipxd, &offs, (uint8*)&(size), sizeof(int))
             && size == sizeof(ipxAddr_t)
                      + sizeof(uint16) + sizeof(uint32) ) {
           uint8 buf[100];
           if (size == xread(&ipxd, &offs, buf, size))
             open_clear_connection(conn, 1, buf);
          }
        }
        break;

      case 0x3333 : /* special 'bindery' calls */
        if (sizeof (int) ==
            xread(&ipxd, &offs, (uint8*)&(conn), sizeof(int))) {
          uint8 *buff = (uint8*)  xmalloc(conn+10);
          XDPRINTF((2,0, "0x3333 len=%d", conn));
          if (conn == xread(&ipxd, &offs, buff, conn))
             handle_bind_calls(buff);
          else
            XDPRINTF((1, 1, "0x3333 protokoll error:len=%d", conn));
          xfree(buff);
        }
        break;

      case 0x5555 : /* clear connection */
        if (sizeof (int) ==
            xread(&ipxd, &offs, (uint8*)&(conn), sizeof(int)))
          open_clear_connection(conn, 0, NULL);
        break;

      case 0xeeee:
        reinit_nwbind();
        break;

      case 0xffff : /* server down */
        data_len = xread(&ipxd, &offs, (char*)&conn, sizeof(int));
        if (sizeof(int) == data_len && conn == what)
           sent_down_message();
        break;
      default : break;
    } /* switch */

  } else {
    errorp(1, "handle_ctrl", "wrong data len=%d", data_len);
  }
}

static int got_sig=0;
static void sig_handler(int isig)
{
  got_sig=isig;
  signal(isig, sig_handler);
}

static void set_sig(void)
{
  signal(SIGQUIT,  sig_handler);
  signal(SIGHUP,   sig_handler);
  signal(SIGTERM,  SIG_IGN);
  signal(SIGINT,   SIG_IGN);
  signal(SIGPIPE,  SIG_IGN);
}

int main(int argc, char *argv[])
{
  int sock_nwbind;
  int i;
  if (argc != 4) {
    fprintf(stderr, "usage nwbind nwname address nwbindsock\n");
    exit(1);
  }

  init_tools(NWBIND, 0);

  strmaxcpy(my_nwname, argv[1], 47);
  adr_to_ipx_addr(&my_addr, argv[2]);

  sscanf(argv[3], "%x", &sock_nwbind);

  internal_act = 1;
  if (nw_init_dbm(my_nwname, &my_addr) <0) {
    errorp(1, "nw_init_dbm", NULL);
    exit(1);
  }
  internal_act = 0;
  max_connections=get_ini_int(60); /* max_connections */
  if (max_connections < 1)
    max_connections=MAX_CONNECTIONS;
  if (max_connections < 1)
    max_connections=1;
  connections=(CONNECTION*)xcmalloc(max_connections*sizeof(CONNECTION));
  max_nw_vols=get_ini_int(61); /* max. volumes */
  if (max_nw_vols < 1)
    max_nw_vols = MAX_NW_VOLS;

#ifdef LINUX
  set_emu_tli();
#endif

#if USE_PERMANENT_OUT_SOCKET
  ipx_out_fd=open_ipx_socket(NULL, 0);
#endif
  XDPRINTF((1, 0, "USE_PERMANENT_OUT_SOCKET %s",
                (ipx_out_fd > -1) ? "enabled" : "disabled"));

  ud.opt.len       = sizeof(ipx_pack_typ);
  ud.opt.maxlen    = sizeof(ipx_pack_typ);
  ud.opt.buf       = (char*)&ipx_pack_typ; /* gets actual Typ */

  ud.addr.len      = sizeof(ipxAddr_t);
  ud.addr.maxlen   = sizeof(ipxAddr_t);
  ud.addr.buf      = (char*)&from_addr;

  ud.udata.len     = IPX_MAX_DATA;
  ud.udata.maxlen  = IPX_MAX_DATA;
  ud.udata.buf     = (char*)&ipx_in_data;
  set_sig();

  while (got_sig != SIGQUIT) {
    act_c        = (CONNECTION*)NULL;
    if (t_rcvudata(ncp_fd, &ud, &rcv_flags) > -1){
      time(&akttime);
      XDPRINTF((10, 0, "NWBIND-LOOP from %s", visable_ipx_adr(&from_addr)));
      if ( ncprequest->type[0] == 0x22
        && ncprequest->type[1] == 0x22) {
        act_connection  = (int)ncprequest->connection
                         | (((int)ncprequest->high_connection) << 8);

        if (act_connection > 0  && act_connection <= max_connections) {
          act_c = &(connections[act_connection-1]);
          internal_act = 0;
          if (act_c->active && IPXCMPNODE(from_addr.node, my_addr.node)
                        && IPXCMPNET (from_addr.net,  my_addr.net)) {
            if (!ncprequest->function){ /* wdog reset */
              nwserv_reset_wdog(act_connection);
              XDPRINTF((3, 0, "send wdog reset"));
            } else   
              handle_fxx(ud.udata.len, (int)ncprequest->function);
          } else {
            XDPRINTF((1, 0, "NWBIND-LOOP addr=%s of connection=%d is wrong",
             visable_ipx_adr(&from_addr), act_connection));
          }
        } else {
          XDPRINTF((1, 0, "NWBIND-LOOP connection=%d is wrong",
            act_connection));
        }
      } else if ( ncprequest->type[0] == 0xee
               && ncprequest->type[1] == 0xee
               && IPXCMPNODE(from_addr.node, my_addr.node)
               && IPXCMPNET (from_addr.net,  my_addr.net)) {
        /* comes from nwserv, i hope :)  */
        handle_ctrl();
      } else {
        XDPRINTF((1, 0, "NWBIND-LOOP got wrong type 0x%x func=0x%x from %s",
           (int) GET_BE16(ncprequest->type),
           (int) ncprequest->function, visable_ipx_adr(&from_addr) ));
      }
    }
    if (got_sig == SIGHUP) {
     /* here I update some Bindery stuff from nwserv.conf */
      reinit_nwbind();
      got_sig = 0;
    }
  }
  
  /* perhaps some connections need clearing (utmp), hint from Ambrose Li */
  i=max_connections+1;
  while (--i)
    open_clear_connection(i, 0, NULL);

  if (ncp_fd > -1) {
    t_unbind(ncp_fd);
    t_close(ncp_fd);
    if (ipx_out_fd > -1) {
      t_unbind(ipx_out_fd);
      t_close(ipx_out_fd);
    }
  }
  internal_act=1;
  nw_exit_dbm();
  xfree(connections);
  XDPRINTF((2,0, "LEAVE nwbind"));
  return(0);
}
