/* nwconn.c 14-Aug-97       */
/* one process / connection */

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
#if 1
# define LOC_RW_BUFFERSIZE RW_BUFFERSIZE
#else
# define LOC_RW_BUFFERSIZE 512
#endif
#include <dirent.h>
#include "nwvolume.h"
#include "nwfile.h"
#include "connect.h"
#include "nwqconn.h"
#include "namspace.h"
#include "nwconn.h"

int act_connection = 0;
int act_pid        = 0;

#define  FD_NCP_OUT    3

static int          father_pid    = -1;
static ipxAddr_t    from_addr;
static ipxAddr_t    my_addr;
static struct       t_unitdata ud;
static uint8        ipx_pack_typ  =  PACKT_CORE;
static int          last_sequence = -9999;

static IPX_DATA     ipxdata;
static NCPRESPONSE  *ncpresponse=(NCPRESPONSE*)&ipxdata;
static uint8        *responsedata=((uint8*)&ipxdata)+sizeof(NCPRESPONSE);

static int          requestlen;
static uint8        readbuff[IPX_MAX_DATA];

static uint8        saved_readbuff[IPX_MAX_DATA];
static int          saved_sequence=-1;
static int rw_buffer_size = 512;  /* default */

static NCPREQUEST   *ncprequest  = (NCPREQUEST*)readbuff;
static uint8        *requestdata = readbuff + sizeof(NCPREQUEST);
static int          ncp_type;
static int          sock_nwbind=-1;
static int          sock_echo  =-1;
static char         *prog_title;

static int req_printed=0;

typedef struct {
  BURSTPACKET *sendburst;   /* buffer for sending burstpacket
                             * allocated and prefilled by response to
                             * func 0x65
                             * max. max_packet_size
                             */

  uint8   *send_buf;        /* we look from servers side
                             * complete data buf of burst reply
                             * file read status + file read buf
                             * sendbuff must be 8 byte more allocated
                             * than max_send_size !
                             */

  int     max_send_size;    /* send_buf size, complete Burst DATA size */

  uint32  packet_sequence;  /* -> packet_sequence
                             * will be increased after every
                             * packet
                             */
  int     burst_sequence;

  struct t_unitdata ud;
  ipxAddr_t to_addr;

  uint8   *recv_buf;        /* complete data buf for burst read requests
                             * must be 24 byte more allocated
                             * than max_recv_size !
                             */

  int     max_recv_size;    /* allocated size of recv_buf */

  int     max_burst_data_size;  /* size of BURSTDATA, max. IPX_DATA - BURSTHEADER */
  uint8   ipx_pack_typ;
} BURST_W;

static BURST_W *burst_w=NULL;

static void set_program_title(char *s)
{
  memset(prog_title, 0, 49);
  if (s&&*s)
    strmaxcpy(prog_title, s, 48);
  else
    strcpy(prog_title, "()");
}

static int ncp_response(int sequence, int task,
                int completition, int data_len)
{
  ncpresponse->sequence       = (uint8) sequence;
  ncpresponse->task           = (uint8) task;
  ncpresponse->completition   = (uint8) completition;
  last_sequence               = sequence;

  if (req_printed) {
    XDPRINTF((0,0, "NWCONN NCP_RESP seq:%d, conn:%d,  compl=0x%x task=%d TO %s",
        (int)ncpresponse->sequence,
        (int)ncpresponse->connection
          | (((int)ncpresponse->high_connection) << 8),
        (int)completition,
        (int)ncpresponse->task, visable_ipx_adr((ipxAddr_t *) ud.addr.buf)));
  }
  ud.udata.len = ud.udata.maxlen = sizeof(NCPRESPONSE) + data_len;
  if (t_sndudata(FD_NCP_OUT, &ud) < 0){
    if (nw_debug) t_error("t_sndudata in NWCONN !OK");
    return(-1);
  }
  return(0);
}

static int call_nwbind(void)
{
  ipxAddr_t to_addr;
  memcpy(&to_addr, &my_addr, sizeof(ipxAddr_t));
  U16_TO_BE16(sock_nwbind, to_addr.sock);
  ud.udata.len = ud.udata.maxlen = sizeof(NCPREQUEST) + requestlen;
  ud.udata.buf = (char*)&readbuff;
  ud.addr.buf  = (char*)&to_addr;
  if (t_sndudata(FD_NCP_OUT, &ud) < 0){
    if (nw_debug) t_error("t_sndudata in NWCONN !OK");
    ud.addr.buf   = (char*)&from_addr;
    ud.udata.buf  = (char*)&ipxdata;
    return(-1);
  }
  ud.addr.buf     = (char*)&from_addr;
  ud.udata.buf    = (char*)&ipxdata;
  return(0);
}

static void pr_debug_request()
{
  if (req_printed++) return;
  if (ncp_type == 0x2222) {
    int ufunc = 0;
    switch (ncprequest->function) {
      case 0x16 :
      case 0x17 : ufunc = (int) *(requestdata+2); break;
      case 0x57 : ufunc = (int) *(requestdata);   break;
      default   : break;
    } /* switch */
    XDPRINTF((1, 0,  "NCP REQUEST: func=0x%02x, ufunc=0x%02x, seq:%03d, task:%02d",
                      (int)ncprequest->function, ufunc,
                      (int)ncprequest->sequence,
                      (int)ncprequest->task));
  } else {
     XDPRINTF((1, 0, "Got NCP:type:0x%x, seq:%d, task:%d, func=0x%x",
                      ncp_type,
                      (int)ncprequest->sequence,
                      (int)ncprequest->task,
                      (int)ncprequest->function));
  }
  if (requestlen > 0){
    int    j = requestlen;
    uint8  *p=requestdata;
    XDPRINTF((0, 2, "len %d, DATA:", j));
    while (j--) {
      int c = *p++;
      if (c > 32 && c < 127)  XDPRINTF((0, 3,",\'%c\'", (char) c));
      else XDPRINTF((0,3, ",0x%x", c));
    }
    XDPRINTF((0,1,NULL));
  }
}
#if TEST_FNAME
static int test_handle = -1;
#endif
static int handle_ncp_serv(void)
{
  int    function       = (int)ncprequest->function;
  int    completition   = 0;  /* first set      */
  int    org_nw_debug   = nw_debug;
  int    do_druck       = 0;
  int    data_len       = 0;

  if (last_sequence == (int)ncprequest->sequence
       && ncp_type != 0x1111){ /* send the same again */
    if (t_sndudata(FD_NCP_OUT, &ud) < 0){
      if (nw_debug) t_error("t_sndudata !OK");
    }
    XDPRINTF((2,0, "Sequence %d is written twice", (int)ncprequest->sequence));
    return(0);
  }
  req_printed=0;

  if (nw_debug > 1){
    if (nw_debug < 10
       && (function==0x48 || function == 0x49))  /* read or write */
          nw_debug=1;
    if (nw_debug < 15
       && (function==0x48)) /* read */
          nw_debug=1;
  }

  if (nw_debug > 5) pr_debug_request();

  if (ncp_type == 0x2222) {
    switch (function) {

#if 0
         case 0x3 :  { /* Log File */
NWCONN	1:UNKNOWN FUNCTION od. TYPE: 0x3
NWCONN	1:NWCONN NCP_RESP seq:56, conn:1, compl=0xfb task=5 TO
	net=0:0:0:22, node=0:0:1:22:59:52, sock=40:03
NWCONN	1:NCP REQUEST: func=0x03, ufunc=0x00, seq:060, task:05
NWCONN	1:len 15, DATA:,0x5,0x1,0x0,0x12,0xa,'0','9','0','6',
	'9','7','.','Z','I','P'
                     ;;
                     } break;
         case 0x4 :  { /* Lock File Set  */
                     ;;
                     } break;
         case 0x5 :  { /* Release File  */
                     ;;
                     } break;
         case 0x6 :  { /* Release File Set */
                     ;;
                     } break;
         case 0x7 :  { /* Clear File */
                     ;;
                     } break;
         case 0x8 :  { /* Clear File Set  */
                     ;;
                     } break;
#endif

         case 0x12 : { /* Get Volume Info with Number */
                       int volume = (int)*requestdata;
                       struct XDATA {
                         uint8 sec_per_block[2];
                         uint8 total_blocks[2];
                         uint8 avail_blocks[2];
                         uint8 total_dirs[2];
                         uint8 avail_dirs[2];
                         uint8 name[16];
                         uint8 removable[2];
                       } *xdata = (struct XDATA*) responsedata;
                       int result;
                       memset(xdata, 0, sizeof(struct XDATA));
                       if ((result = nw_get_volume_name(volume, xdata->name))>-1){
                         struct fs_usage fsp;
                         if (!nw_get_fs_usage(xdata->name, &fsp)) {
                           int sector_scale=1;
                           while (fsp.fsu_blocks/sector_scale > 0xffff)
                                  sector_scale+=2;
                           U16_TO_BE16(sector_scale, xdata->sec_per_block);
                           U16_TO_BE16(fsp.fsu_blocks/sector_scale, xdata->total_blocks);
                           U16_TO_BE16(fsp.fsu_bavail/sector_scale, xdata->avail_blocks);
                           U16_TO_BE16(fsp.fsu_files,  xdata->total_dirs);
                           U16_TO_BE16(fsp.fsu_ffree,  xdata->avail_dirs);
                           if ( get_volume_options(volume) & VOL_OPTION_REMOUNT) {
                             U16_TO_BE16(1,  xdata->removable);
                           } else {
                             U16_TO_BE16(0,  xdata->removable);
                           }
                         }
                         data_len = sizeof(struct XDATA);
                       } else completition = (uint8) -result;
                     } break;

         case 0x14 : { /* GET DATE und TIME */
                       struct SERVER_DATE {
                         uint8   year;
                         uint8   mon;
                         uint8   day;
                         uint8   std;
                         uint8   min;
                         uint8   sec;
                         uint8   day_of_week;
                       } *mydate = (struct SERVER_DATE*) responsedata;
                       struct tm  *s_tm;
                       time_t      timer;
                       time(&timer);
                       s_tm = localtime(&timer);
                       mydate->year  = (uint8) s_tm->tm_year;
                       mydate->mon   = (uint8) s_tm->tm_mon+1;
                       mydate->day   = (uint8) s_tm->tm_mday;

                       mydate->std   = (uint8) s_tm->tm_hour;
                       mydate->min   = (uint8) s_tm->tm_min;
                       mydate->sec   = (uint8) s_tm->tm_sec;
                       mydate->day_of_week = (uint8) s_tm->tm_wday; /* Wochentag  */
                       data_len = sizeof(struct SERVER_DATE);
                     }
                     break;

         case 0x15 :
                     return(-1); /* nwbind must do this call */


         case 0x16 : {
                       /* uint8 len = *(requestdata+1); */
                       uint8 *p  =  requestdata +2;
                       if (0 == *p){
                   /******** SetDirektoryHandle *************/
                         struct INPUT {
                           uint8   header[7];         /* Requestheader    */
                           uint8   div[3];            /* 0x0, dlen, ufunc */
                           uint8   target_dir_handle; /* to change        */
                           uint8   source_dir_handle;
                           uint8   pathlen;
                           uint8   path[2];
                         } *input = (struct INPUT *) (ncprequest);
                         completition =
                           (uint8)-nw_set_dir_handle((int)input->target_dir_handle,
                                                     (int)input->source_dir_handle,
                                                          input->path,
                                                     (int)input->pathlen,
                                                     (int)(ncprequest->task));

                       } else  if (1 == *p){
                   /******** GetDirektoryPATH ***************/
                         struct INPUT {
                           uint8   header[7];      /* Requestheader */
                           uint8   div[3];         /* 0x0, dlen, ufunc */
                           uint8   dir_handle;
                         } *input = (struct INPUT *) (ncprequest);
                         struct XDATA {
                           uint8 len;
                           uint8 name[256];
                         } *xdata = (struct XDATA*) responsedata;
                         int result = nw_get_directory_path((int)input->dir_handle, xdata->name);
                         if (result > -1){
                            xdata->len = (uint8) result;
                            data_len   = result + 1;
                            xdata->name[result] = '\0';
                            XDPRINTF((5,0, "GetDirektoryPATH=%s", xdata->name));
                         } else completition = (uint8)-result;
                       } else  if (2 == *p){ /* Scan Direktory Information */
                   /******** Scan Dir Info   ****************/
                         struct INPUT {
                           uint8   header[7];       /* Requestheader */
                           uint8   div[3];            /* 0x0, dlen, ufunc */
                           uint8   dir_handle;      /* Verzeichnis Handle */
                           uint8   sub_dir_nmbr[2]; /* HI LOW */
                                                    /* firsttime 1 */
                           uint8   len;             /* kann auch 0 sein */
                           uint8   path[2];
                         } *input = (struct INPUT *) (ncprequest);
                         struct XDATA {
                           uint8 sub_dir_name[16];
                           uint8 create_date_time[4];
                           uint8 owner_id[4];       /* HI LOW */
                           uint8 max_right_mask;
                           uint8 reserved;          /* Reserved by Novell */
                           uint8 sub_dir_nmbr[2];   /* HI LOW */
                         } *xdata = (struct XDATA*) responsedata;
                         int result;
                         memcpy(xdata->sub_dir_nmbr, input->sub_dir_nmbr, 2);
                         result = nw_scan_dir_info((int)input->dir_handle,
                                  input->path, (int)input->len,
                                  xdata->sub_dir_nmbr, xdata->sub_dir_name,
                                  xdata->create_date_time, xdata->owner_id);
                         if (result > -1){
                            xdata->max_right_mask = (uint8)result;
                            data_len              = sizeof(struct XDATA);
                            XDPRINTF((5,0,"Scan Dir Info max_right_mask=%d", (int)result));
                         } else completition = (uint8)-result;
                       } else  if (*p == 0x3){ /* Get Direktory Rights */
                   /******** Get Eff Dir Rights ****************/
                         struct XDATA {
                           uint8 eff_right_mask;  /* Effektive Right to Dir, old! */
                         } *xdata = (struct XDATA*) responsedata;
                         int result = nw_get_eff_dir_rights(
                                        (int)*(p+1),
                                               p+3,
                                        (int)*(p+2), 0);
                         if (result > -1) {
                           xdata->eff_right_mask = (uint8) result;
                           data_len = 1;
                           XDPRINTF((5,0,"Got eff Dir Rights=%d", (int)result));
                         } else completition = (uint8) -result;
                       } else  if (*p == 0x4){ /* Modify Max Right MAsk */
                   /******** MODIFY MAX RIGHT MASK ****************/
                         /* NO REPLY !! */
                         completition = 0xfb;  /* TODO */
                       } else  if (*p == 0x5){ /* Get Volume Number 0 .. 31 */
                   /******** GetVolume Number ***************/
                         /* p+1 = namelen */
                         /* p+2 = data z.b 'SYS' */
                         struct XDATA {
                           uint8 volume;  /* Nummer */
                         } *xdata = (struct XDATA*) responsedata;
                         int result = nw_get_volume_number(p+2, (int)*(p+1));
                         if (result > -1) {
                           xdata->volume = (uint8) result;
                           data_len = 1;
                         } else completition = (uint8) -result;
                       } else  if (*p == 0x6){ /* Get Volume Name from 0 .. 31 */
                   /******** Get Volume Name  ***************/
                         struct XDATA {
                           uint8 namelen;
                           uint8 name[16];
                         } *xdata = (struct XDATA*) responsedata;
                         int result = nw_get_volume_name((int)*(p+1), xdata->name);
                         if (result > -1) {
                           xdata->namelen = (uint8) result;
                           data_len       = result+1;
                         } else completition = (uint8) -result;
                       } else if (*p == 0xa){ /* legt Verzeichnis an */
                   /******** Create Dir *********************/
                         int dir_handle  = (int) *(p+1);
#if 0
                         int rightmask   = (int) *(p+2);
#endif
                         int pathlen     = (int) *(p+3);
                         uint8 *path     =  p+4;
                         int code = nw_mk_rd_dir(dir_handle, path, pathlen, 1);
                         if (code) completition = (uint8) -code;
                       } else  if (*p == 0xb){ /* deletes dir */
                   /******** Delete DIR *********************/
                         int dir_handle  = (int) *(p+1);
#if 0
                         int reserved    = (int) *(p+2); /* Res. by NOVELL */
#endif
                         int pathlen     = (int) *(p+3);
                         uint8 *path     =  p+4;
                         int code = nw_mk_rd_dir(dir_handle, path, pathlen, 0);
                         if (code) completition = (uint8) -code;
                       } else  if (*p == 0xd){  /* Add Trustees to DIR  */
                   /******** AddTrustesstoDir ***************/
#if 0
                         struct INPUT {
                           uint8   header[7];      /* Requestheader */
                           uint8   div[3];            /* 0x0, dlen, ufunc */
                           uint8   dir_handle;     /* Verzeichnis Handle */
                           uint8   trustee_id[4];  /* Trustee Object ID  */
                           uint8   trustee_right_mask;
                           uint8   pathlen;
                           uint8   path;
                         } *input = (struct INPUT *) (ncprequest);
                          /* TODO !!!!!!!!!!!!!!!!!!!!  */
#endif
                         do_druck++;
                       } else  if (*p == 0xf){ /* rename dir */
                   /******** Rename DIR *********************/
                         int dir_handle  = (int) *(p+1);
                         int oldpathlen  = (int) *(p+2);
                         uint8 *oldpath  =       p+3;
                         int newpathlen  = (int) *(oldpath + oldpathlen);
                         uint8 *newpath  =       oldpath + oldpathlen + 1;
                         int code = mv_dir(dir_handle,
                                              oldpath, oldpathlen,
                                              newpath, newpathlen);
                         if (code) completition = (uint8) -code;
                       } else  if (*p == 0x12 /* Allocate Permanent Dir Handle */

                   /******** Allocate Permanent DIR Handle **/
                          || *p == 0x13     /* Allocate Temp Dir Handle */
                   /******** Allocate Temp DIR Handle **/
                          || *p == 0x16)  {   /* Allocate Special Temp Dir Handle */
                   /******** Allocate spez temp  DIR Handle **/
                         struct XDATA {
                           uint8 dirhandle;   /* new Dir Handle   */
                           uint8 right_mask;  /* 0xff effektive Right MAsk ? */
                         } *xdata = (struct XDATA*) responsedata;
                         int dirhandle = nw_alloc_dir_handle(
                                            (int) *(p+1),
                                                   p+4,
                                            (int)*(p+3),
                                            (int)*(p+2),
                                            (*p==0x12) ? 0
                                         : ((*p==0x13) ? 1 : 2),
                                         (int)(ncprequest->task));
                         if (dirhandle > -1){
                           xdata->dirhandle  = (uint8) dirhandle;
                           xdata->right_mask = 0xff;
                           data_len = sizeof(struct XDATA);
                         } else completition = (uint8) -dirhandle;

                       } else  if (*p == 0x14){ /* deallocate Dir Handle */
                   /******** Free DIR Handle ****************/
                         int err_code = nw_free_dir_handle((int)*(p+1),
                                         (int)(ncprequest->task));
                         if (err_code) completition = (uint8) -err_code;
                       } else if (*p == 0x15){ /* liefert Volume Information */
                   /******** Get Volume Info with Handle ****/
                         struct XDATA {
                           uint8  sectors[2];
                           uint8  total_blocks[2];
                           uint8  avail_blocks[2];
                           uint8  total_dirs[2];  /* anz dirs  */
                           uint8  avail_dirs[2];  /* free dirs */
                           uint8  name[16];       /* SYS Name  */
                           uint8  removable[2];
                         } *xdata = (struct XDATA*)responsedata;
                         int result = nw_get_vol_number((int)*(p+1));
                         memset(xdata, 0, sizeof(struct XDATA));
                         if (result > -1) {
                           int volume = result;
                           result = nw_get_volume_name(volume, xdata->name);
                           if (result > -1) {
                             struct fs_usage fsp;
                             if (!nw_get_fs_usage(xdata->name, &fsp)) {
                               int sector_scale=1;
                               while (fsp.fsu_blocks/sector_scale > 0xffff)
                                  sector_scale+=2;
                               U16_TO_BE16(sector_scale, xdata->sectors);
                               U16_TO_BE16(fsp.fsu_blocks/sector_scale, xdata->total_blocks);
                               U16_TO_BE16(fsp.fsu_bavail/sector_scale, xdata->avail_blocks);
                               U16_TO_BE16(fsp.fsu_files,  xdata->total_dirs);
                               U16_TO_BE16(fsp.fsu_ffree,  xdata->avail_dirs);
                               if (get_volume_options(volume) & VOL_OPTION_REMOUNT) {
                                 U16_TO_BE16(1,  xdata->removable);
                               } else {
                                 U16_TO_BE16(0,  xdata->removable);
                               }
                             }
                             data_len = sizeof(struct XDATA);
                             XDPRINTF((5,0,"GIVE VOLUME INFO from :%s:", xdata->name));
                             result = 0;
                           }
                         }
                         completition = (uint8)-result;
                       } else  if (*p == 0x19){
                        /* Set Directory Information
                         * Modifies basic directory information as creation date and
                         * directory rights mask. DOS namespace.
                         */
#if 0
                         struct INPUT {
                           uint8   header[7];         /* Requestheader */
                           uint8   div[3];            /* 0x0, dlen, ufunc */
                           uint8   dir_handle;
                           uint8   creation_date[2];
                           uint8   creation_time[2];
                           uint8   owner_id[4];
                           uint8   new_max_rights;
                           uint8   pathlen;
                           uint8   path;
                         } *input = (struct INPUT *) (ncprequest);
#endif
                         /* No REPLY  */
                         completition = 0xfb;  /* !!!!! TODO !!!! */
                       } else  if (*p == 0x1a){ /* Get Pathname of A Volume Dir Pair */
#if 0
                         struct INPUT {
                           uint8   header[7];      /* Requestheader */
                           uint8   div[3];            /* 0x0, dlen, ufunc */
                           uint8   volume;
                           uint8   dir_entry[2];
                         } *input = (struct INPUT *) (ncprequest);
                         struct XDATA {
                           uint8  pathlen;
                           uint8  pathname;
                         } *xdata = (struct XDATA*)responsedata;
#endif
                         completition = 0xfb;  /* !!!!! TODO !!!! */
                       } else  if (*p == 0x1e){
                         /* SCAN a Directory */
                         struct INPUT {
                           uint8   header[7];        /* Requestheader */
                           uint8   div[3];            /* 0x0, dlen, ufunc */
                           uint8   dir_handle;       /* Verzeichnis Handle */
                           uint8   attrib;           /* Search Attrib z.B. 0x6 */
                           uint8   searchsequence[4]; /* 32 bit */
                           uint8   len;
                           uint8   data[2];
                         } *input = (struct INPUT *) (ncprequest);
                         int result = nw_scan_a_directory(
                                      responsedata,
                                      input->dir_handle,
                                      input->data,
                                      input->len,
                                      input->attrib,
                                      GET_BE32(input->searchsequence));

                         if (result > -1) data_len = result;
                         else completition = (uint8) (-result);
                       } else  if (*p == 0x1f){
                         /* SCAN a root dir ????  */
                         struct INPUT {
                           uint8   header[7];        /* Requestheader      */
                           uint8   div[3];           /* 0x0, dlen, ufunc */
                           uint8   dir_handle;       /* Verzeichnis Handle */
                           uint8   dont_know1;       /* ????  0xc0  */
                           uint8   dont_know2;       /* ????  0xfa  */
                         } *input = (struct INPUT *) (ncprequest);
                         int result = nw_scan_a_root_dir(
                                      responsedata,
                                      input->dir_handle);
                         if (result > -1) data_len = result;
                         else completition = (uint8) (-result);
                       } else  if (*p == 0x20){
                       /* scan volume user disk restrictions */
                         uint8  volnr    = *(p+1);
                         /* uint32 sequenz  = GET_BE32(p+2); */
                         struct XDATA {
                           uint8  entries;  /* 0x0   */
                           /*--- per entry (max.entries = 12) ----*/
                           uint8  id[4];
                           uint8  restriction[4];
                         } *xdata = (struct XDATA*) responsedata;
                         int result = nw_get_volume_name(volnr, NULL);
                         if (result > -1) {
                           xdata->entries = 0x0;
                           data_len = (8 * xdata->entries) + 1;
                         } else completition = (uint8) (-result);
                       } else  if (*p == 0x21) {
                         /* change Vol restrictions for Obj */
                         XDPRINTF((5, 0, "Change vol restrictions"));
                         return(-2); /* nwbind must do prehandling */
                       } else  if (*p == 0x22) {
                         /* remove Vol restrictions for Obj */
                         XDPRINTF((5, 0, "Remove vol restrictions"));
                         return(-2); /* nwbind must do prehandling */
                       } else  if (*p == 0x25){
                        /* Set Entry, Set Directory File Information
                         * sets or changes the file or directory information to the
                         * values entered in 'Change Bits'.
                         */
                         /* NO REPLY */
                         /* ncopy use this call */
                          /* TODO !!!!!!!!!!!!!!!!!!!!  */

                         do_druck++;

                       } else  if (*p == 0x26) { /* Scan file or Dir for ext trustees */
                         int sequenz = (int)*(p+2); /* trustee sequenz  */
                         struct XDATA {
                           uint8  entries;
                           uint8  ids[80];       /* 20 id's */
                           uint8  trustees[40];  /* 20 trustees's */
                         } *xdata = (struct XDATA*) responsedata;
                         int result  = nw_get_eff_dir_rights(
                                       (int)*(p+1),
                                       p+4,
                                       (int)*(p+3), 1);
                         if (result > -1){
                           if (!sequenz) {
                             memset(xdata, 0, sizeof(struct XDATA));
                             xdata->entries=1;
#if 0
                             U32_TO_BE32(1, xdata->ids);  /* SUPERVISOR  */
#else
                             U32_TO_BE32(act_obj_id, xdata->ids);  /* actual LOGIN User */
                             /* NOTE: this should be made better */
#endif
                             xdata->trustees[1] = 0x1;    /* Supervisory */
                             xdata->trustees[0] = 0xff;   /* all low     */
                             data_len = sizeof(struct XDATA);
                           } else completition = 0x9c;  /* no more trustees */
                         } else completition = (uint8) (-result);
                       } else  if (*p == 0x27) { /* Add Ext Trustees to DIR */
#if 0
                         struct INPUT {
                           uint8   header[7];         /* Requestheader     */
                           uint8   div[3];            /* 0x0, dlen, ufunc  */
                           uint8   dir_handle;        /* Handle            */
                           uint8   trustee_id[4];     /* Trustee Object ID */
                           uint8   trustee_rights[2]; /* low - high        */
                           uint8   pathlen;
                           uint8   path;
                         } *input = (struct INPUT *) (ncprequest);
                          /* TODO !!!!!!!!!!!!!!!!!!!!  */
#endif
                         do_druck++;
                       } else if (*p == 0x28){
                        /* Scan File Physical  ??? */
                         struct INPUT {
                           uint8   header[7];         /* Requestheader */
                           uint8   div[3];            /* 0x0, dlen, ufunc */
                           uint8   dir_handle;        /* directory handle */
                           uint8   attrib;            /* Search Attrib ?? 0x2f */
                           uint8   searchsequence[4]; /* 32 bit */
                           uint8   len;
                           uint8   data[2];
                         } *input = (struct INPUT *) (ncprequest);
                         /* we try, whether this is ok  ????? */
                         int result = nw_scan_a_directory(
                                      responsedata,
                                      input->dir_handle,
                                      input->data,
                                      input->len,
                                      input->attrib,
                                      GET_BE32(input->searchsequence));
                         if (result > -1) data_len = result;
                         else completition = (uint8) (-result);
                       } else  if (*p == 0x29){
                        /* read  volume restrictions for an object */
#if QUOTA_SUPPORT
                         XDPRINTF((5, 0, "Read vol restrictions"));
                         return(-2); /* nwbind must do prehandling */
#else
#if DO_DEBUG
                          uint8  volnr = *(p+1);
                          uint32 id    = GET_BE32(p+2);
#endif
                          struct XDATA {
                            uint8 restriction[4];
                            uint8 inuse[4];
                          } *xdata = (struct XDATA*) responsedata;
                          XDPRINTF((5,0, "Get vol restriction (DUMMY) vol=%d, id=0x%lx",
                                   (int)volnr, id));
                          U32_TO_32(0x40000000, xdata->restriction);
                          U32_TO_32(0x0,        xdata->inuse);
                          data_len=sizeof(struct XDATA);
#endif
                       } else  if (*p == 0x2a){
                          /* Get Eff. Rights of DIR's and Files  ??*/
                         struct XDATA {
                           uint8    eff_rights;   /* Effektive Right to Dir */
                           uint8    unkwown_data; /* 0x1  */
                         } *xdata = (struct XDATA*) responsedata;
                         int result = nw_get_eff_dir_rights(
                                       (int)*(p+1),
                                       p+3,
                                       (int)*(p+2), 1);
                         if (result > -1){
                           xdata->eff_rights   = (uint8)result;
                           xdata->unkwown_data = 0x1;
                           data_len            = sizeof(struct XDATA);
                         } else completition = (uint8) (-result);
                       } else  if (*p == 0x2c){
                         /* Get Volume and Purge Information */
                         /* new Call since V3.11 */
                         /* ncpfs need this call */
                         int volume   = (int) *(p+1);
                         struct XDATA {
                           uint8 total_blocks[4];        /* LOW-HI !! */
                           uint8 avail_blocks[4];
                           uint8 purgeable_blocks[4];
                           uint8 not_purgeable_blocks[4];
                           uint8 total_dirs[4];
                           uint8 avail_dirs[4];
                           uint8 reserved_by_novell[4];
                           uint8 sec_per_block;
                           uint8 namlen;
                           uint8 name[1];
                         } *xdata = (struct XDATA*) responsedata;
                         uint8 name[100];
                         int result = nw_get_volume_name(volume, name);
                         if (result > -1){
                           struct fs_usage fsp;
                           memset(xdata, 0, sizeof(struct XDATA));
                           if (!nw_get_fs_usage(name, &fsp)) {
                             xdata->sec_per_block = 8; /* hard coded */
                             U32_TO_32(fsp.fsu_blocks/8, xdata->total_blocks);
                             U32_TO_32(fsp.fsu_bavail/8, xdata->avail_blocks);
                             U32_TO_32(fsp.fsu_files,  xdata->total_dirs);
                             U32_TO_32(fsp.fsu_ffree,  xdata->avail_dirs);
                           }
                           xdata->namlen   = strlen((char*)name);
                           strmaxcpy(xdata->name, name, xdata->namlen);
                           data_len = xdata->namlen + 30;
                         } else completition = (uint8) -result;
                       } else  if (*p == 0x2d){
                         /* Get Direktory Information */
                         int dir_handle = (int) *(p+1);
                         struct XDATA {
                           uint8    total_blocks[4];
                           uint8    avail_blocks[4];
                           uint8    total_dirs[4];
                           uint8    avail_dirs[4];
                           uint8    reserved_by_novell[4];
                           uint8    sec_per_block;
                           uint8    namlen;
                           uint8    name[1];  /* Volume Name  */
                         } *xdata = (struct XDATA*) responsedata;
                         int result = nw_get_vol_number(dir_handle);
                         uint8 name[100];
                         if (result > -1)
                           result = nw_get_volume_name(result, name);
                         if (result > -1) {
                           struct fs_usage fsp;
                           memset(xdata, 0, sizeof(struct XDATA));
                           if (!nw_get_fs_usage(name, &fsp)) {
                             xdata->sec_per_block = 8; /* hard coded */
                             U32_TO_32(fsp.fsu_blocks/8, xdata->total_blocks);
                             U32_TO_32(fsp.fsu_bavail/8, xdata->avail_blocks);
                             U32_TO_32(fsp.fsu_files,  xdata->total_dirs);
                             U32_TO_32(fsp.fsu_ffree,  xdata->avail_dirs);
                           }
                           xdata->namlen = strlen((char*)name);
                           strmaxcpy(xdata->name, name, xdata->namlen);
                           data_len = xdata->namlen + 22;
                         } else completition = (uint8) -result;
                       } else  if (*p == 0x2e){  /* RENAME DATEI */
                         completition   = 0xfb;  /* TODO: !!! */

#if WITH_NAME_SPACE_CALLS
                       } else  if (*p == 0x2f){
                         /* Fill namespace buffer */
                         /* ncopy use this call */
                         int volume     = (int) *(p+1);
                         /* (p+2) == 0xe4 or 0xe2 sometimes  ???? */
                         int result=fill_namespace_buffer(
                            volume, responsedata);
                         if (result > -1) {
                           data_len = result;
                         } else completition = (uint8) -result;
                       } else  if (*p == 0x30){
                         /* Get Name Space Directory Entry */
                         int volume          = (int) *(p+1);
                         uint32 basehandle   =  GET_32(p+2);
                         int    namespace    = (int) *(p+6);
                         int    result=get_namespace_dir_entry(
                                 volume, basehandle, namespace,
                                 responsedata);
                         if (result > -1) {
                           data_len = result;
                         } else completition = (uint8) -result;
#endif
                       } else completition = 0xfb;  /* unkwown request */
                     }
                     break;
         case 0x17 : {  /* FILE SERVER ENVIRONMENT */
           /* uint8 len   = *(requestdata+1); */
           uint8 ufunc    = *(requestdata+2);
#if DO_DEBUG
           uint8 *rdata   = requestdata+3;
#endif
           switch (ufunc) {
#if FUNC_17_02_IS_DEBUG
             case 0x02 :  {
                /* I hope this call isn't used       */
                /* now missused as a debug switch :) */
               struct XDATA {
                 uint8  nw_debug;   /* old level */
               } *xdata = (struct XDATA*) responsedata;
               if (*rdata == NWCONN) {
                 xdata->nw_debug = (uint8)org_nw_debug;
                 nw_debug = org_nw_debug = (int) *(rdata+1);
                 data_len = 1;
               } else return(-1);
             }
             break;
#endif

             case 0x14: /* Login Objekt, unencrypted passwords */
             case 0x18: /* crypt_keyed LOGIN */
             return(-2); /* nwbind must do prehandling */


             case 0x0f: { /* Scan File Information  */
               struct INPUT {
                 uint8   header[7];      /* Requestheader   */
                 uint8   div[3];         /* 0, len + ufunc   */
                 uint8   sequence[2];    /* z.B. 0xff, 0xff */
                 uint8   dir_handle;
                 uint8   search_attrib;  /*    0: NONE    */
                                         /*   02: HIDDEN  */
                                         /*   04: SYSTEM  */
                                         /*   06: BOTH    */
                                         /* 0x10: DIR     */
                 uint8   len;
                 uint8   data[2];        /* Name        */
               } *input = (struct INPUT *)ncprequest;

               struct XDATA {
                 uint8          sequence[2];      /* next sequence */
                 /* NW_FILE_INFO f; */
                 uint8          f[sizeof(NW_FILE_INFO)];
                 uint8          owner_id[4];
                 uint8          archive_date[2];
                 uint8          archive_time[2];
                 uint8          reserved[56];
               } *xdata = (struct XDATA*)responsedata;
               int len = input->len;
               int searchsequence;
               NW_FILE_INFO f;
               uint32   owner;

               memset(xdata, 0, sizeof(struct XDATA));
               searchsequence = nw_search( (uint8*) &f,
                                &owner,
                                (int)input->dir_handle,
                                (int) GET_BE16(input->sequence),
                                (int) input->search_attrib,
                                input->data, len);
               if (searchsequence > -1) {
                 memcpy(xdata->f, &f, sizeof(NW_FILE_INFO));
                 U16_TO_BE16((uint16) searchsequence, xdata->sequence);
                 U32_TO_BE32(owner, xdata->owner_id);
                 data_len = sizeof(struct XDATA);
               } else completition = (uint8) (- searchsequence);
             }
             break;

             case 0x10: { /* Set  File Information  */
               struct INPUT {
                 uint8   header[7];      /* Requestheader   */
                 uint8   div[3];         /* 0, len + ufunc   */
                 uint8   f[sizeof(NW_FILE_INFO) - 14]; /* no name */
                 uint8   owner_id[4];
                 uint8   archive_date[2];
                 uint8   archive_time[2];
                 uint8   reserved[56];
                 uint8   dir_handle;
                 uint8   search_attrib;  /*    0: NONE    */
                                         /*   02: HIDDEN  */
                                         /*   04: SYSTEM  */
                                         /*   06: BOTH    */
                                         /* 0x10: DIR     */
                 uint8   len;
                 uint8   data[2];        /* Name        */
               } *input = (struct INPUT *)ncprequest;
               NW_FILE_INFO f;
               int result;
               memcpy(((uint8*)&f)+14, input->f, sizeof(NW_FILE_INFO)-14);
               result = nw_set_file_information((int)input->dir_handle,
                                               input->data,
                                               (int)input->len,
                                               (int)input->search_attrib, &f);
               /* no reply packet */
               if (result <0) completition = (uint8)-result;
             }
             break;

             case 0x68:   /* create queue job and file old */
             case 0x79:   /* create queue job and file     */
             return(-2);  /* nwbind must do prehandling    */

             case 0x69:    /* close file and start queue old ?? */
             case 0x7f: {  /* close file and start queue */
               struct INPUT {
                 uint8   header[7];          /* Requestheader */
                 uint8   packetlen[2];       /* low high      */
                 uint8   func;               /* 0x7f or 0x69  */
                 uint8   queue_id[4];        /* Queue ID      */
                 uint8   job_id[4];          /* result from creat queue    */
                                             /* if 0x69 then only first 2 byte ! */
               } *input = (struct INPUT *) (ncprequest);
               uint32 q_id = GET_BE32(input->queue_id);
               int  job_id = (ufunc==0x69) ? GET_BE16(input->job_id)
                                           : GET_BE32(input->job_id);
               int result  = close_queue_job(q_id, job_id);
               if (result < 0) {
                 completition = (uint8)-result;
               } else {
                 return(-2);  /* nwbind must do next    */
               }
             }
             break;

             case 0x7c :  /* service queue job */
             return(-2);  /* nwbind must do prehandling    */

             case 0x83 :  /* finish queue job */
             case 0x84 : { /* abort queue job */
               struct INPUT {
                 uint8   header[7];          /* Requestheader */
                 uint8   packetlen[2];       /* low high      */
                 uint8   func;               /* 0x7f or 0x69  */
                 uint8   queue_id[4];        /* Queue ID      */
                 uint8   job_id[4];          /* result from creat queue    */
                                             /* if 0x69 then only first 2 byte ! */
               } *input = (struct INPUT *) (ncprequest);
               uint32 q_id = GET_BE32(input->queue_id);
               int  job_id = GET_BE32(input->job_id);
               int result  = finish_abort_queue_job(q_id, job_id);
               if (result <0)
                 completition=(uint8) -result;
               else
                return(-1);  /* nwbind must do the rest */
             }
             break;

             case 0xf3: {  /* Map Direktory Number TO PATH */
               XDPRINTF((2,0, "TODO: Map Directory Number TO PATH"));
               completition = 0xff;
             }
             break;

             case 0xf4: {  /* Map PATH TO Dir Entry */
               XDPRINTF((2,0, "TODO: Map PATH TO Dir Entry"));
               completition = 0xff;
             }
             break;

             default : return(-1);
             break;
           } /* switch (ufunc) */
         } /* case 0x17 */
         break;

         case 0x18 : /* End of Job */
                     nw_free_handles(ncprequest->task);
                     break;

         case 0x19 : /* logout, some of this call is handled in ncpserv. */
                     free_queue_jobs();
                     nw_free_handles(-1);
                     set_default_guid();
                     nw_setup_home_vol(-1, NULL);
                     set_act_obj_id(0);  /* NOT logged in */
                     set_program_title(NULL);
                     return(-1); /* nwbind must do a little rest */
                     break;

         case 0x20 : /* Semaphore */
                     return(-1);   /* handled by nwbind */

         case 0x1a : /* lock file  */
         case 0x1e : /* unlock file */
                     {
                       struct INPUT {
                         uint8   header[7];      /* Requestheader */
                         uint8   reserve;        /* 0x1, lock-flag ? */
                         uint8   ext_fhandle[2]; /* all zero      */
                         uint8   fhandle[4];     /* Filehandle    */
                         uint8   offset[4];
                         uint8   size[4];
                         uint8   unknown[2];     /* lock timeout ??? */
                       } *input = (struct INPUT *)ncprequest;
                       int fhandle  = GET_32  (input->fhandle);
                       uint32 offset= GET_BE32(input->offset);
                       uint32 size  = GET_BE32(input->size);
                       completition = (uint8)(-nw_lock_file(fhandle,
                                                             offset, size,
                                               (int)(function == 0x1a)));
                     }
                     break;

         case 0x21 : { /* Negotiate Buffer Size,  Packetsize  */
                       uint8 *getsize=responsedata;
                       rw_buffer_size = min(LOC_RW_BUFFERSIZE,
                                        (int) (GET_BE16((uint8*)requestdata)));
                       U16_TO_BE16(rw_buffer_size, getsize);
                       data_len = 2;
                       XDPRINTF((2,0, "Negotiate Buffer size = 0x%04x,(%d)",
                              (int) rw_buffer_size, (int) rw_buffer_size));
                     }
                     break;

         case 0x22 : { /* div TTS Calls */
                       int ufunc = (int) *requestdata;
                       if (!ufunc) completition=0; /* TTS not availible */
                       else completition=0xfb;     /* request not known */
                     } break;

         case 0x23 : { /* div AFP Calls */
#if 0
                       int ufunc = (int) *requestdata;
#endif
                       completition=0xbf;   /* we say invalid namespace here */
                     } break;

         case 0x3b :  /* commit file to disk        */
         case 0x3d :  /* commit file 		    */
                     {
                       struct INPUT {
                         uint8   header[7];     /* Requestheader */
                         uint8   reserve;
                         uint8   ext_fhandle[2]; /* all zero   */
                         uint8   fhandle[4];     /* filehandle */
                       } *input = (struct INPUT *)ncprequest;
                       uint32 fhandle = GET_32(input->fhandle);
                       int result=nw_commit_file(fhandle);
                       if (result<0)
                         completition=(uint8)-result;
                     }
                     break;

         case 0x3e : { /* FILE SEARCH INIT  */
                       /* returns dhandle for searchings */
                       int  dir_handle = (int)*requestdata;
                       int  len        = (int)*(requestdata+1); /* pathlen */
                       uint8 *p        = requestdata+2;          /* path */
                       struct XDATA {
                         uint8  volume;            /* Volume       */
                         uint8  dir_id[2];         /* Direktory ID */
                         uint8  searchsequenz[2];
                         uint8  dir_rights;        /* Rights       */
                       } *xdata= (struct XDATA*) responsedata;
                       int volume;
                       int searchsequenz;
                       int dir_id;
                       int rights = nw_open_dir_handle(dir_handle, p, len,
                                     &volume, &dir_id, &searchsequenz);
                       if (rights >-1) {
                         xdata->volume = (uint8)volume;
                         U16_TO_BE16((uint16)dir_id,        xdata->dir_id);
                         U16_TO_BE16((uint16)searchsequenz, xdata->searchsequenz);
                         xdata->dir_rights = (uint8)rights;
                         data_len = sizeof(struct XDATA);
                       } else completition = (uint8) -rights;
                     } break;

         case 0x3f : {  /* file search continue */
                        /* Dir_id is from file search init */
                       struct INPUT {
                         uint8   header[7];         /* Requestheader */
                         uint8   volume;            /* Volume ID    */
                         uint8   dir_id[2];         /* from File Search Init */
                         uint8   searchsequence[2]; /* sequence FFFF = first entry */
                         uint8   search_attrib;     /* Attribute */
                             /*     0 none,
                                    2 HIDDEN,
                              *     4 System ,
                                    6 Both,
                              *  0x10 Dir
                              */
                         uint8   len;            /* fnname len */
                         uint8   data[2];        /* fnname with wildcards */
                       } *input = (struct INPUT *) ncprequest;
                         int     len=input->len  ; /* FN Length */

                       struct XDATA {
                         uint8   searchsequence[2]; /* same as request sequence  */
                         uint8   dir_id[2];         /* Direktory ID    */
                       /*  is correct !! */
                         union {
                           NW_DIR_INFO  d;
                           NW_FILE_INFO f;
                         } u;
                       } *xdata = (struct XDATA*)responsedata;

                       int searchsequence = nw_dir_search(
                                             (uint8*) &(xdata->u),
                                             (int) GET_BE16(input->dir_id),
                                             (int) GET_BE16(input->searchsequence),
                                             (int) input->search_attrib,
                                             input->data, len);
                       if (searchsequence > -1) {
                         U16_TO_BE16((uint16) searchsequence, xdata->searchsequence);
                         memcpy(xdata->dir_id, input->dir_id, 2);
                         data_len = sizeof(struct XDATA);
                       } else completition = (uint8) (- searchsequence);
                     }
                     break;

         case 0x40 : /* Search for a File */
                     {
                       struct INPUT {
                         uint8   header[7];      /* Requestheader   */
                         uint8   sequenz[2];     /* z.B. 0xff, 0xff */
                         uint8   dir_handle;     /* z.B  0x1        */
                         uint8   search_attrib;  /* z.B. 0x6        */
                         uint8   len;
                         uint8   data[2];        /* Name          */
                       } *input = (struct INPUT *)ncprequest;
                       struct XDATA {
                         uint8   sequenz[2];    /* answer sequence */
                         uint8   reserved[2];   /* z.B  0x0   0x0  */
                         union {
                           NW_DIR_INFO  d;
                           NW_FILE_INFO f;
                         } u;
                       } *xdata = (struct XDATA*)responsedata;
                       int len = input->len;
                       uint8 my_sequenz[2];
                       int searchsequence;
                       uint32 owner;
                       memcpy(my_sequenz, input->sequenz, 2);
                       searchsequence = nw_search( (uint8*) &(xdata->u),
                                             &owner,
                                             (int)input->dir_handle,
                                             (int) GET_BE16(my_sequenz),
                                             (int) input->search_attrib,
                                             input->data, len);
                       if (searchsequence > -1) {
                         U16_TO_BE16((uint16) searchsequence, xdata->sequenz);
                         data_len = sizeof(struct XDATA);
                       } else completition = (uint8) (- searchsequence);
                     }
                     break;

         case 0x41  : {  /* open file for reading */
                       struct INPUT {
                         uint8   header[7];     /* Requestheader */
                         uint8   dirhandle;     /* Dirhandle     */
                         uint8   attrib;        /* z.B. 0x6 od. 0x4e  */
                               /* O_RDWR|TRUNC 0x6, O_RDONLY 0x6 */
                         uint8   len;           /* namelaenge */
                         uint8   data[2];       /* Name       */
                       } *input = (struct INPUT *)ncprequest;
                       struct XDATA {
                         uint8   ext_fhandle[2]; /* all zero       */
                         uint8   fhandle[4];     /* Dateihandle    */
                         uint8   reserve2[2];    /* z.B  0x0   0x0 */
                         NW_FILE_INFO fileinfo;
                       } *xdata= (struct XDATA*)responsedata;
                       int  fhandle=nw_creat_open_file((int)input->dirhandle,
                               input->data, input->len,
                               &(xdata->fileinfo),
                               (int)input->attrib,
                               0x1, 0, (int)(ncprequest->task));

                       if (fhandle > -1){
                         U32_TO_32(fhandle, xdata->fhandle);
                         U16_TO_BE16(0, xdata->ext_fhandle);
                         U16_TO_BE16(0, xdata->reserve2);
                         data_len = sizeof(struct XDATA);
                       } else completition = (uint8) (-fhandle);
                     }
                     break;

         case 0x42 : /* close file */
                     {
                       struct INPUT {
                         uint8   header[7];      /* Requestheader */
                         uint8   reserve;
                         uint8   ext_fhandle[2]; /* all zero   */
                         uint8   fhandle[4];     /* filehandle */
                       } *input = (struct INPUT *)ncprequest;
                       uint32 fhandle = GET_32(input->fhandle);
                       completition = (uint8)(-nw_close_file(fhandle, 0));

#if TEST_FNAME
                       if (!completition && fhandle == test_handle) {
                         do_druck++;
                         test_handle = -1;
                       }
#endif
                     }
                     break;

         case 0x43 : /* creat file, overwrite if exist */
         case 0x4D : /* create new file                */
                     {
                       struct INPUT {
                         uint8   header[7];     /* Requestheader   */
                         uint8   dirhandle;
                         uint8   attribute;     /* creat Attribute */
                         uint8   len;
                         uint8   data[1];       /* Name */
                       } *input = (struct INPUT *)ncprequest;
                       struct XDATA {
                         uint8   extfhandle[2];
                         uint8   fhandle[4];   /* Filehandle */
                         uint8   reserved[2];  /* rese. by NOVELL */
                         NW_FILE_INFO fileinfo;
                       } *xdata= (struct XDATA*)responsedata;
                       int  fhandle=nw_creat_open_file(
                                 (int)input->dirhandle,
                                      input->data,
                                      (int)input->len,
                                      &(xdata->fileinfo),
                                      (int)input->attribute,
                                      0,
                                      (function==0x43) ? 1 : 2,
                                      (int)(ncprequest->task));
                       if (fhandle > -1){
                         data_len = sizeof(struct XDATA);
                         U32_TO_32  (fhandle, xdata->fhandle);
                         U16_TO_BE16(0,       xdata->extfhandle);
                         U16_TO_BE16(0,       xdata->reserved);

#ifdef TEST_FNAME
                         input->data[input->len] = '\0';
                         if (strstr(input->data, TEST_FNAME)){
                           test_handle = fhandle;
                           do_druck++;
                         }
#endif
                       } else completition = (uint8) (-fhandle);
                     }
                     break;

         case 0x44 : /* file(s)   delete */
                     {
                       struct INPUT {
                         uint8   header[7];     /* Requestheader */
                         uint8   dirhandle;     /*     0x0 */
                         uint8   searchattributes;
                         /* 0 none, 2 Hidden, 4 System, 6 Both */
                         uint8   len;
                         uint8   data[2];        /* Name */
                       } *input = (struct INPUT *)ncprequest;
                       int err_code = nw_delete_datei((int)input->dirhandle,
                           input->data, (int)input->len);
                       if (err_code < 0)
                          completition = (uint8) -err_code;
                     }
                     break;

         case 0x45 : /* rename file */
                     {
                       struct INPUT {
                         uint8   header[7];     /* Requestheader */
                         uint8   dir_handle;   /* ??     0x1 */
                         uint8   reserve1;      /* z.B. 0x2 */
                         uint8   len;
                         uint8   data[2];        /* Name */
                       } *input = (struct INPUT *)ncprequest;
                       uint8 *p = input->data+input->len; /* reserve z.B. 0x1 */
                                                 /* + 1  = len2 */
                                                 /* + 1  = data2 */
                       int errcode = mv_file(
                                       (int)input->dir_handle, input->data,(int)input->len,
                                       (int)input->dir_handle, p+2, (int)*(p+1) );

                       if (errcode < 0) completition = (uint8) -errcode;
                     }
                     break;

         case 0x46 : /* set file attributes */
                     {
                       struct INPUT {
                         uint8   header[7];     /* Requestheader */
                         uint8   access;        /*  0x80, od 0x0 */
                         /* 0x80 for example is shared */
                         uint8   dir_handle;
                         uint8   attrib;         /* search attrib */
                         uint8   len;
                         uint8   data[2];        /* filename */
                       } *input = (struct INPUT *)ncprequest;
                       completition =
                         (uint8) (-nw_chmod_datei((int)input->dir_handle,
                                           input->data, (int)input->len,
                                           (int)input->attrib,
                                           (int)input->access));
                     }
                     break;

         case 0x47 : /* move pointer to end of file ???? */
                     /* and return filesize ?  */
                     {
                       struct INPUT {
                         uint8   header[7];         /* Requestheader */
                         uint8   filler;
                         uint8   ext_filehandle[2]; /* all zero */
                         uint8   fhandle[4];        /* Dateihandle */
                       } *input = (struct INPUT *)ncprequest;
                       struct XDATA {
                         uint8   size[4];    /* Position ??? */
                       } *xdata=(struct XDATA*)responsedata;
                       int    fhandle  = GET_32(input->fhandle);
                       int    size     = nw_seek_file(fhandle, 0);
                       if (size > -1) {
                         data_len = sizeof(struct XDATA);
                         U32_TO_BE32(size, xdata->size);
                       }
                       else completition = (uint8) -size;
                     }
                     break;

         case 0x48 : /* read file */
                     {
                       struct INPUT {
                         uint8   header[7];     /* Requestheader */
                         uint8   filler;
                         uint8   ext_fhandle[2]; /* all zero */
                         uint8   fhandle[4];     /* filehandle */
                         uint8   offset[4];
                         uint8   max_size[2];    /* byte to readd */
                       } *input = (struct INPUT *)ncprequest;
                       struct XDATA {
                         uint8   size[2]; /* read bytes  */
                         uint8   data[2]; /* read data   */
                       } *xdata=(struct XDATA*)responsedata;
                       int    fhandle  = GET_32  (input->fhandle);
                       int    max_size = GET_BE16(input->max_size);
                       off_t  offset   = GET_BE32(input->offset);
                       int    zusatz   = (offset & 1) ? 1 : 0;
                       int    size;
                       if (max_size > rw_buffer_size) {
                         XDPRINTF((1,0, "wanted read=%d byte > %d",
                           max_size, rw_buffer_size));
                         size = -0x88; /* we say wrong filehandle */
                       } else
                        size = nw_read_file(fhandle,
                                                 xdata->data+zusatz,
                                                 max_size,
                                                 offset);
                       if (size > -1) {
                         U16_TO_BE16(size, xdata->size);
                         data_len=size+zusatz+2;
                       } else completition = (uint8) -size;
                     }
                     break;

         case 0x49 : {  /* write file */
                       struct INPUT {
                         uint8   header[7];     /* Requestheader */
                         uint8   filler;         /* 0 Filler ?? */
                         uint8   ext_handle[2];
                         uint8   fhandle[4];     /* Dateihandle   */
                         uint8   offset[4];      /* SEEK OFFSET    */
                         uint8   size[2];        /* Datasize       */
                         uint8   data[2];        /* Schreibdaten */
                       } *input = (struct INPUT *)ncprequest;
                       off_t  offset     = GET_BE32(input->offset);
                       int    fhandle    = GET_32  (input->fhandle);
                       int    input_size = GET_BE16(input->size);
                       int size          = nw_write_file(fhandle,
                                                 input->data,
                                                 input_size,
                                                 offset);
                       if (size < 0)
                          completition = (uint8) -size;
                       else if (size < input_size)
                          completition = (uint8)0xff;
                     }
                     break;


         case 0x4a : {  /* File SERVER COPY  */
                       /* should be OK */
                       struct INPUT {
                         uint8   header[7];       /* Requestheader */
                         uint8   reserved;        /* Reserved by Novell */
                         uint8   qext_fhandle[2]; /* ext Filehandle */
                         uint8   qfhandle[4];     /* Quellfile */
                         uint8   zext_fhandle[2]; /* ext Filehandle */
                         uint8   zfhandle[4];     /* Zielfile */
                         uint8   qoffset[4];      /* SourceFile Offset */
                         uint8   zoffset[4];      /* DestFile Offset   */
                         uint8   size[4];         /* copysize          */
                       } *input = (struct INPUT *)ncprequest;
                       int    qfhandle   = GET_32  (input->qfhandle);
                       int    zfhandle   = GET_32  (input->zfhandle);
                       off_t  qoffset    = GET_BE32(input->qoffset);
                       off_t  zoffset    = GET_BE32(input->zoffset);
                       uint32 input_size = GET_BE32(input->size);
                       int size          = nw_server_copy(qfhandle, qoffset,
                                                    zfhandle, zoffset,
                                                    input_size);
                       if (size < 0) completition = (uint8) -size;
                       else {
                         struct XDATA {
                           uint8   zsize[4];   /* real transfered Bytes */
                         } *xdata= (struct XDATA*)responsedata;
                         U32_TO_BE32(size, xdata->zsize);
                         data_len = sizeof(struct XDATA);
                       }
                     }
                     break;


         case 0x4b : {  /* set date of file, file will be closed later */
                       struct INPUT {
                         uint8   header[7];  /* Requestheader */
                         uint8   filler;
                         uint8   reserve[2]; /* ext Filehandle  */
                         uint8   fhandle[4]; /* Dateihandle */
                         uint8   zeit[2];    /* time    */
                         uint8   datum[2];   /* date    */
                       } *input = (struct INPUT *)ncprequest;
                       int result = nw_set_fdate_time(GET_32(input->fhandle),
                                    input->datum, input->zeit);
                       if (result <0) completition = (uint8) -result;
                     }
                     break;

         case 0x4c  : {  /* open file */
                       struct INPUT {
                         uint8   header[7];     /* Requestheader */
                         uint8   dirhandle;     /* Dirhandle     */
                         uint8   attrib;        /* z.B. 0x6 od. 0x4e  */
                               /* O_RDWR|TRUNC 0x6, O_RDONLY 0x6 */

                         uint8   access; /* z.B. 0x9 od 0x11 od. 0x13 */
                               /* O_RDWR|TRUNC  0x13, O_RDONLY 0x11 */
                               /* O_RDWR|TRUNC|O_DENYNONE 0x3 */
                               /* 0x10 BINAERMODUS ??    */
                               /* 0x02 do write          */
                         uint8   len;           /* namelaenge */
                         uint8   data[2];       /* Name       */
                       } *input = (struct INPUT *)ncprequest;
                       struct XDATA {
                         uint8   ext_fhandle[2]; /* all zero       */
                         uint8   fhandle[4];     /* Dateihandle    */
                         uint8   reserve2[2];    /* z.B  0x0   0x0 */
                         NW_FILE_INFO fileinfo;
                       } *xdata= (struct XDATA*)responsedata;
                       int  fhandle=nw_creat_open_file((int)input->dirhandle,
                               input->data, input->len,
                               &(xdata->fileinfo),
                               (int)input->attrib,
                               (int)input->access, 0,
                               (int)(ncprequest->task));

                       if (fhandle > -1){
                         U32_TO_32  (fhandle, xdata->fhandle);
                         U16_TO_BE16(0, xdata->ext_fhandle);
                         U16_TO_BE16(0, xdata->reserve2);

                         data_len = sizeof(struct XDATA);
#ifdef TEST_FNAME
                         input->data[input->len] = '\0';
                         if (strstr(input->data, TEST_FNAME)){
                           test_handle = fhandle;
                           do_druck++;
                         }
#endif

                       } else completition = (uint8) (-fhandle);
                     }
                     break;

#if WITH_NAME_SPACE_CALLS
         case 0x56 : /* some extended atrribute calls */
                     {
                       int result = handle_func_0x56(requestdata, responsedata, ncprequest->task);
                       if (result > -1) data_len = result;
                       else completition=(uint8)-result;
                     }
                     break;

         case 0x57 : /* some new namespace calls */
                     {
                       int result = handle_func_0x57(requestdata, responsedata, ncprequest->task);
                       if (result > -1) data_len = result;
                       else completition=(uint8)-result;
                     }
                     break;
#endif

#ifdef _MAR_TESTS_XX
         case 0x5f : { /* ????????????? UNIX Client */
                       /* a 4.1 Server also do not know this call */
                       struct INPUT {
                         uint8   header[7];  /* Requestheader */
                         uint8   unknown[4]; /* 0x10, 0,0,0  */
                       } *input = (struct INPUT *)ncprequest;
                       completition = 0;
                     }
                     break;

#endif

         case 0x61 :
#if ENABLE_BURSTMODE
                     if (server_version_flags&1) { /* enable Burstmode */
                       /* Negotiate Buffer Size,  Packetsize new ?  */
                       int   wantsize = GET_BE16((uint8*)requestdata);
                       /* wantsize is here max.
                        * phys. packet size without MAC-header
                        * e.g. 1500 if ethernet
                        */
                       int   flags    = (int)   *(requestdata+2);
                      /**** flags ***********************
                       * CHECKSUMMING_REQUESTED         1
                       * SIGNATURE_REQUESTED            2
                       * COMPLETE_SIGNATURES_REQUESTED  4
                       * ENCRYPTION_REQUESTED           8
                       * LIP_DISABLED                0x80
                       **********************************/
                       struct XDATA {
                         uint8   getsize[2];
                         uint8   socket[2];      /* echo socket */
                         uint8   flags;          /* zero        */
                       } *xdata= (struct XDATA*)responsedata;
                       memset(xdata, 0, sizeof(*xdata));
                       wantsize = min(IPX_MAX_DATA+30,     wantsize);
                       rw_buffer_size = min(RW_BUFFERSIZE, wantsize-64);

                       U16_TO_BE16(wantsize,  xdata->getsize);
                       U16_TO_BE16(sock_echo, xdata->socket);
                       data_len = sizeof(*xdata);
                       XDPRINTF((5,0, "Negotiate Buffer (new) =0x%04x,(%d), flags=0x%x",
                              (int) wantsize, (int) wantsize, flags));
                     } else
#endif
                     {
                       XDPRINTF((2,0, "Function '0x61' (Burst) not enabled"));
                       completition = 0xfb; /* unknown request */
                       nw_debug=0;
                     }
                     break;

         case 0x65 :  /* Packet Burst Connection Request */
#if ENABLE_BURSTMODE
                     if (server_version_flags&1) { /* enable burstmode */
                       struct INPUT {
                         uint8   header[7];          /* Requestheader   */
                         uint8   connid[4];          /* RANDOM ID       */
                                                     /* build by time() */
                         uint8   max_packet_size[4]; /* HI-LO */
                         /* max_packet_size is here max.
                          * phys. packet size without MAC-header
                          * e.g. 1500 if ethernet
                          */
                         uint8   target_socket[2];   /* HI-LO */
                         uint8   max_send_size[4];   /* HI-LO */
                         uint8   max_recv_size[4];   /* HI-LO */
                       } *input = (struct INPUT *)ncprequest;
                       struct XDATA {
                         uint8   server_id[4];       /* RANDOM ID       */
                                                     /* build by time() */
                         uint8   max_packet_size[4]; /* HI-LO        */
                         uint8   max_send_size[4];   /* HI-LO        */
                         uint8   max_recv_size[4];   /* HI-LO        */
                       } *xdata= (struct XDATA*) responsedata;
                       int client_socket=GET_BE16(input->target_socket);
                       uint32 max_packet_size=min(sizeof(IPX_DATA),
                                 GET_BE32(input->max_packet_size)-30);
                       U32_TO_BE32(max_packet_size + 30,
                           xdata->max_packet_size);
                       if (!burst_w)
                         burst_w=(BURST_W*)xcmalloc(sizeof(BURST_W));
                       xfree(burst_w->sendburst);
                       xfree(burst_w->send_buf);
                       xfree(burst_w->recv_buf);

                       burst_w->max_burst_data_size=
                          max_packet_size-sizeof(BURSTPACKET);

                       burst_w->sendburst=
                         (BURSTPACKET*)xcmalloc(max_packet_size);

  		       burst_w->ud.udata.buf = (char*)(burst_w->sendburst);

                       burst_w->sendburst->type[0]=0x77;
                       burst_w->sendburst->type[1]=0x77;
                       burst_w->sendburst->streamtyp=2; /* BIG_SEND_BURST */

                       U32_TO_BE32(time(NULL),   burst_w->sendburst->source_conn);
                       U16_TO_16(act_connection, burst_w->sendburst->source_conn);
                       /* we need to identify it */

                       memcpy(xdata->server_id,
                              burst_w->sendburst->source_conn, 4);
                       memcpy(burst_w->sendburst->dest_conn,
                              input->connid, 4);

                       burst_w->max_send_size=
                         min(max_burst_send_size,
                           GET_BE32(input->max_recv_size));
                       burst_w->send_buf=xcmalloc(burst_w->max_send_size+8);

                       burst_w->max_recv_size=
                         min(max_burst_recv_size,
                           GET_BE32(input->max_send_size));
#if 1  /* MUST BE REMOVED LATER !!! */
                       /* we don't want fragmented receive packets */
                       if (burst_w->max_recv_size >
                            burst_w->max_burst_data_size-24)
                          burst_w->max_recv_size
                            =burst_w->max_burst_data_size-24;
#endif
                       burst_w->recv_buf=xcmalloc(burst_w->max_recv_size+24);
#if 1
                       U32_TO_BE32(0x5ff22, burst_w->sendburst->delaytime);
#endif
                       U32_TO_BE32(burst_w->max_recv_size,   xdata->max_recv_size);
                       U32_TO_BE32(burst_w->max_send_size,   xdata->max_send_size);

                       burst_w->ipx_pack_typ     = PACKT_CORE;
                       burst_w->ud.opt.len       = sizeof(uint8);
  		       burst_w->ud.opt.maxlen    = sizeof(uint8);
  		       burst_w->ud.opt.buf       = (char*)&(burst_w->ipx_pack_typ);

                       memcpy(&(burst_w->to_addr), &from_addr, sizeof(ipxAddr_t));
                       U16_TO_BE16(client_socket, burst_w->to_addr.sock);
  		       burst_w->ud.addr.len      = sizeof(ipxAddr_t);
  		       burst_w->ud.addr.maxlen   = sizeof(ipxAddr_t);
  		       burst_w->ud.addr.buf      = (char*)&(burst_w->to_addr);
                       data_len = sizeof(*xdata);
                     } else
#endif
                     {
                       XDPRINTF((2,0, "Packet Burst Connection Request not enabled"));
                       nw_debug=0;
                       completition = 0xfb; /* unknown request */
                     }
                     break;

         case 0x68 :  /* NDS NCP,  NDS Fragger Protokoll ??  */
                   XDPRINTF((2,0, "NDS Fragger Protokoll not supportet"));
                   nw_debug=0;
                   completition = 0xfb; /* unknown request */
                   break;

         default : completition = 0xfb; /* unknown request */
                   break;

    } /* switch function */
  } else if (ncp_type == 0x1111) {
    free_queue_jobs();
    (void) nw_init_connect();
    last_sequence = -9999;
  } else {
    XDPRINTF((1,0, "WRONG TYPE:0x%x", ncp_type));
    completition = 0xfb;
  }

  if (nw_debug && (completition == 0xfb || (do_druck == 1))) { /* UNKWON FUNCTION od. TYPE */
    pr_debug_request();
    if (completition == 0xfb)
      XDPRINTF((0,0, "UNKNOWN FUNCTION od. TYPE: 0x%x", function));
    else if (data_len){
      int j     = data_len;
      uint8  *p = responsedata;
      XDPRINTF((0,2, "RSPONSE: len %d, DATA:",  data_len));
      while (j--) {
        int c = *p++;
        if (c > 32 && c < 127)  XDPRINTF((0,3,",\'%c\'", (char) c));
        else XDPRINTF((0,3,",0x%x", c));
      }
      XDPRINTF((0,1, NULL));
    }
  }
  ncp_response(ncprequest->sequence, ncprequest->task, completition, data_len);
  nw_debug = org_nw_debug;
  return(0);
}

static void handle_after_bind()
{
  NCPREQUEST *ncprequest = (NCPREQUEST*) saved_readbuff;
  uint8 *requestdata  = saved_readbuff + sizeof(NCPREQUEST);
  uint8 *bindresponse = readbuff       + sizeof(NCPRESPONSE);
  int   data_len      = 0;
  int   completition  = 0;
  switch (ncprequest->function) {
      /*  QUOTA support from: Matt Paley  */
    case 0x16 : {
      uint8 ufunc    = *(requestdata+2);
      switch (ufunc) {
        case 0x21: {
          /* change Vol restrictions for Obj */
          uint8  volnr = *(requestdata+3);
          uint32 id     = GET_BE32(requestdata+4);
          uint32 blocks = GET_32(requestdata+8);
          int    gid    = ((int *) bindresponse)[0];
          int    uid    = ((int *) bindresponse)[1];
          int    perm   = ((int *) bindresponse)[2];
          int    result;
          XDPRINTF((5, 0,
               "Change vol rest id=%x vol=%d blocks=%d gid=%d uid=%d p=%d",
                    (int) id, (int) volnr, (int) blocks,
                    (int) gid, (int) uid, (int) perm));
          if (perm == 0) {
            result = nw_set_vol_restrictions(volnr, uid, blocks);
          } else {
            result = -0x85;
          }
          if (result < 0) completition = (uint8)-result;
        }
        break;
        case 0x22: {
          /* Remove Vol restrictions for Obj */
          uint8  volnr = *(requestdata+3);
          uint32 id     = GET_BE32(requestdata+4);
          int    gid    = ((int *) bindresponse)[0];
          int    uid    = ((int *) bindresponse)[1];
          int    perm   = ((int *) bindresponse)[2];
          int result;
          XDPRINTF((1, 0, "Remove vol rest id=%x vol=%d gid=%d uid=%d p=%d",
                    (int) id, (int) volnr, (int) gid, (int) uid, (int) perm));
          if (perm == 0) {
            result = nw_set_vol_restrictions(volnr, uid, 0);
          } else {
            result = -0x85;
          }
          if (result < 0) completition = (uint8)-result;
        }
        break;
        case 0x29: {
          /* Get Vol restrictions for Obj */
          uint8  volnr = *(requestdata+3);
          uint32 id     = GET_BE32(requestdata+4);
          int    gid    = ((int *) bindresponse)[0];
          int    uid    = ((int *) bindresponse)[1];
          int    perm   = ((int *) bindresponse)[2];
          uint32 quota, used;
          int result;
          struct XDATA {
            uint8 restriction[4];
            uint8 inuse[4];
          } *xdata = (struct XDATA*) responsedata;
          XDPRINTF((5, 0, "Get vol rest id=%x vol=%d gid=%d uid=%d p=%d",
                    (int) id, (int) volnr, (int) gid, (int) uid, (int) perm));
          if (perm == 0) {
            result = nw_get_vol_restrictions(volnr, uid, &quota, &used);
          } else {
            result = -0x85;
          }
          data_len = 8;
          if (result == 0) {
            U32_TO_32(quota, xdata->restriction);
            U32_TO_32(used,  xdata->inuse);
          } else {
            U32_TO_32(0x40000000, xdata->restriction);
            U32_TO_32(0x0,        xdata->inuse);
            completition = (uint8) -result;
          }
        }
        break;
        default : completition = 0xfb;
      }
      break;
    }
    case 0x17 : {  /* FILE SERVER ENVIRONMENT */
       uint8 ufunc    = *(requestdata+2);
       switch (ufunc) {
         case 0x14:   /* Login Objekt, unencrypted passwords */
         case 0x18: { /* crypt_keyed LOGIN */
           int   fnlen = (int) *(bindresponse + 3 * sizeof(int));
           uint8 objname[48];
           /* ncpserv have changed the structure */
           set_guid(*((int*)bindresponse), *((int*)(bindresponse+sizeof(int))));
           set_act_obj_id(*((uint32*)(bindresponse + 2 * sizeof(int))));
           nw_setup_home_vol(fnlen, bindresponse   + 3 * sizeof(int) +1);
           if (ufunc==0x14) {
             xstrmaxcpy(objname, requestdata+6, (int) *(requestdata+5));
           } else if (ufunc==0x18){
             xstrmaxcpy(objname, requestdata+14, (int) *(requestdata+13));
           } else objname[0]='\0';
           set_program_title(objname);
         }
         break;

         case 0x68:   /* create queue job and file old */
         case 0x79: { /* create queue job and file     */
                      /* nwbind made prehandling       */
           struct INPUT {
             uint8   header[7];          /* Requestheader   */
             uint8   packetlen[2];       /* low high        */
             uint8   func;               /* 0x79 or 0x68    */
             uint8   queue_id[4];        /* Queue ID        */
             uint8   queue_job[280];     /* oldsize is 256  */
           } *input = (struct INPUT *) (ncprequest);
           uint32  q_id = GET_BE32(input->queue_id);
           uint8  *qjob = bindresponse;
           int result = creat_queue_job(q_id, qjob,
                                         responsedata,
                                         (ufunc == 0x68)  );
           if (result > -1) 
             data_len=result;
           else
             completition = (uint8) -result; 
         }
         break;

         case 0x69:    /* close file and start queue old ?? */
         case 0x7f: {  /* close file and start queue */
           struct INPUT {
             uint8   header[7];          /* Requestheader */
             uint8   packetlen[2];       /* low high      */
             uint8   func;               /* 0x7f or 0x69  */
             uint8   queue_id[4];        /* Queue ID      */
             uint8   job_id[4];          /* result from creat queue    */
                                         /* if 0x69 then only 2 byte ! */
           } *input = (struct INPUT *) (ncprequest);
           struct RINPUT {
             uint8   client_area[152];
             uint8   prc_len;           /* len of printcommand */
             uint8   prc[1];            /* printcommand */
           } *rinput = (struct RINPUT *) (bindresponse);
           uint32 q_id = GET_BE32(input->queue_id);
           int  job_id = (ufunc==0x69) ? GET_BE16(input->job_id)
                                       : GET_BE32(input->job_id);

           int result = close_queue_job2(q_id, job_id,
                                            rinput->client_area,
                                            rinput->prc,
                                            rinput->prc_len);
           if (result < 0) completition = (uint8)-result;
         }
         break;

         case 0x7c : {  /* service queue job */
           struct INPUT {
             uint8   header[7];          /* Requestheader   */
             uint8   packetlen[2];       /* low high        */
             uint8   func;               /* 0x7c            */
             uint8   queue_id[4];        /* Queue ID        */
             uint8   job_typ[2];         /* service typ     */
           } *input = (struct INPUT *) (ncprequest);
           uint32  q_id = GET_BE32(input->queue_id);
           uint8  *qjob = bindresponse;
           int result = service_queue_job(q_id, qjob,
                                      responsedata, 0);
           if (result > -1) 
             data_len=result;
           else
             completition = (uint8) -result; 
         }
         break;

         default : completition = 0xfb;
       }
    }
    break;
    default : completition = 0xfb;
  } /* switch */
  ncp_response(ncprequest->sequence, ncprequest->task, completition, data_len);
}

#if ENABLE_BURSTMODE


static int send_burst(int offset, int datasize, int flags)
{
  BURSTPACKET *sb=burst_w->sendburst;
  U32_TO_BE32(burst_w->packet_sequence++, sb->packet_sequence);
  U32_TO_BE32(offset,   sb->burstoffset);
  U16_TO_BE16(datasize, sb->datasize);
  U16_TO_BE16(0,        sb->missing);
  sb->flags = (uint8)flags;
  memcpy(sb+1, burst_w->send_buf+offset, datasize);
  burst_w->ud.udata.len =
     burst_w->ud.udata.maxlen = datasize+sizeof(BURSTPACKET);
  if (t_sndudata(FD_NCP_OUT, &(burst_w->ud)) < 0){
    if (nw_debug) t_error("t_sndudata in NWCONN !OK");
    return(-1);
  }
  return(0);
}

#include <sys/time.h>
static void sleep_mu(int mu)
{
  struct timeval t;
  t.tv_sec  = 0;
  t.tv_usec = mu;
  (void) select(1, NULL, NULL, NULL, &t);
}

static void handle_burst_response(uint32 offset, int size)
{
  BURSTPACKET *sb=burst_w->sendburst;
  U16_TO_BE16(burst_w->burst_sequence,   sb->burst_seq);
  U16_TO_BE16(burst_w->burst_sequence+1, sb->ack_seq);
  U32_TO_BE32(size,  sb->burstsize);

  while (size) {
    int sendsize=min(size, burst_w->max_burst_data_size);
    int flags=0;
    size-=sendsize;
    if (!size) flags|=0x10; /* EndOfBurst */
    send_burst(offset, sendsize, flags);
#if 0
    sleep_mu(2);
#endif
    offset+=sendsize;
  }

}

static void handle_burst(BURSTPACKET *bp, int len)
{
  if (burst_w) {
    uint32 burstoffset   = GET_BE32(bp->burstoffset);
    int    burstsequence = GET_BE16(bp->burst_seq);
    int    datasize      = GET_BE16(bp->datasize);

    if (datasize && !(bp->flags & 0x80)) {
      /* copy if no System Packet */
      if (datasize+burstoffset > burst_w->max_recv_size+24) {
        XDPRINTF((1, 0, "recv burstpacket offs=%d+size=%d > max_recv+24=%d",
         burstoffset, datasize, burst_w->max_recv_size+24));
        return;
      }
      memcpy(burst_w->recv_buf+burstoffset, bp+1, datasize);
    }

    if (bp->flags & 0x10) {  /* last packet, now action */
      /* 0x10 = EOB flag */
      struct REQ {
        uint8 function[4];           /* lo-hi    1=READ, 2=WRITE   */
        uint8 fhandle[4];            /* from open file             */
        uint8 reserved1[4];          /* all zero                   */
        uint8 reserved2[4];          /* ??? c8,0 od. c9,f0         */
        uint8 file_offset[4];        /* HI-LO  */
        uint8 file_size  [4];        /* HI-LO  */
        uint8 data[2];               /* only Write */
      } *req=(struct REQ*)(burst_w->recv_buf);
      int function=GET_32(req->function);

      if (function == 1 || function == 2) {  /* Read or Write */
        uint32 fhandle = GET_32(req->fhandle);
        uint32 foffset = GET_BE32(req->file_offset);
        uint32 fsize   = GET_BE32(req->file_size);
        if (function == 1) {  /* Read Request */
          struct XDATA {
            uint8 resultcode[4];  /* lo-hi ,
                             * 0=noerror=OK,
                             * 1=init-err,
                             * 2=IO-err,
                             * 3=no data
                             */
            uint8 readbytes[4];   /* hi-lo */
          } *xdata= (struct XDATA*)burst_w->send_buf;
          int zusatz = 0; /* (foffset & 1) ? 1 : 0; */
          int size   = nw_read_file(fhandle,
                                   burst_w->send_buf+sizeof(struct XDATA),
                                   fsize, foffset);
          if (zusatz) {
            XDPRINTF((1, 0, "foffset=%d, fsize=%d", foffset, fsize));
          }
          if (size > -1) {
            U32_TO_32(0,      xdata->resultcode);
            U32_TO_BE32(size, xdata->readbytes);
          } else {
            U32_TO_32(3,      xdata->resultcode);
            U32_TO_BE32(0,    xdata->readbytes);
            size=0;
          }
          burst_w->burst_sequence = burstsequence;
          handle_burst_response(0, size+sizeof(struct XDATA));
        } else { /* Write Request */
          struct XDATA {
            uint8 resultcode[4];  /* lo-hi ,
                                   * 0=noerror=OK,
                                   * 4=write error
                                   */
          } *xdata= (struct XDATA*)burst_w->send_buf;
          int size = nw_write_file(fhandle, req->data, fsize, foffset);
          U32_TO_32(size==fsize ? 0 : 4, xdata->resultcode);
          burst_w->burst_sequence = burstsequence;
          handle_burst_response(0, sizeof(struct XDATA));
        }
      } else {
        XDPRINTF((1, 0, "burst unknow function=0x%x", function));
      }
      req->function[0]=0;
    } else if (bp->flags & 0x80) {  /* System Flag */
      int missing=GET_BE16(bp->missing);
      uint8 *p=(uint8*)(bp+1);
      burst_w->burst_sequence = burstsequence;
      while (missing--){
        int offs=GET_BE32(p);
        int size=GET_BE16(p+4);
        handle_burst_response(offs, size);
        p+=6;
      }
    }
  } else {
    XDPRINTF((1, 0, "burst_w not allocated"));
  }
}
#endif

static void close_all(void)
{
  nw_exit_connect();
  close(0);
  close(FD_NCP_OUT);
}

static int  fl_get_int=0;

static void sig_quit(int rsig)
{
  XDPRINTF((2, 0, "Got Signal=%d", rsig));
  fl_get_int=-1;
}

static void sig_pipe(int rsig)
{
  XDPRINTF((1, 0, "Got SIG_PIPE"));
  signal(SIGPIPE,  sig_pipe);
}

static void sig_hup(int rsig)
{
  if (!fl_get_int) fl_get_int=1;
  signal(SIGHUP,   sig_hup);
}

static void get_new_debug(void)
{
  get_ini_debug(3);
  if (fl_get_int > 0) fl_get_int=0;
}

static void set_sig(void)
{
  signal(SIGTERM,  sig_quit);
  signal(SIGQUIT,  sig_quit);
  signal(SIGINT,   sig_quit);
  signal(SIGPIPE,  sig_pipe);
  signal(SIGHUP,   sig_hup);
  if (use_mmap)
     signal(SIGBUS,   sig_bus_mmap);  /* in nwfile.c */
}
#include <sys/resource.h>

int main(int argc, char **argv)
{
  if (argc != 4 || 3!=sscanf(argv[3], "()INIT-:%x,%x,%x-",
     &father_pid, &sock_nwbind, &sock_echo)) {
    fprintf(stderr, "usage nwconn connid FROM_ADDR ()INIT-:pid,nwbindsock,echosock-\n");
    exit(1);
  }
  prog_title=argv[3];
  setuid(0);
  setgid(0);
  act_connection = atoi(*(argv+1));
  init_tools(NWCONN, act_connection);
  memset(saved_readbuff, 0, sizeof(saved_readbuff));
  XDPRINTF((2, 0, "FATHER PID=%d, ADDR=%s CON:%d",
                  father_pid, *(argv+2), act_connection));
  adr_to_ipx_addr(&from_addr,   *(argv+2));

  if (nw_init_connect()) exit(1);
  act_pid = getpid();

#ifdef LINUX
  set_emu_tli();
#endif
  last_sequence = -9999;
  if (get_ipx_addr(&my_addr)) exit(1);

#if CALL_NWCONN_OVER_SOCKET
# if 1
#  ifdef SIOCIPXNCPCONN
   {
     int conn   = act_connection;
     int result = ioctl(0, SIOCIPXNCPCONN, &conn);
     XDPRINTF((2, 0, "ioctl:SIOCIPXNCPCONN result=%d", result));
   }
#  endif
# endif
#endif

  set_default_guid();
  set_program_title(NULL);

  ud.opt.len       = sizeof(uint8);
  ud.opt.maxlen    = sizeof(uint8);
  ud.opt.buf       = (char*)&ipx_pack_typ;

  ud.addr.len      = sizeof(ipxAddr_t);
  ud.addr.maxlen   = sizeof(ipxAddr_t);
  ud.addr.buf      = (char*)&from_addr;
  ud.udata.buf     = (char*)&ipxdata;

  U16_TO_BE16(0x3333, ncpresponse->type);
  ncpresponse->task            = (uint8) 1;    /* allways 1 */
  ncpresponse->connection      = (uint8)act_connection;
  ncpresponse->high_connection = (uint8)(act_connection >> 8);

  set_sig();

  while (fl_get_int >= 0) {
    int data_len = read(0, readbuff, sizeof(readbuff));
    /* this read is a pipe or a socket read,
     * depending on CALL_NWCONN_OVER_SOCKET
     */

    if (fl_get_int) {
      if (fl_get_int == 1) get_new_debug();
      else if (fl_get_int < 0) break;
    }

    if (data_len > 0) {
      XDPRINTF((99, 0,  "NWCONN GOT DATA len = %d",data_len));
      ncpresponse->connect_status = (uint8) 0;
      ncpresponse->task           = ncprequest->task;

      if ((ncp_type = (int)GET_BE16(ncprequest->type)) == 0x3333) {
        /* this is a response packet */
        data_len -= sizeof(NCPRESPONSE);
        if (saved_sequence > -1
            && ((int)(ncprequest->sequence) == saved_sequence)
            && !ncprequest->function) {
          /* comes from nwbind */
          handle_after_bind();
        } else {
          /* OK for direct sending */
          XDPRINTF((6,0, "NWCONN:direct sending:type 0x3333, completition=0x%x, len=%d",
                 (int)(ncprequest->function), data_len));
          if (data_len)
            memcpy(responsedata, readbuff+sizeof(NCPRESPONSE), data_len);
          ncpresponse->connect_status = ((NCPRESPONSE*)readbuff)->connect_status;
          ncp_response(ncprequest->sequence,
                       ncprequest->task,
                       ncprequest->function, data_len);
        }
        saved_sequence = -1;
#if ENABLE_BURSTMODE
      } else if (ncp_type == 0x7777) { /* BURST-MODE */
        XDPRINTF((16, 0, "GOT BURSTPACKET"));
        handle_burst((BURSTPACKET*)readbuff, data_len);
#endif
      } else { /* this calls I must handle, it is a request */
        int result;
        requestlen  = data_len - sizeof(NCPREQUEST);
        if (0 != (result = handle_ncp_serv()) ) {
          if (result == -2) {
            /* here the actual call must be saved
             * because we need it later, when the request to nwbind
             * returns.
             */
            memcpy(saved_readbuff, readbuff, data_len);
            saved_sequence = (int)(ncprequest->sequence);
          } else saved_sequence = -1;
          /* this call must go to nwbind */
          call_nwbind();
        }
      }
    }
  } /* while */

  seteuid(0);
#  ifdef SIOCIPXNCPCONN
   {
     int conn = -act_connection;
     (void)ioctl(0, SIOCIPXNCPCONN, &conn);
   }
#  endif
  close_all();
  XDPRINTF((2,0, "leave nwconn pid=%d", getpid()));
  return(0);
}
