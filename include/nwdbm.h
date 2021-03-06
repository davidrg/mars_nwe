/* nwdbm.h 25-Apr-00 */
/* (C)opyright (C) 1993,2000  Martin Stover, Marburg, Germany
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
#ifndef _NWDBM_H_
#define _NWDBM_H_

#define NETOBJ_KEY_SIZE  4

typedef struct {
  uint32  id;           /* Objekt ID           */
  /* --------------- */
  uint8   name[48];
  uint16  type;
  uint8   flags;               /* statisch, dynamisch */
  uint8   security;
} NETOBJ;

#define NETIOBJ_KEY_SIZE 50
typedef struct {
  uint8   name[48];
  uint16  type;
  /* --------------- */
  uint32  id;           /* Objekt ID           */
} NETIOBJ;

#define NETPROP_KEY_SIZE  5
typedef struct {
  uint32  obj_id;              /* ID OBJECT   */
  uint8   id;                  /* PROPERTY id */
  /* --------------- */
  uint8   name[16];
  uint8   flags;               /* statisch, dynamisch */
  uint8   security;
} NETPROP;

#define NETVAL_KEY_SIZE  6
typedef struct {
  uint32  obj_id;              /* ID OBJECT   */
  uint8   prop_id;             /* PROPERTY id */
  uint8   segment;             /* Segment Nr  */
  /* --------------- */
  uint8   value[128];          /* Inhalt      */
} NETVAL;

/* Object Flags */
#define  O_FL_STAT    0x0
#define  O_FL_DYNA    0x1

/* Property Flags */
/* BIT 7 */
#define  P_FL_STAT    0x0
#define  P_FL_DYNA    0x1
/* BIT 6 */
#define  P_FL_ITEM    0x0
#define  P_FL_SET     0x2

extern int tells_server_version;
extern int password_scheme;

extern uint8 *sys_unixname; /* Unixname of SYS: ends with '/'  */
extern int   sys_unixnamlen;   /* len of unixname   */
extern int   sys_downshift;    /* is SYS downshift  */
extern uint8 *sys_sysname;  /* Name of first Volume, normally SYS */

extern uint32 network_serial_nmbr;
extern uint16 network_appl_nmbr;
extern int entry8_flags; 

#define PW_SCHEME_CHANGE_PW       1
#define PW_SCHEME_LOGIN		  2
#define PW_SCHEME_GET_KEY_FAIL    4
#define PW_SCHEME_ALLOW_EMPTY_PW  8

/*
**	LOGIN_CONTROL structure is an 86 byte structure that contains
**	account password information
**      Paolo Prandini <prandini@spe.it>  mst:25-Apr-00
**
*/
typedef struct {
	
  uint8	accountExpiresYear;
  uint8	accountExpiresMonth;
  uint8	accountExpiresDay;
  uint8	accountExpired;

  uint8	passwordExpiresYear;
  uint8	passwordExpiresMonth;
  uint8	passwordExpiresDay;
  uint8	passwordGraceLogins;
  uint8 expirationInterval[2]; /* hi-lo */
  uint8	graceReset;
  uint8	minimumPasswordLength;
  
  uint8 maxConcurrentConnections[2]; /* hi-lo */
  uint8	timeBitMap[42];
  uint8	lastLoginDate[6];
  uint8	restrictionFlags;  // bit 0=1 disallow pwd change, 
  			   // bit 1=1 require unique pwd
  uint8	filler;
  uint8	maxDiskBlocks[4];  /* hi-lo */
  uint8	badLoginCount[2];  /* hi-lo */
  uint8	nextResetTime[4];  /* hi-lo */
  uint8	badStationAddress[12];

} LOGIN_CONTROL;	

/*
**	USER_DEFAULTS structure is a structure that contains
**	default account password information
*/
typedef struct {

  uint8	accountExpiresYear;
  uint8	accountExpiresMonth;
  uint8	accountExpiresDay;
  uint8	restrictionFlags;
  uint8 expirationInterval[2];  /* hi-lo */
  uint8	graceReset;
  uint8	minimumPasswordLength;
  
  uint8 maxConcurrentConnections[2]; /* hi-lo */
  uint8	timeBitMap[42];
  uint8	balance[4];       /* ?? */
  uint8 creditLimit[4];   /* ?? */
  uint8	maxDiskBlocks[4]; /* hi-lo */
  uint8	createHomeDir;

} USER_DEFAULTS;


/* next routine is in nwbind.c !!!! */
extern int b_acc(uint32 obj_id, int security, int forwrite);

extern void sync_dbm(void);

extern int nw_get_prop(int object_type,
	        uint8 *object_name, int object_namlen,
	        int   segment_nr,
	        uint8 *prop_name, int prop_namlen,
	        uint8 *property_value,
	        uint8 *more_segments,
	        uint8 *property_flags);


extern int find_obj_id(NETOBJ *o);
extern int scan_for_obj(NETOBJ *o, uint32 last_obj_id, int ignore_rights);

extern int nw_delete_obj(NETOBJ *obj);
extern int nw_rename_obj(NETOBJ *o, uint8 *newname);
extern int nw_change_obj_security(NETOBJ *o, int newsecurity);
extern int nw_get_obj(NETOBJ *o);


extern int nw_get_prop_val_by_obj_id(uint32 obj_id,
                              int   segment_nr,
	        	      uint8 *prop_name, int prop_namlen,
	        	      uint8 *property_value,
	        	      uint8 *more_segments,
	        	      uint8 *property_flags);

extern int nw_get_prop_val(int object_type,
	        uint8 *object_name, int object_namlen,
	        int   segment_nr,
	        uint8 *prop_name, int prop_namlen,
	        uint8 *property_value,
	        uint8 *more_segments,
	        uint8 *property_flags);

extern int nw_delete_property(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen);

extern int nw_is_member_in_set(uint32 obj_id, char *propname, 
                uint32 member_id);

extern int nw_is_obj_in_set(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int   member_type,
	        uint8 *member_name, int member_namlen);

extern int nw_add_obj_to_set(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int   member_type,
	        uint8 *member_name, int member_namlen);

extern int nw_delete_obj_from_set(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int   member_type,
	        uint8 *member_name, int member_namlen);



extern int nw_write_prop_value(int object_type,
	        uint8 *object_name, int object_namlen,
	        int   segment_nr, int erase_segments,
	        uint8 *prop_name, int prop_namlen,
	        uint8 *property_value);


extern int nw_change_prop_security(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int   prop_security);

extern int nw_scan_property(NETPROP *prop,
                     int      object_type,
    		     uint8    *object_name,
    		     int      object_namlen,
    		     uint8    *prop_name,
    		     int      prop_namlen,
    		     uint32   *last_scan);

extern int nw_get_prop_val_str(uint32 q_id, char *propname, uint8 *buff);

extern int nw_create_obj(NETOBJ *obj, uint32 wanted_id);


extern int nw_obj_has_prop(NETOBJ *obj);


extern int nw_create_prop(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int prop_flags, int prop_security);

extern uint32 nw_new_obj_prop(uint32 wanted_id,
                  char *objname, int objtype, int objflags, int objsecurity,
	          char *propname, int propflags, int propsecurity,
	          char *value, int valuesize, int ever);

extern int nw_is_security_equal(uint32 id1,  uint32 id2);
#define HAVE_SU_RIGHTS(id) ( ((id)==1L) || !nw_is_security_equal(1L, (id)))
extern int get_groups_i_m_in(uint32 id, uint32 *gids);

extern int get_guid(int *gid, int *uid, uint32 obj_id, uint8 *name);

/* mst:25-Apr-00 */
extern int nw_get_login_control(uint32 obj_id, LOGIN_CONTROL *plc);
extern int nw_set_login_control(uint32 obj_id, LOGIN_CONTROL *plc);

extern int nw_test_passwd(uint32 obj_id, uint8 *vgl_key, uint8 *akt_key);
extern int nw_test_unenpasswd(uint32 obj_id, uint8 *password);
extern int nw_set_passwd(uint32 obj_id, char *password, int dont_ch);

extern int nw_keychange_passwd(uint32 obj_id,
                               uint8 *cryptkey,  uint8 *oldpass,
			       int   cryptedlen, uint8 *newpass,
			       int   id_flags);

extern int nw_test_adr_time_access(uint32 obj_id, ipxAddr_t *client_adr);


extern int nwdbm_mkdir(char *unixname, int mode, int flags);
extern int nwdbm_rmdir(char *path);
extern void test_ins_unx_user(uint32 id);
extern int  test_allow_password_change(uint32 id);

extern int nw_fill_standard(char *servername, ipxAddr_t *adr);
extern int nw_init_dbm(char *servername, ipxAddr_t *adr);
extern void nw_exit_dbm(void);

extern int do_export_dbm(char *path);
extern int do_import_dbm(char *path);
extern int do_export_dbm_to_dir(void);


#endif
