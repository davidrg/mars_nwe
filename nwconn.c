/* nwconn.c 04-May-96       */
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
#include <dirent.h>
#include "nwvolume.h"
#include "nwfile.h"
#include "connect.h"
#include "nwqueue.h"
#include "namspace.h"


#define  FD_NCP_OUT    3

static int          father_pid    = -1;
static ipxAddr_t    from_addr;
static ipxAddr_t    my_addr;
static struct 	    t_unitdata ud;
static uint8        ipx_pack_typ  =  PACKT_CORE;
static int          last_sequence = -9999;

static IPX_DATA     ipxdata;
static NCPRESPONSE  *ncpresponse=(NCPRESPONSE*)&ipxdata;
static uint8        *responsedata=((uint8*)&ipxdata)+sizeof(NCPRESPONSE);

static int          requestlen;
static uint8        readbuff[IPX_MAX_DATA];

static uint8        saved_readbuff[IPX_MAX_DATA];
static int          saved_sequence=-1;

static NCPREQUEST   *ncprequest  = (NCPREQUEST*)readbuff;
static uint8        *requestdata = readbuff + sizeof(NCPREQUEST);
static int          ncp_type;
static int          sock_nwbind=-1;

static int req_printed=0;

static int ncp_response(int sequence,
	        int completition, int data_len)
{
  ncpresponse->sequence       = (uint8) sequence;
  ncpresponse->completition   = (uint8) completition;
  ncpresponse->reserved       = (uint8) 0;
  last_sequence 	      = sequence;

  if (req_printed) {
    XDPRINTF((0,0, "NWCONN NCP_RESP seq:%d, conn:%d,  compl=0x%x task=%d TO %s",
        (int)ncpresponse->sequence,  (int) ncpresponse->connection, (int)completition,
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
    XDPRINTF((0, 0, "NCP REQUEST: func=0x%02x, ufunc=0x%02x, seq:%03d, task:%02d",
                      (int)ncprequest->function, ufunc,
                      (int)ncprequest->sequence,
                      (int)ncprequest->task));
  } else {
     XDPRINTF((0, 0, "Got NCP:type:0x%x, seq:%d, task:%d, reserved:0x%x, func=0x%x",
                      ncp_type,
                      (int)ncprequest->sequence,
                      (int)ncprequest->task,
                      (int)ncprequest->reserved,
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

static int test_handle = -1;
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
                                  sector_scale*=2;
	                   U16_TO_BE16(sector_scale, xdata->sec_per_block);
	                   U16_TO_BE16(fsp.fsu_blocks/sector_scale, xdata->total_blocks);
	                   U16_TO_BE16(fsp.fsu_bavail/sector_scale, xdata->avail_blocks);
	                   U16_TO_BE16(fsp.fsu_files,  xdata->total_dirs);
	                   U16_TO_BE16(fsp.fsu_ffree,  xdata->avail_dirs);
                           if ( get_volume_options(volume, 1) & VOL_OPTION_REMOUNT) {
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


	 case 0x16 : {
	               /* uint8 len = *(requestdata+1); */
	               uint8 *p  =  requestdata +2;
	               if (0 == *p){
	           /****** * SetDirektoryHandle *************/
	                 struct INPUT {
	                   uint8   header[7];     /* Requestheader */
	                   uint8   div[3];           /* 0x0, dlen,  typ */
	                   uint8   target_dir_handle; /* Verzeichnis Handle zu „ndern */
	                   uint8   source_dir_handle; /* Verzeichnis Handle */
	                   uint8   pathlen;
	                   uint8   path[2];
	                 } *input = (struct INPUT *) (ncprequest);
	                 completition =
	                   (uint8)-nw_set_dir_handle((int)input->target_dir_handle,
	                                             (int)input->source_dir_handle,
	                                                  input->path,
	                                             (int)input->pathlen,
	                                             (int)(ncprequest->task));

	               } else  if (1 == *p){  /* liefert Verzeichnis Namen */
	                                      /* Dir_handles  */
	           /******** GetDirektoryPATH ***************/
	                 struct INPUT {
	                   uint8   header[7];      /* Requestheader */
	                   uint8   div[3];         /* 0x0, dlen,  typ */
	                   uint8   dir_handle;     /* Verzeichnis Handle */
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
	                   uint8   div[3];          /* 0x0, dlen,  typ */
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
	               } else  if (*p == 0x6){ /* Get Volume Name von 0 .. 31 */
	           /******** Get Volume Name  ***************/
	                 struct XDATA {
	                   uint8 namelen;
	                   uint8 name[16];
	                 } *xdata = (struct XDATA*) responsedata;
	                 int result = nw_get_volume_name((int)*(p+1), xdata->name);
	                 if (result > -1) {
	                   xdata->namelen = (uint8) result;
	                   data_len       = sizeof(struct XDATA);
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
	                 struct INPUT {
	                   uint8   header[7];      /* Requestheader */
	                   uint8   div[3];         /* 0x0, dlen,  typ */
	                   uint8   dir_handle;     /* Verzeichnis Handle */
	                   uint8   trustee_id[4];  /* Trustee Object ID  */
	                   uint8   trustee_right_mask;
	                   uint8   pathlen;
	                   uint8   path;
	                 } *input = (struct INPUT *) (ncprequest);
	                  /* TODO !!!!!!!!!!!!!!!!!!!!  */
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
	                  || *p == 0x16)  {   /* Allocate spez Temp Dir Handle */
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
	                 int err_code = nw_free_dir_handle((int)*(p+1));
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
                                  sector_scale*=2;
                               U16_TO_BE16(sector_scale, xdata->sectors);
                               U16_TO_BE16(fsp.fsu_blocks/sector_scale, xdata->total_blocks);
                               U16_TO_BE16(fsp.fsu_bavail/sector_scale, xdata->avail_blocks);
                               U16_TO_BE16(fsp.fsu_files,  xdata->total_dirs);
                               U16_TO_BE16(fsp.fsu_ffree,  xdata->avail_dirs);
                               if (get_volume_options(volume, 1) & VOL_OPTION_REMOUNT) {
                                 U16_TO_BE16(1,  xdata->removable);
                               } else {
                                 U16_TO_BE16(0,  xdata->removable);
                               }
                             }
	                     data_len = sizeof(struct XDATA);
	                     XDPRINTF((5,0,"GIVE VOLUME INFO von :%s:", xdata->name));
	                     result = 0;
	                   }
	                 }
	                 completition = (uint8)-result;
	               } else  if (*p == 0x19){ /* Set Directory Information */
	                 struct INPUT {
	                   uint8   header[7];      /* Requestheader */
	                   uint8   div[3];         /* 0x0, dlen,  typ */
	                   uint8   dir_handle;     /* Verzeichnis Handle */
	                   uint8   trustee_id[4];  /* Trustee Object ID  */
	                   uint8   trustee_right_mask;
	                   uint8   pathlen;
	                   uint8   path;
	                 } *input = (struct INPUT *) (ncprequest);
                         /* No REPLY */
	                 completition = 0xfb;  /* !!!!! TODO !!!! */
	               } else  if (*p == 0x1a){ /* Get Pathname of A Volume Dir Pair */
	                 struct INPUT {
	                   uint8   header[7];      /* Requestheader */
	                   uint8   div[3];         /* 0x0, dlen,  typ */
	                   uint8   volume;
	                   uint8   dir_entry[2];
	                 } *input = (struct INPUT *) (ncprequest);
	                 struct XDATA {
	                   uint8  pathlen;
	                   uint8  pathname;
	                 } *xdata = (struct XDATA*)responsedata;
	                 completition = 0xfb;  /* !!!!! TODO !!!! */
	               } else  if (*p == 0x1e){
                         /* SCAN a Directory */
	                 struct INPUT {
	                   uint8   header[7];        /* Requestheader */
	                   uint8   div[3];           /* 0x0, dlen,  typ */
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
	                   uint8   div[3];           /* 0x0, dlen,  typ    */
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
	                 uint8  volnr = *(p+1);
	                 uint32 id     = GET_BE32(p+2);
                         uint32 blocks = GET_BE32(p+6);
                         XDPRINTF((2,0,"TODO:Change vol restriction vol=%d, id=0x%lx, Blocks=0x%lx",
                                   (int)volnr, id, blocks));
	               } else  if (*p == 0x22) {
	                 /* remove Vol restrictions for Obj */
                          uint8  volnr = *(p+1);
                          uint32 id    = GET_BE32(p+2);
                          XDPRINTF((2,0, "TODO:Remove vol restriction vol=%d, id=0x%lx",
                                   (int)volnr, id));

	               } else  if (*p == 0x25){ /* setting FILE INFO ??*/
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
                             U32_TO_BE32(1, xdata->ids);  /* SUPERVISOR  */
                             xdata->trustees[1] = 0x1;    /* Supervisory */
                             xdata->trustees[0] = 0xff;   /* all low     */
                             data_len = sizeof(struct XDATA);
                           } else completition = 0x9c;  /* no more trustees */
                         } else completition = (uint8) (-result);
	               } else  if (*p == 0x27) { /* Add Trustees to DIR ?? */
	                 struct INPUT {
	                   uint8   header[7];      /* Requestheader */
	                   uint8   div[3];         /* 0x0, dlen,  typ */
	                   uint8   dir_handle;     /* Verzeichnis Handle */
	                   uint8   trustee_id[4];  /* Trustee Object ID  */
	                   uint8   trustee_right_mask;
	                   uint8   weis_nicht;    /* ???  z.B. 0x0 */
	                   uint8   pathlen;
	                   uint8   path;
	                 } *input = (struct INPUT *) (ncprequest);
	                  /* TODO !!!!!!!!!!!!!!!!!!!!  */
	                 do_druck++;
	               } else  if (*p == 0x29){
	                /* read  volume restrictions for an object */
                          uint8  volnr = *(p+1);
                          uint32 id    = GET_BE32(p+2);
	                  struct XDATA {
	                    uint8 weisnicht[8];  /* ?????? */
	                  } *xdata = (struct XDATA*) responsedata;
                          XDPRINTF((5,0, "Get vol restriction vol=%d, id=0x%lx",
                                   (int)volnr, id));
                          memset(xdata, 0, sizeof(struct XDATA));
                          data_len=sizeof(struct XDATA);
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
	                     xdata->sec_per_block = 1; /* hard coded */
	                     U32_TO_32(fsp.fsu_blocks, xdata->total_blocks);
	                     U32_TO_32(fsp.fsu_bavail, xdata->avail_blocks);
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
	                     xdata->sec_per_block = 1; /* hard coded */
	                     U32_TO_32(fsp.fsu_blocks, xdata->total_blocks);
	                     U32_TO_32(fsp.fsu_bavail, xdata->avail_blocks);
	                     U32_TO_32(fsp.fsu_files,  xdata->total_dirs);
	                     U32_TO_32(fsp.fsu_ffree,  xdata->avail_dirs);
                           }
                           xdata->namlen = strlen((char*)name);
                           strmaxcpy(xdata->name, name, xdata->namlen);
	                   data_len = xdata->namlen + 22;
                         } else completition = (uint8) -result;
	               } else  if (*p == 0x2e){  /* RENAME DATEI */
	                 completition   = 0xfb;  /* TODO: !!! */
	               } else  if (*p == 0x2f){  /* ??????    */
	                 completition   = 0xfb;  /* TODO: !!! */
	               } else completition = 0xfb;  /* unkwown request */
	             }
	             break;

	 case 0x17 : {  /* FILE SERVER ENVIRONMENT */
	   /* uint8 len   = *(requestdata+1); */
	   uint8 ufunc    = *(requestdata+2);
           uint8 *rdata   = requestdata+3;

           switch (ufunc) {

#if FUNC_17_02_IS_DEBUG
             case 0x02 :  {
                /* I hope this is call isn't used    */
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

               struct OUTPUT {
                 uint8          sequence[2];      /* next sequence */
                 /* NW_FILE_INFO f; */
                 uint8          f[sizeof(NW_FILE_INFO)];
                 uint8    	owner_id[4];
                 uint8    	archive_date[2];
                 uint8    	archive_time[2];
                 uint8    	reserved[56];
               } *xdata = (struct OUTPUT*)responsedata;
               int len = input->len;
               int searchsequence;
               NW_FILE_INFO f;
               memset(xdata, 0, sizeof(struct OUTPUT));
               searchsequence = nw_search( (uint8*) &f,
                                (int)input->dir_handle,
                                (int) GET_BE16(input->sequence),
                                (int) input->search_attrib,
                                input->data, len);
               if (searchsequence > -1) {
                 memcpy(xdata->f, &f, sizeof(NW_FILE_INFO));
                 U16_TO_BE16((uint16) searchsequence, xdata->sequence);
                 U32_TO_BE32(1L, xdata->owner_id);  /* Supervisor */
                 data_len = sizeof(struct OUTPUT);
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
             case 0x69:   /* close file and start queue old ?? */
             case 0x79:   /* create queue job and file     */
             case 0x7f:   /* close file and start queue */
             return(-2);  /* nwbind must do prehandling    */

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
                     nw_free_handles((ncprequest->task > 0) ?
                                      (int) (ncprequest->task) : 1);
                     break;

	 case 0x19 : /* logout, some of this call is handled in ncpserv. */
                     nw_free_handles(0);
                     set_default_guid();
                     nw_setup_home_vol(-1, NULL);
                     return(-1); /* nwbind must do rest */
                     break;

	 case 0x1a : /* lock file  */
	 case 0x1e : /* unlock file */
	             {
	               struct INPUT {
	                 uint8   header[7];      /* Requestheader */
	                 uint8   reserve;        /* 0x1 	  */
	                 uint8   ext_fhandle[2]; /* all zero 	  */
	                 uint8   fhandle[4];     /* Filehandle 	  */
	                 uint8   offset[4];
	                 uint8   size[4];
	                 uint8   weisnicht[2];   /* lock timeout ??? */
	               } *input = (struct INPUT *)ncprequest;
	               int fhandle  = GET_BE32(input->fhandle);
	               int offset   = GET_BE32(input->offset);
	               int size     = GET_BE32(input->size);
	               completition = (uint8)(-nw_lock_datei(fhandle,
	                                                     offset, size,
	                                       (int)(function == 0x1a)));
	             }
	             break;

	 case 0x21 : { /* Negotiate Buffer Size,  Packetsize  */
	               int   wantsize = GET_BE16((uint8*)ncprequest);
	               uint8 *getsize=responsedata;

#if IPX_DATA_GR_546
	               wantsize = min(0x400, wantsize);
#else
	               wantsize = min(0x200, wantsize);
#endif
	               U16_TO_BE16(wantsize, getsize);
	               data_len = 2;
                       XDPRINTF((5,0, "Negotiate Buffer size = 0x%04x,(%d)",
                                        (int) wantsize, (int) wantsize));
	             }
	             break;

	 case 0x22 : { /* div TTS Calls */
                       int ufunc = (int) *requestdata;
                       if (!ufunc) completition=0; /* TTS not availible */
                       else completition=0xfb;     /* request not known */
                     } break;

	 case 0x3d : {  /* commit file, flush file buffers  */
	               struct INPUT {
	                 uint8   header[7];     /* Requestheader */
	                 uint8   reserve;
	                 uint8   ext_fhandle[2]; /* all zero   */
	                 uint8   fhandle[4];     /* filehandle */
	               } *input = (struct INPUT *)ncprequest;
	               uint32 fhandle = GET_BE32(input->fhandle);
                       XDPRINTF((2,0, "TODO: COMMIT FILE:fhandle=%ld", fhandle));
                        /* TODO */
                        ;
                     } break;


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
                         uint8   dir_id[2];         /* von File Search Init */
	                 uint8   searchsequence[2]; /* FRAGE Sequence FFFF ertster Eintrag */
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

	               struct OUTPUT {
	                 uint8   searchsequence[2]; /* FRAGE Sequence  */
	                 uint8   dir_id[2];         /* Direktory ID    */
                       /*  is correct !! */
	                 union {
	                   NW_DIR_INFO  d;
	                   NW_FILE_INFO f;
	                 } u;
	               } *xdata = (struct OUTPUT*)responsedata;

	               int searchsequence = nw_dir_search(
	                                     (uint8*) &(xdata->u),
	                                     (int) GET_BE16(input->dir_id),
	                                     (int) GET_BE16(input->searchsequence),
	                                     (int) input->search_attrib,
	                                     input->data, len);
	               if (searchsequence > -1) {
	                 U16_TO_BE16((uint16) searchsequence, xdata->searchsequence);
                         memcpy(xdata->dir_id, input->dir_id, 2);
	                 data_len = sizeof(struct OUTPUT);
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
	               struct OUTPUT {
	                 uint8   sequenz[2];    /* answer sequence */
	                 uint8   reserved[2];   /* z.B  0x0   0x0  */
	                 union {
	                   NW_DIR_INFO  d;
	                   NW_FILE_INFO f;
	                 } u;
	               } *xdata = (struct OUTPUT*)responsedata;
	               int len = input->len;
	               uint8 my_sequenz[2];
	               int searchsequence;
	               memcpy(my_sequenz, input->sequenz, 2);
	               searchsequence = nw_search( (uint8*) &(xdata->u),
	                                     (int)input->dir_handle,
	                                     (int) GET_BE16(my_sequenz),
	                                     (int) input->search_attrib,
	                                     input->data, len);
	               if (searchsequence > -1) {
	                 U16_TO_BE16((uint16) searchsequence, xdata->sequenz);
	                 data_len = sizeof(struct OUTPUT);
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
	               struct OUTPUT {
	                 uint8   ext_fhandle[2]; /* all zero       */
	                 uint8   fhandle[4];     /* Dateihandle    */
	                 uint8   reserve2[2];    /* z.B  0x0   0x0 */
	                 NW_FILE_INFO fileinfo;
	               } *xdata= (struct OUTPUT*)responsedata;
	               int  fhandle=nw_creat_open_file((int)input->dirhandle,
	                       input->data, input->len,
	                       &(xdata->fileinfo),
	                       (int)input->attrib,
	                       0x1, 0);

	               if (fhandle > -1){
	                 U32_TO_BE32(fhandle, xdata->fhandle);
	                 U16_TO_BE16(0, xdata->ext_fhandle);
	                 U16_TO_BE16(0, xdata->reserve2);
	                 data_len = sizeof(struct OUTPUT);
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
	               uint32 fhandle = GET_BE32(input->fhandle);
	               completition = (uint8)(-nw_close_datei(fhandle, 0));
	               if (!completition && fhandle == test_handle) {
	                 do_druck++;
	                 test_handle = -1;
	               }
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
	               struct OUTPUT {
	                 uint8   extfhandle[2];
	                 uint8   fhandle[4];   /* Filehandle */
	                 uint8   reserved[2];  /* rese. by NOVELL */
	                 NW_FILE_INFO fileinfo;
	               } *xdata= (struct OUTPUT*)responsedata;
	               int  fhandle=nw_creat_open_file(
	                         (int)input->dirhandle,
	                              input->data,
	                              (int)input->len,
	                              &(xdata->fileinfo),
	                              (int)input->attribute,
                                      0,
                                      (function==0x43) ? 1 : 2);
	               if (fhandle > -1){
	                 data_len = sizeof(struct OUTPUT);
	                 U32_TO_BE32(fhandle, xdata->fhandle);
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

	 case 0x46 : /* chmod file ??? */
	             {
	               struct INPUT {
	                 uint8   header[7];     /* Requestheader */
	                 uint8   attrib;        /*  0x80, od 0x0 */
	                 /* 0x80 for example for sharable */
	                 uint8   dir_handle;    /*  ??? z.B.0x1 */
	                 uint8   modus;         /* z.B.0x6  */
	                 uint8   len;
	                 uint8   data[2];        /* Name */
	               } *input = (struct INPUT *)ncprequest;
	               completition =
	                 (uint8) (-nw_chmod_datei((int)input->dir_handle,
	                                   input->data, (int)input->len,
	                                   (int)input->modus));
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
	               struct OUTPUT {
	                 uint8   size[4];    /* Position ??? */
	               } *xdata=(struct OUTPUT*)responsedata;
	               int    fhandle  = GET_BE32(input->fhandle);
	               int    size     = nw_seek_datei(fhandle, 0);
	               if (size > -1) {
	                 data_len = sizeof(struct OUTPUT);
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
	               struct OUTPUT {
	                 uint8   size[2];        /* read byzes  */
	                 uint8   data[1072];     /* max data    */
	               } *xdata=(struct OUTPUT*)responsedata;
	               int    fhandle  = GET_BE32(input->fhandle);
	               int    max_size = GET_BE16(input->max_size);
	               off_t  offset   = GET_BE32(input->offset);
	               int    zusatz   = (offset & 1) ? 1 : 0;
	               int    size     = nw_read_datei(fhandle,
	                                         xdata->data+zusatz,
	                                         max_size,
	                                         offset);

	               if (fhandle == test_handle) do_druck++;
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
	               int    fhandle    = GET_BE32(input->fhandle);
	               int    input_size = GET_BE16(input->size);
	               int size          = nw_write_datei(fhandle,
	                                         input->data,
	                                         input_size,
	                                         offset);
	               if (fhandle == test_handle) do_druck++;
	               if (size < 0)
	                  completition = (uint8) -size;
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
	               int    qfhandle   = GET_BE32(input->qfhandle);
	               int    zfhandle   = GET_BE32(input->zfhandle);
	               off_t  qoffset    = GET_BE32(input->qoffset);
	               off_t  zoffset    = GET_BE32(input->zoffset);
	               uint32 input_size = GET_BE32(input->size);
	               int size          = nw_server_copy(qfhandle, qoffset,
	                                            zfhandle, zoffset,
	                                            input_size);
	               if (size < 0) completition = (uint8) -size;
	               else {
	                 struct OUTPUT {
	                   uint8   zsize[4];   /* real transfered Bytes */
	                 } *xdata= (struct OUTPUT*)responsedata;
	                 U32_TO_BE32(size, xdata->zsize);
	                 data_len = sizeof(struct OUTPUT);
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
	               int result = nw_set_fdate_time(GET_BE32(input->fhandle),
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
	               struct OUTPUT {
	                 uint8   ext_fhandle[2]; /* all zero       */
	                 uint8   fhandle[4];     /* Dateihandle    */
	                 uint8   reserve2[2];    /* z.B  0x0   0x0 */
	                 NW_FILE_INFO fileinfo;
	               } *xdata= (struct OUTPUT*)responsedata;
	               int  fhandle=nw_creat_open_file((int)input->dirhandle,
	                       input->data, input->len,
	                       &(xdata->fileinfo),
	                       (int)input->attrib,
	                       (int)input->access, 0);

	               if (fhandle > -1){
	                 U32_TO_BE32(fhandle, xdata->fhandle);
	                 U16_TO_BE16(0, xdata->ext_fhandle);
	                 U16_TO_BE16(0, xdata->reserve2);

	                 data_len = sizeof(struct OUTPUT);
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
	 case 0x57 : /* some new namespace calls */
                     {
                       int result = handle_func_0x57(requestdata, responsedata, ncprequest->task);
                       if (result > -1) data_len = result;
                       else completition=(uint8)-result;
                     }
                     break;
#endif

#ifdef _MAR_TESTS_
	 case 0x5f : { /* ????????????? UNIX Client */
	               struct INPUT {
	                 uint8   header[7];  /* Requestheader */
                         uint8   unknown[4]; /* 0x10, 0,0,0  */
	               } *input = (struct INPUT *)ncprequest;
	               completition = 0;
                     }
                     break;

#endif

#if 0
	 case 0x61 : { /* Negotiate Buffer Size, Packetsize new ??? */
                       /* > 3.11 */
                       /* similar request as 0x21 */
                     }

#endif

#if 0
	 case 0x68 :  /* NDS NCP,  NDS Fragger Protokoll ??  */
#endif

	 default : completition = 0xfb; /* unknown request */
	             break;

    } /* switch function */
  } else if (ncp_type == 0x1111) {
    (void) nw_init_connect();
    last_sequence = -9999;
  } else {
    printf("WRONG TYPE 0x%x IN NWCONN\n", ncp_type);
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
#if 0
  ncpresponse->task  = ncprequest->task;
#endif
  ncp_response(ncprequest->sequence, completition, data_len);
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
    case 0x17 : {  /* FILE SERVER ENVIRONMENT */
       uint8 ufunc    = *(requestdata+2);
       uint8 *rdata   = requestdata+3;
       switch (ufunc) {
         case 0x14:   /* Login Objekt, unencrypted passwords */
         case 0x18: { /* crypt_keyed LOGIN */
           int   fnlen = (int) *(bindresponse + 2 * sizeof(int));
           /* ncpserv have changed the structure */
           set_guid(*((int*)bindresponse), *((int*)(bindresponse+sizeof(int))));
           nw_setup_home_vol(fnlen, bindresponse + 2 * sizeof(int) +1);
         }
         break;

         case 0x68:   /* create queue job and file old */
         case 0x79: { /* create queue job and file     */
                      /* nwbind must do prehandling    */
           struct INPUT {
             uint8   header[7];          /* Requestheader   */
             uint8   packetlen[2];       /* low high        */
             uint8   func;               /* 0x79 or 0x68    */
             uint8   queue_id[4];        /* Queue ID        */
             uint8   queue_job[280];     /* oldsize is 256  */
           } *input = (struct INPUT *) (ncprequest);
           struct RINPUT {
             uint8   dir_nam_len;        /* len of dirname */
             uint8   dir_name[1];
           } *rinput = (struct RINPUT *) (bindresponse);
           int result = nw_creat_queue(ncpresponse->connection,
                                  input->queue_id,
                                  input->queue_job,
                                   rinput->dir_name,
                                   (int)rinput->dir_nam_len,
                                   (ufunc == 0x68)  );
           if (!result) {
             data_len = (ufunc == 0x68) ? 54 : 78;
             memcpy(responsedata, input->queue_job, data_len);
           } else completition= (uint8)-result;
         }
         break;

         case 0x69:    /* close file and start queue old ?? */
         case 0x7f: {  /* close file and start queue */
           struct INPUT {
             uint8   header[7];          /* Requestheader */
             uint8   packetlen[2];       /* low high      */
             uint8   func;               /* 0x7f or 0x6f  */
             uint8   queue_id[4];        /* Queue ID      */
             uint8   job_id[4];          /* result from creat queue    */
                                         /* if 0x69 then only 2 byte ! */
           } *input = (struct INPUT *) (ncprequest);
           struct RINPUT {
             uint8   prc_len;           /* len of printcommand */
             uint8   prc[1];            /* printcommand */
           } *rinput = (struct RINPUT *) (bindresponse);
           int result = nw_close_file_queue(input->queue_id,
                                            input->job_id,
                                            rinput->prc,
                                            rinput->prc_len);
           if (result < 0) completition = (uint8)-result;
         }
         break;
         default : completition = 0xfb;
       }
    }
    break;
    default : completition = 0xfb;
  } /* switch */
  ncp_response(ncprequest->sequence, completition, data_len);
}

extern int t_errno;

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
  fl_get_int=2;
}

static void sig_hup(int rsig)
{
  fl_get_int=1;
  signal(SIGHUP,   sig_hup);
}

static void get_new_debug(void)
{
  get_ini_debug(3);
  fl_get_int=0;
}

static void set_sig(void)
{
  signal(SIGTERM,  sig_quit);
  signal(SIGQUIT,  sig_quit);
  signal(SIGINT,   sig_quit);
  signal(SIGPIPE,  sig_quit);
  signal(SIGHUP,   sig_hup);
}

int main(int argc, char **argv)
{
#if CALL_NWCONN_OVER_SOCKET
  uint8      i_ipx_pack_typ;
  ipxAddr_t  x_from_addr;
  ipxAddr_t  client_addr;
  struct     t_unitdata iud;
#endif
  if (argc != 5) {
    fprintf(stderr, "usage nwconn PID FROM_ADDR Connection nwbindsock\n");
    exit(1);
  } else father_pid = atoi(*(argv+1));
  setuid(0);
  setgid(0);

  init_tools(NWCONN, atoi(*(argv+3)));
  memset(saved_readbuff, 0, sizeof(saved_readbuff));

  XDPRINTF((2, 0, "FATHER PID=%d, ADDR=%s CON:%s", father_pid, *(argv+2), *(argv+3)));

  adr_to_ipx_addr(&from_addr,   *(argv+2));

#if CALL_NWCONN_OVER_SOCKET
  adr_to_ipx_addr(&client_addr, *(argv+2));
#endif

  if (nw_init_connect()) exit(1);

  sscanf(argv[4], "%x", &sock_nwbind);

#ifdef LINUX
  set_emu_tli();
#endif
  last_sequence = -9999;
  if (get_ipx_addr(&my_addr)) exit(1);
#if CALL_NWCONN_OVER_SOCKET
# if 1
#  ifdef SIOCIPXNCPCONN
   {
     int conn   = atoi(*(argv+3));
     int result = ioctl(0, SIOCIPXNCPCONN, &conn);
     XDPRINTF((2, 0, "ioctl:SIOCIPXNCPCONN result=%d", result));
   }
#  endif
# endif
#endif

  set_default_guid();

  ud.opt.len       = sizeof(uint8);
  ud.opt.maxlen    = sizeof(uint8);
  ud.opt.buf       = (char*)&ipx_pack_typ;

  ud.addr.len      = sizeof(ipxAddr_t);
  ud.addr.maxlen   = sizeof(ipxAddr_t);
  ud.addr.buf      = (char*)&from_addr;
  ud.udata.buf     = (char*)&ipxdata;
  U16_TO_BE16(0x3333, ncpresponse->type);
  ncpresponse->task           = (uint8) 1;    /* allways 1 */
  ncpresponse->reserved       = (uint8) 0;    /* allways 0 */
  ncpresponse->connection     = (uint8) atoi(*(argv+3));

#if CALL_NWCONN_OVER_SOCKET
  iud.opt.len       = sizeof(i_ipx_pack_typ);
  iud.opt.maxlen    = sizeof(i_ipx_pack_typ);
  iud.opt.buf       = (char*)&i_ipx_pack_typ; /* gets actual Typ */

  iud.addr.len      = sizeof(ipxAddr_t);
  iud.addr.maxlen   = sizeof(ipxAddr_t);
  iud.addr.buf      = (char*)&x_from_addr;

  iud.udata.len     = IPX_MAX_DATA;
  iud.udata.maxlen  = IPX_MAX_DATA;
  iud.udata.buf     = (char*)readbuff;
#endif

  set_sig();

  while (1) {
#if CALL_NWCONN_OVER_SOCKET
    int rcv_flags = 0;
    int data_len = (t_rcvudata(0, &iud, &rcv_flags) > -1)
                            ? iud.udata.len : -1;
#else
    int data_len = read(0, readbuff, sizeof(readbuff));
#endif
    ncpresponse->connect_status = (uint8) 0;

    if (fl_get_int) {
      if (fl_get_int      == 1) get_new_debug();
      else if (fl_get_int == 2) break;
    }

    if (data_len > 0) {
      XDPRINTF((99, 0,  "NWCONN GOT DATA len = %d",data_len));
      if ((ncp_type = (int)GET_BE16(ncprequest->type)) == 0x3333) {
        data_len -= sizeof(NCPRESPONSE);
        if (saved_sequence > -1 && ((int)(ncprequest->sequence) == saved_sequence)
            && !ncprequest->function) {
          handle_after_bind();
        } else {
          /* OK for direct sending */
          XDPRINTF((6,0, "NWCONN:direct sending:type 0x3333, completition=0x%x, len=%d",
	         (int)(ncprequest->function), data_len));
          if (data_len)
            memcpy(responsedata, readbuff+sizeof(NCPRESPONSE), data_len);
          ncpresponse->connect_status = ((NCPRESPONSE*)readbuff)->connect_status;
          ncp_response((int)(ncprequest->sequence),
                       (int)(ncprequest->function), data_len);
        }
        saved_sequence = -1;
      } else { /* this calls I must handle */
        int result;
        requestlen  = data_len - sizeof(NCPREQUEST);
        if (0 != (result = handle_ncp_serv()) ) {
          if (result == -2) {  /* here the actual call must be saved */
            memcpy(saved_readbuff, readbuff, data_len);
            saved_sequence = (int)(ncprequest->sequence);
          } else saved_sequence = -1;
          /* this call must go to nwbind */
          call_nwbind();
        }
      }
    }
  } /* while */
  close_all();
  XDPRINTF((2,0, "leave nwconn pid=%d", getpid()));
  return(0);
}
