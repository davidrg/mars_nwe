/* nwdbm.h 30-Apr-96 */
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

#define PW_SCHEME_CHANGE_PW       1
#define PW_SCHEME_LOGIN		  2
#define PW_SCHEME_GET_KEY_FAIL    4
#define PW_SCHEME_ALLOW_EMPTY_PW  8

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



extern int find_obj_id(NETOBJ *o, uint32 last_obj_id);

extern int nw_delete_obj(NETOBJ *obj);
extern int nw_rename_obj(NETOBJ *o, uint8 *newname);
extern int nw_change_obj_security(NETOBJ *o, int newsecurity);
extern int nw_get_obj(NETOBJ *o);


extern int prop_find_member(uint32 obj_id, int prop_id, uint32 member_id);
extern int prop_add_member(uint32 obj_id, int prop_id, uint32 member_id);
extern int prop_delete_member(uint32 obj_id, int prop_id, uint32 member_id);

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

extern int nw_create_obj(NETOBJ *obj, uint32 wanted_id);


extern int nw_obj_has_prop(NETOBJ *obj);


extern int nw_create_prop(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int prop_flags, int prop_security);


extern uint32 nw_new_obj_prop(uint32 wanted_id,
                  char *objname, int objtype, int objflags, int objsecurity,
	          char *propname, int propflags, int propsecurity,
	          char *value, int valuesize);

extern int get_guid(int *gid, int *uid, uint32 obj_id, uint8 *name);
extern int get_home_dir(uint8 *homedir, uint32 obj_id);

extern int nw_test_passwd(uint32 obj_id, uint8 *vgl_key, uint8 *akt_key);
extern int nw_test_unenpasswd(uint32 obj_id, uint8 *password);
extern int nw_set_passwd(uint32 obj_id, char *password, int dont_ch);

extern int nw_keychange_passwd(uint32 obj_id,
                               uint8 *cryptkey,  uint8 *oldpass,
			       int   cryptedlen, uint8 *newpass,
			       uint32 act_id);

extern int nw_get_q_dirname(uint32 q_id, uint8 *buff);
extern int nw_get_q_prcommand(uint32 q_id, uint8 *buff);

extern void test_ins_unx_user(uint32 id);

extern int  nw_fill_standard(char *servername, ipxAddr_t *adr);
extern int  nw_init_dbm(char *servername, ipxAddr_t *adr);
#endif
