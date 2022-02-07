/* nwdbm.c  22-Feb-96  data base for mars_nwe */
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

/*
 * This code is only called from the process 'ncpserv'
 * So, there is no need for locking or something else.
 */

#include "net.h"
#include "nwdbm.h"
#include "nwcrypt.h"
#ifdef LINUX
#  include <ndbm.h>
#  define SHADOW_PWD  0
#else
#  include </usr/ucbinclude/ndbm.h>
#  define SHADOW_PWD  1
#endif

#if SHADOW_PWD
#  include <shadow.h>
#endif

#define DBM_REMAINS_OPEN  1

int        tells_server_version=0;
int        password_scheme=0;

static datum key;
static datum data;
static DBM   *my_dbm=NULL;

#define FNPROP  0
#define FNVAL   1
#define FNOBJ   2

static char  *dbm_fn[3]  = { "nwprop", "nwval", "nwobj" };

#if DBM_REMAINS_OPEN
static DBM   *my_dbms[3] = { NULL, NULL, NULL };
#endif

static int x_dbminit(char *s)
{
  char buff[256];
  sprintf(buff, "%s/%s", PATHNAME_BINDERY, s);
  my_dbm = dbm_open(buff, O_RDWR|O_CREAT, 0666);
  return( (my_dbm == NULL) ? -1 : 0);
}

static int dbminit(int what_dbm)
{
#if DBM_REMAINS_OPEN
  int result = 0;
  if (NULL == my_dbms[what_dbm]) {
    result = x_dbminit(dbm_fn[what_dbm]);
    if (!result)  my_dbms[what_dbm] = my_dbm;
  } else my_dbm = my_dbms[what_dbm];
  return(result);
#else
  return(x_dbminit(dbm_fn[what_dbm]));
#endif
}

static int dbmclose()
{
  if (my_dbm != NULL) {
#if !DBM_REMAINS_OPEN
    dbm_close(my_dbm);
#endif
    my_dbm = NULL;
  }
  return(0);
}

void sync_dbm()
{
#if DBM_REMAINS_OPEN
  int k = 3;
  while (k--) {
    if (NULL != my_dbms[k]) {
      dbm_close(my_dbms[k]);
      my_dbms[k] = NULL;
    }
  }
#endif
}


#define firstkey()          dbm_firstkey(my_dbm)
#define nextkey(key)        dbm_nextkey(my_dbm)
#define delete(key)         dbm_delete(my_dbm, key)
#define fetch(key)          dbm_fetch(my_dbm,  key)
#define store(key, content) dbm_store(my_dbm,  key, content, DBM_REPLACE)


static int name_match(uint8 *s, uint8 *p)
{
  uint8   pc;
  while ( (pc = *p++) != 0){
    switch  (pc) {

      case '?' : if (!*s++) return(0);    /* simple char */
	         break;

      case '*' : if (!*p) return(1);      /* last star    */
	         while (*s) {
	           if (name_match(s, p) == 1) return(1);
	           ++s;
	         }
	         return(0);

      default : if (pc != *s++) return(0); /* normal char */
	        break;
    } /* switch */
  } /* while */
  return ( (*s) ? 0 : 1);
}

int find_obj_id(NETOBJ *o, uint32 last_obj_id)
{
  int result = -0xfc; /* no Object */
  XDPRINTF((2, 0,"findobj_id OBJ=%s, type=0x%x, lastid=0x%lx",
	      o->name, (int)o->type, last_obj_id));

  if (!dbminit(FNOBJ)){

    key = firstkey();
    if (last_obj_id && (last_obj_id != MAX_U32)){
      int  flag = 0;
      while (key.dptr != NULL && !flag) {
	if ( ((NETOBJ*)(key.dptr))->id == last_obj_id) flag++;
	key = nextkey(key);
      }
    }

    while (key.dptr != NULL && result) {
      data = fetch(key);
      if (data.dptr != NULL){
	NETOBJ *obj = (NETOBJ*)data.dptr;
	if ( ( ((int)obj->type == (int)o->type) || o->type == MAX_U16) &&
	   name_match(obj->name, o->name))  {
	  XDPRINTF((2, 0, "found OBJ=%s, id=0x%lx", obj->name, obj->id));
	  result = 0;
	  memcpy((char *)o, (char*)obj, sizeof(NETOBJ));
	} else {
	  XDPRINTF((3,0,"not found,but NAME=%s, type=0x%x, id=0x%lx",
	                 obj->name, (int)obj->type, obj->id));
	}
      }
      if (result) key = nextkey(key);
    } /* while */

  } else result = -0xff;
  dbmclose();
  return(result);
}

static int loc_delete_property(uint32 obj_id, uint8 *prop_name, uint8 prop_id)
/* deletes Object property or properties */
/* wildcards allowed in property name  */
{
  uint8 xset[256];
  int result = -0xfb; /* no property */
  memset(xset, 0, sizeof(xset));
  if (!prop_id) {
    XDPRINTF((2,0, "loc_delete_property obj_id=%d, prop=%s", obj_id, prop_name));
    if (!dbminit(FNPROP)){
      for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
	NETPROP *p=(NETPROP*)key.dptr;
	if (p->obj_id == obj_id) {
	  data = fetch(key);
	  p = (NETPROP*)data.dptr;
	  if (p != NULL && name_match(p->name, prop_name)){
	    XDPRINTF((2,0, "found prop: %s, id=%d for deleting", p->name, (int)p->id));
	    if ((int)(p->id) > result) result = (int)(p->id);
	    xset[p->id]++;
	  }
	}
      } /* for */
    } else result = -0xff;
    dbmclose();
  } else {
    XDPRINTF((2,0, "loc_delete_property obj_id=%d, prop_id=%d", obj_id, (int)prop_id));
    xset[prop_id]++;
    result = prop_id;
  }
  if (result > 0) {
    if (!dbminit(FNVAL)){
      int k;
      NETVAL val;
      key.dptr   = (char*)&val;
      key.dsize  = NETVAL_KEY_SIZE;
      val.obj_id = obj_id;
      for (k = 1; k <= result; k++){
	if (xset[k]){
	  int l   = 0;
	  val.prop_id  = (uint8)k;
	  while (l++ < 255) {
	    val.segment = (uint8)l;
	    if (delete(key)) break;
	  }
	}
      } /* for */
    } else result=-0xff;
    dbmclose();
    if (result > 0) {
      if (!dbminit(FNPROP)){  /* now delete properties */
	int k;
	NETPROP prop;
	key.dptr   = (char*)&prop;
	key.dsize  = NETPROP_KEY_SIZE;
	prop.obj_id = obj_id;
	for (k = (prop_id) ? prop_id : 1; k <= result; k++){
	  if (xset[k]){
	    prop.id  = (uint8)k;
	    if (delete(key)) {
	      result = -0xf6;
	      break;
	    }
	  }
	} /* for */
	if (result > 0) result=0;
      } else result=-0xff;
      dbmclose();
    }
  }
  return(result);
}

static int loc_delete_obj(uint32 objid)
{
  int result=0;
  (void)loc_delete_property(objid, (uint8*)"*", 0);
  if (!dbminit(FNOBJ)){
    key.dptr  = (char*)&objid;
    key.dsize = NETOBJ_KEY_SIZE;
    if (delete(key)) result = -0xff;
  } else result = -0xff;
  dbmclose();
  return(result);
}

int nw_delete_obj(NETOBJ *obj)
{
  int result = find_obj_id(obj, 0);
  XDPRINTF((2,0, "nw_delete_obj obj_id=%d, obj_name=%s", obj->id, obj->name));
  if (!result) result=loc_delete_obj(obj->id);
  return(result);
}

int nw_rename_obj(NETOBJ *o, uint8 *newname)
/* rename object */
{
  int result = find_obj_id(o, 0);
  if (!result) {
    result = -0xff;
    if (!dbminit(FNOBJ)){
      key.dsize = NETOBJ_KEY_SIZE;
      key.dptr  = (char*)o;
      data      = fetch(key);
      if (data.dptr != NULL){
        NETOBJ *obj=(NETOBJ*)data.dptr;
        XDPRINTF((2,0, "rename_obj:got OBJ name=%s, id = 0x%x", obj->name, (int)obj->id));
        strncpy(obj->name, newname, 48);
        if (!store(key, data)) result=0;
      }
    }
    dbmclose();
  }
  return(result);
}

int nw_change_obj_security(NETOBJ *o, int newsecurity)
/* change Security of Object */
{
  int result = find_obj_id(o, 0);
  if (!result) {
    result = -0xff;
    if (!dbminit(FNOBJ)){
      key.dsize = NETOBJ_KEY_SIZE;
      key.dptr  = (char*)o;
      data      = fetch(key);
      if (data.dptr != NULL){
        NETOBJ *obj=(NETOBJ*)data.dptr;
        XDPRINTF((2,0, "change_obj_security:got OBJ name=%s, id = 0x%x", obj->name, (int)obj->id));
        obj->security = (uint8) newsecurity;
        if (!store(key, data)) result=0;
      }
    }
    dbmclose();
  }
  return(result);
}

int nw_get_obj(NETOBJ *o)
{
  int result = -0xfc; /* no Object */
  XDPRINTF((2,0, "nw_get_obj von OBJ id = 0x%x", (int)o->id));
  if (!dbminit(FNOBJ)){
    key.dsize = NETOBJ_KEY_SIZE;
    key.dptr  = (char*)o;
    data      = fetch(key);
    if (data.dptr != NULL){
      NETOBJ *obj=(NETOBJ*)data.dptr;
      XDPRINTF((2,0, "got OBJ name=%s, id = 0x%x", obj->name, (int)obj->id));
      memcpy(o, data.dptr, sizeof(NETOBJ));
      result = 0;
    }
  } else result = -0xff;
  dbmclose();
  return(result);
}

static int find_prop_id(NETPROP *p, uint32 obj_id, int last_prop_id)
{
  int result = -0xfb; /* no Property */
  XDPRINTF((2,0, "find Prop id von name=0x%x:%s, lastid=%d",
           obj_id, p->name, last_prop_id));
  if (!dbminit(FNPROP)){
    int  flag = (last_prop_id) ? 0 : 1;
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      NETPROP *prop=(NETPROP*)key.dptr;
      if (prop->obj_id == obj_id) {
        if (!flag) flag = (last_prop_id == prop->id);
        else {
	  data = fetch(key);
	  prop = (NETPROP*)data.dptr;
	  if (data.dptr != NULL  && name_match(prop->name, p->name) )  {
	    XDPRINTF((2,0, "found PROP %s, id=0x%x", prop->name, (int) prop->id));
	    result = 0;
	    memcpy(p, prop, sizeof(NETPROP));
	    break;
	  }
        }
      }
    }
  } else result = -0xff;
  dbmclose();
  return(result);
}

#define find_first_prop_id(p, obj_id)  \
        find_prop_id((p), (obj_id), 0)


static int loc_change_prop_security(NETPROP *p, uint32 obj_id)
{
  int result = -0xfb; /* no Property */
  XDPRINTF((2,0, "loc_change_prop_security Prop id von name=0x%x:%s", obj_id, p->name));
  if (!dbminit(FNPROP)){
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      NETPROP *prop=(NETPROP*)key.dptr;
      if (prop->obj_id == obj_id) {
	data = fetch(key);
	prop = (NETPROP*)data.dptr;
	if (data.dptr != NULL  && name_match(prop->name, p->name) )  {
	  uint8 security = p->security;
	  XDPRINTF((2,0, "found PROP %s, id=0x%x", prop->name, (int) prop->id));
	  result = 0;
	  memcpy(p, prop, sizeof(NETPROP));
	  p->security = security;
	  data.dptr  = (char*)p;
	  data.dsize = sizeof(NETPROP);
	  key.dptr  = (char *)p;
	  key.dsize = NETPROP_KEY_SIZE;
	  if (store(key, data)) result=-0xff;
	  break;
	}
      }
    }
  } else result = -0xff;
  dbmclose();
  return(result);
}


static int loc_get_prop_val(uint32 obj_id, int prop_id, int segment,
	         uint8 *property_value, uint8 *more_segments)
{
  int result = -0xec; /* no such Segment */
  NETVAL  val;
  if (!dbminit(FNVAL)){
    key.dsize   = NETVAL_KEY_SIZE;
    key.dptr    = (char*)&val;
    val.obj_id  = obj_id;
    val.prop_id = (uint8)prop_id;
    val.segment = (uint8)segment;
    data        = fetch(key);
    if (data.dptr != NULL){
      NETVAL  *v = (NETVAL*)data.dptr;
      if (NULL != property_value) memcpy(property_value, v->value, 128);
      XDPRINTF((2,0, "found VAL 0x%x, %d, %d", obj_id, prop_id, segment));
      result = 0;
      val.segment++;
      data  = fetch(key);
      if (NULL != more_segments)
        *more_segments = (data.dptr != NULL) ? 0xff : 0;
    }
  } else result = -0xff;
  dbmclose();
  return(result);
}


int prop_find_member(uint32 obj_id, int prop_id, uint32 member_id)
{
  int result = -0xea; /* no such member */
  NETVAL  val;
  if (!dbminit(FNVAL)){
    key.dsize   = NETVAL_KEY_SIZE;
    key.dptr    = (char*)&val;
    val.obj_id  = obj_id;
    val.prop_id = (uint8)prop_id;
    val.segment = (uint8)1;
    data        = fetch(key);
    if (data.dptr != NULL){
      NETVAL  *v = (NETVAL*)data.dptr;
      uint8   *p=v->value;
      int     k=0;
      XDPRINTF((2,0, "found VAL 0x%x, %d", obj_id, prop_id));
      while (k++ < 32){
	uint32 id = GET_BE32(p);
	if (id == member_id) {
	  result = 0;
	  break;
	} else p += 4;
      }
    }
  } else result = -0xff;
  dbmclose();
  return(result);
}

int prop_add_member(uint32 obj_id, int prop_id, uint32 member_id)
{
  int result = 0; /* OK */
  NETVAL  val;
  if (!dbminit(FNVAL)){
    key.dsize   = NETVAL_KEY_SIZE;
    key.dptr    = (char*)&val;
    val.obj_id  = obj_id;
    val.prop_id = (uint8)prop_id;
    val.segment = (uint8)0;
    while (!result) {
      val.segment++;
      data        = fetch(key);
      if (data.dptr != NULL){
	NETVAL  *v = (NETVAL*)data.dptr;
	uint8   *p = v->value;
	int      k = 0;
	while (k++ < 32){
	  uint32 null_id = 0;
	  if (!memcmp(p, (char*)&null_id, 4)) {
	    U32_TO_BE32(member_id, p);
	    memcpy(&val, v, sizeof(NETVAL));
	    data.dptr = (char*)&val;
	    key.dptr  = (char*)&val;
	    if (store(key, data)) result=-0xff;
	    goto L1;
	  } else p += 4;
	}
      } else {
	memset(val.value, 0, 128);
	U32_TO_BE32(member_id, val.value);
	data.dptr  = (char*)&val;
	data.dsize = sizeof(NETVAL);
	if (store(key, data)) result=-0xff;
	goto L1;
      }
    } /* while */
  } else result = -0xff;
L1:
  dbmclose();
  return(result);
}

int prop_delete_member(uint32 obj_id, int prop_id, uint32 member_id)
{
  int result = -0xea; /* no such member */
  NETVAL  val;
  if (!dbminit(FNVAL)){
    key.dsize   = NETVAL_KEY_SIZE;
    key.dptr    = (char*)&val;
    val.obj_id  = obj_id;
    val.prop_id = (uint8)prop_id;
    val.segment = (uint8)0;
    data        = fetch(key);
    while (result) {
      val.segment++;
      data        = fetch(key);
      if (data.dptr != NULL) {
	NETVAL  *v = (NETVAL*)data.dptr;
	uint8   *p = v->value;
	int      k = 0;
	while (k++ < 32){
	  if (GET_BE32(p) == member_id) {
	    memset(p, 0, 4);
	    memcpy(&val, v, sizeof(NETVAL));
	    data.dptr = (char*)&val;
	    if (store(key, data)) result=-0xff;
	    else result=0;
	    goto L1;
	  } else p += 4;
	}
      } else break;
    }
  } else result = -0xff;
L1:
  dbmclose();
  return(result);
}

int ins_prop_val(uint32 obj_id, uint8 prop_id, int segment,
	         uint8 *property_value, int erase_segments)
{
  int result = -0xec; /* no such Segment */
  if (!dbminit(FNVAL)){
    NETVAL  val;
    int flag    = 1;
    key.dsize   = NETVAL_KEY_SIZE;
    key.dptr    = (char*)&val;
    val.obj_id  = obj_id;
    val.prop_id = (uint8)prop_id;
    if (segment > 1) {
      val.segment = segment-1;
      data        = fetch(key);
      if (data.dptr == NULL) flag = 0;
    }
    if (flag){
      val.segment = segment;
      memcpy(val.value, property_value, 128);
      data.dsize = sizeof(NETVAL);
      data.dptr  = (char*)&val;
      if (!store(key, data)) {
	result = 0;
	if (erase_segments == 0xff){
	  val.segment++;
	  while (!delete(key)) val.segment++;
	}
      }
    }
  } else result = -0xff;
  dbmclose();
  return(result);
}

int nw_get_prop_val_by_obj_id(uint32 obj_id,
                              int segment_nr,
	        	      uint8 *prop_name, int prop_namlen,
	        	      uint8 *property_value,
	        	      uint8 *more_segments,
	        	      uint8 *property_flags)
{
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)prop.name, (char*)prop_name, prop_namlen);
  XDPRINTF((2,0, "nw_get_prop_val_by_obj_id,id=0x%x, prop=%s, segment=%d",
            obj_id, prop.name, segment_nr));

  if ((result=find_first_prop_id(&prop, obj_id))==0){
    if ((result=loc_get_prop_val(obj_id, prop.id, segment_nr,
	          property_value, more_segments)) == 0){
      *property_flags = prop.flags;
    }
  }
  return(result);
}

int nw_get_prop_val(int object_type,
	        uint8 *object_name, int object_namlen,
	        int   segment_nr,
	        uint8 *prop_name, int prop_namlen,
	        uint8 *property_value,
	        uint8 *more_segments,
	        uint8 *property_flags)
{
  NETOBJ obj;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  obj.type    = (uint16) object_type;
  if ((result = find_obj_id(&obj, 0)) == 0){
    result = nw_get_prop_val_by_obj_id(obj.id,
                              segment_nr,
	        	      prop_name, prop_namlen,
	        	      property_value,
	        	      more_segments,
	        	      property_flags);
  }
  return(result);
}

int nw_delete_property(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name,   int prop_namlen)
{
  NETOBJ  obj;
  uint8   prop_name_x[20];
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)prop_name_x, (char*)prop_name, prop_namlen);
  XDPRINTF((2,0, "nw_delete_property obj=%s, prop=%s, type=0x%x",
      obj.name, prop_name_x, object_type));
  obj.type    = (uint16) object_type;
  if ((result = find_obj_id(&obj, 0)) == 0){
    result = loc_delete_property(obj.id, prop_name_x, 0);
  }
  return(result);
}

int nw_is_obj_in_set(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int   member_type,
	        uint8 *member_name, int member_namlen)
{
  NETOBJ  obj;
  NETOBJ  mobj;
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)mobj.name,  (char*)member_name, member_namlen);
  strmaxcpy((char*)prop.name, (char*)prop_name,   prop_namlen);
  XDPRINTF((2,0, "nw_is_obj_in_set obj=%s,0x%x, member=%s,0x%x, prop=%s",
      obj.name, object_type, mobj.name, member_type, prop.name));
  obj.type    = (uint16) object_type;
  mobj.type   = (uint16) member_type;
  if ((result = find_obj_id(&obj, 0)) == 0){
    result=find_first_prop_id(&prop, obj.id);
    if (!result)
      result = find_obj_id(&mobj, 0);
    if (!result)
      result = prop_find_member(obj.id, (int)prop.id,  mobj.id);
  }
  return(result);
}

int nw_add_obj_to_set(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int   member_type,
	        uint8 *member_name, int member_namlen)
{
  NETOBJ  obj;
  NETOBJ  mobj;
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)mobj.name,  (char*)member_name, member_namlen);
  strmaxcpy((char*)prop.name, (char*)prop_name,   prop_namlen);
  XDPRINTF((2,0, "nw_add_obj_to_set obj=%s,0x%x, member=%s,0x%x, prop=%s",
      obj.name, object_type, mobj.name, member_type, prop.name));
  obj.type    = (uint16) object_type;
  mobj.type   = (uint16) member_type;
  if ((result = find_obj_id(&obj, 0)) == 0){
    result=find_first_prop_id(&prop, obj.id);
    if (!result)
      result = find_obj_id(&mobj, 0);
    if (!result)
      result = prop_add_member(obj.id, (int)prop.id,  mobj.id);
  }
  return(result);
}

int nw_delete_obj_from_set(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int   member_type,
	        uint8 *member_name, int member_namlen)
{
  NETOBJ  obj;
  NETOBJ  mobj;
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)mobj.name,  (char*)member_name, member_namlen);
  strmaxcpy((char*)prop.name, (char*)prop_name,   prop_namlen);
  XDPRINTF((2,0, "nw_delete_obj_from_set obj=%s,0x%x, member=%s,0x%x, prop=%s",
      obj.name, object_type, mobj.name, member_type, prop.name));
  obj.type    = (uint16) object_type;
  mobj.type   = (uint16) member_type;
  if ((result = find_obj_id(&obj, 0)) == 0){
    result=find_first_prop_id(&prop, obj.id);
    if (!result)
      result = find_obj_id(&mobj, 0);
    if (!result)
      result = prop_delete_member(obj.id, (int)prop.id,  mobj.id);
  }
  return(result);
}


int nw_write_prop_value(int object_type,
	        uint8 *object_name, int object_namlen,
	        int   segment_nr, int erase_segments,
	        uint8 *prop_name, int prop_namlen,
	        uint8 *property_value)
{
  NETOBJ  obj;
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)prop.name, (char*)prop_name,   prop_namlen);
  XDPRINTF((2,0, "nw_write_prop_value obj=%s, prop=%s, type=0x%x, segment=%d",
      obj.name, prop.name, object_type, segment_nr));
  obj.type    = (uint16) object_type;

  if ((result = find_obj_id(&obj, 0)) == 0){
    if ((result=find_first_prop_id(&prop, obj.id))==0){
       result=ins_prop_val(obj.id, prop.id, segment_nr,
	    property_value, erase_segments);

    }
  }
  return(result);
}


int nw_change_prop_security(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int   prop_security)
{
  NETOBJ  obj;
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)prop.name, (char*)prop_name,   prop_namlen);
  prop.security = (uint8)prop_security;
  XDPRINTF((2,0, "nw_change_prop_security obj=%s,0x%x, prop=%s",
      obj.name, object_type, prop.name));
  obj.type    = (uint16) object_type;
  if ((result = find_obj_id(&obj, 0)) == 0){
    result=loc_change_prop_security(&prop, obj.id);
  }
  return(result);
}

int nw_scan_property(NETPROP *prop,
                     int      object_type,
    		     uint8    *object_name,
    		     int      object_namlen,
    		     uint8    *prop_name,
    		     int      prop_namlen,
    		     uint32   *last_scan)
{
  NETOBJ  obj;
  int     result;
  strmaxcpy((char*)obj.name,   (char*)object_name, object_namlen);
  strmaxcpy((char*)prop->name, (char*)prop_name,   prop_namlen);
  XDPRINTF((2,0, "nw_scan_property obj=%s, prop=%s, type=0x%x, last_scan=0x%lx",
      obj.name, prop->name, object_type, *last_scan));
  obj.type    = (uint16) object_type;

  if ((result = find_obj_id(&obj, 0)) == 0){
    int last_prop_id;
    if (*last_scan == MAX_U32) *last_scan = 0;
    last_prop_id =  *last_scan;
    if ((result=find_prop_id(prop, obj.id, last_prop_id))==0){
      *last_scan = prop->id;
      if (!loc_get_prop_val(obj.id, prop->id,  1,
	                   NULL, NULL))
        result = 0xff; /* Has prop Values */
    }
  }
  return(result);
}

int nw_get_prop_val_str(uint32 q_id, char *propname, uint8 *buff)
/* for simple prop val strings */
{
  uint8  more_segments;
  uint8  property_flags;
  uint8  loc_buff[200];
  int    result;
  if (NULL == buff) buff=loc_buff;
  result=nw_get_prop_val_by_obj_id(q_id, 1, propname, strlen(propname),
                  buff, &more_segments, &property_flags);

  if (result > -1) {
    result=strlen(buff);
    XDPRINTF((2,0, "nw_get_prop_val_str:%s strlen=%d", propname, result));
  } else
    XDPRINTF((2,0, "nw_get_prop_val_str:%s, result=0x%x", propname, result));
  return(result);
}

int nw_create_obj(NETOBJ *obj, uint32 wanted_id)
/*
 * Routine creates object
 * wants OBJ name and OBJ-Type,  returns  obj.id.
 * if wanted_id > 0 and object don't exist then
 * wanted_id should be taken as obj_id.
*/
{
  int result = 0; /* OK */
  XDPRINTF((2,0, "creat OBJ=%s,type=0x%x", obj->name, (int)obj->type));
  if (!dbminit(FNOBJ)){
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      data = fetch(key);
      if (data.dptr){
	NETOBJ *o=(NETOBJ*)data.dptr;
	if (o->type == obj->type && !strcmp(o->name, obj->name)){
	  obj->id =  o->id; /* fill ID       */
	  result  = -0xee;  /* already exist */
	  break;
	}
      }
    }
    if (!result){
      obj->id    = (wanted_id) ? wanted_id -1 : (obj->type << 16) + 1;
         /* 1 is reserved for supervisor !!!! */
      key.dsize  = NETOBJ_KEY_SIZE;
      key.dptr   = (char*)obj;
      while(1) {
	obj->id++;
	data = fetch(key);
	if (data.dptr == NULL) break;
      }
      data.dsize = sizeof(NETOBJ);
      data.dptr = (char*)obj;
      if (store(key, data)) result = -0xff;
    }
  } else result = -0xff;
  dbmclose();
  return(result);
}

int nw_obj_has_prop(NETOBJ *obj)
{
  int result = (dbminit(FNPROP)) ? -0xff : 0;
  if (!result){
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      NETPROP *p=(NETPROP*)key.dptr;
      if (p->obj_id == obj->id) {
	result = 1;
	break;
      }
    }
  }
  dbmclose();
  return(result);
}


int nw_create_obj_prop(NETOBJ *obj, NETPROP *prop)
{
  int result=0;
  if (!dbminit(FNPROP)){
    uint8   founds[256];
    memset((char*)founds, 0, sizeof(founds) );
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      NETPROP *p=(NETPROP*)key.dptr;
      if (p->obj_id == obj->id) {
	data = fetch(key);
	p  = (NETPROP*)data.dptr;
	if (data.dptr != NULL  && !strcmp(prop->name, p->name)){
	  prop->id     = p->id;
	  prop->obj_id = obj->id;
	  result   = -0xed;  /* Property exists */
	  break;
	} else founds[p->id]++;
      }
    }
    if (!result){
      int k = 0;
      while (++k < sizeof(founds) ) {
	if (!founds[k]) break;     /* free slot */
      }
      key.dsize     = NETPROP_KEY_SIZE;
      key.dptr      = (char *)prop;
      data.dsize    = sizeof(NETPROP);
      data.dptr     = (char *)prop;
      prop->obj_id   = obj->id;
      prop->id       = (uint8)k;
      if (store(key, data)) result = -0xff;
    }
  } else result = -0xff;
  dbmclose();
  return(result);
}

int nw_create_prop(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int prop_flags, int prop_security)
/* creats property for an object */
{
  NETOBJ  obj;
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)prop.name, (char*)prop_name,   prop_namlen);
  XDPRINTF((2,0, "nw_create_prop obj=%s, prop=%s, type=0x%x",
      obj.name, prop.name, object_type));
  obj.type    = (uint16) object_type;
  if ((result = find_obj_id(&obj, 0)) == 0){
    prop.flags    = (uint8)prop_flags;
    prop.security = (uint8)prop_security;
    result = nw_create_obj_prop(&obj, &prop);
  }
  return(result);
}

static int nw_new_obj(uint32 *wanted_id,
                      char *objname, int objtype,
           	      int objflags, int objsecurity)
{
  NETOBJ obj;
  int result;
  xstrcpy(obj.name, objname);
  obj.type      =  (uint16) objtype;
  obj.flags     =  (uint8)  objflags;
  obj.security  =  (uint8)  objsecurity;
  obj.id        =  0L;
  result = nw_create_obj(&obj, *wanted_id);
  *wanted_id = obj.id;
  return(result);
}

uint32 nw_new_obj_prop(uint32 wanted_id,
                  char *objname, int objtype, int objflags, int objsecurity,
	          char *propname, int propflags, int propsecurity,
	          char *value, int valuesize)
/*
 * creats new property value, if needed creats Object
 * and the property,
 * if propname == NULL only object will be created.
 * if valuesize == 0, then only obj or property
 * will be created, returns obj-id
 */
{
  NETOBJ  obj;
  NETPROP prop;
  if (objname && *objname)
    nw_new_obj(&wanted_id, objname, objtype,
                         objflags, objsecurity);
  obj.id = wanted_id;
  if (propname && *propname) {
    strmaxcpy(prop.name, propname, sizeof(prop.name));
    prop.flags    =   (uint8)propflags;
    prop.security =   (uint8)propsecurity;
    nw_create_obj_prop(&obj, &prop);
    if (valuesize){
      uint8  locvalue[128];
      memset(locvalue, 0, sizeof(locvalue));
      memcpy(locvalue, value, min(sizeof(locvalue), valuesize));
      ins_prop_val(obj.id, prop.id, 1, locvalue, 0xff);
    }
  }
  return(obj.id);
}

typedef struct {
  int  pw_uid;
  int  pw_gid;
  char pw_passwd[80];
} MYPASSWD;

static MYPASSWD *nw_getpwnam(uint32 obj_id)
{
  static MYPASSWD pwstat;
  char buff[200];
  if (nw_get_prop_val_str(obj_id, "UNIX_USER", buff) > 0){
    struct passwd *pw = getpwnam(buff);
    if (NULL != pw) {
      memcpy(&pwstat, pw, sizeof(struct passwd));
      pwstat.pw_uid = pw->pw_uid;
      pwstat.pw_gid = pw->pw_gid;
      xstrcpy(pwstat.pw_passwd, pw->pw_passwd);
#if SHADOW_PWD
      if (pwstat.pw_passwd[0] == 'x' && pwstat.pw_passwd[1]=='\0') {
        struct spwd *spw=getspnam(buff);
        if (spw) xstrcpy(pwstat.pw_passwd, spw->sp_pwdp);
      }
#endif
      XDPRINTF((2,0, "FOUND obj_id=0x%x, pwnam=%s, gid=%d, uid=%d",
                 obj_id, buff, pw->pw_gid, pw->pw_uid));
      return(&pwstat);
    }
  }
  XDPRINTF((2,0, "NOT FOUND PWNAM of obj_id=0x%x",  obj_id));
  return(NULL);
}

int get_guid(int *gid, int *uid, uint32 obj_id)
/* searched for gid und uid of actual obj */
{
  MYPASSWD *pw = nw_getpwnam(obj_id);
  if (NULL != pw) {
    *gid    = pw->pw_gid;
    *uid    = pw->pw_uid;
    return(0);
  } else {
    *gid = -1;
    *uid = -1;
    return(-0xff);
  }
}

static int crypt_pw_ok(uint8 *password, char *passwd)
/* returns 0 if not ok */
{
  char pnul[2] = {'\0', '\0'};
  char *pp    = (password) ? (char*)password : pnul;
  char *p     =  crypt(pp, passwd);
  return( (strcmp(p, passwd)) ? 0 : 1 );
}

int nw_test_passwd(uint32 obj_id, uint8 *vgl_key, uint8 *akt_key)
/* returns 0, if password ok and -0xff if not ok */
{
  char buf[200];
  if (nw_get_prop_val_str(obj_id, "PASSWORD", buf) > 0) {
    uint8 keybuff[8];
    memcpy(keybuff, vgl_key, sizeof(keybuff));
    nw_encrypt(keybuff, buf, keybuff);
    return (memcmp(akt_key, keybuff, sizeof(keybuff)) ? -0xff : 0);
  } else {
    if (password_scheme & PW_SCHEME_LOGIN) {
      MYPASSWD *pw = nw_getpwnam(obj_id);
      if (pw && *(pw->pw_passwd) && !crypt_pw_ok(NULL, pw->pw_passwd))
        return(-0xff);
      if (obj_id == 1) return(-0xff);
    }
    return(0); /* no password */
  }
}

int nw_test_unenpasswd(uint32 obj_id, uint8 *password)
{
  uint8 passwordu[100];
  uint8 passwd[200];
  uint8 stored_passwd[200];
  MYPASSWD *pw;
  if (password && *password
     && nw_get_prop_val_str(obj_id, "PASSWORD", stored_passwd) > 0 ) {
    uint8 s_uid[4];
    U32_TO_BE32(obj_id, s_uid);
    xstrcpy(passwordu, password);
    upstr(passwordu);
    shuffle(s_uid, passwordu, strlen(passwordu), passwd);
    if (!memcmp(passwd, stored_passwd, 16)) return(0);
  }
  if (NULL != (pw = nw_getpwnam(obj_id))) {
    int pwok = crypt_pw_ok(password, pw->pw_passwd);
    if (!pwok) {
      uint8 passwordu[100];
      xstrcpy(passwordu, password);
      downstr(passwordu);
      pwok = crypt_pw_ok(passwordu, pw->pw_passwd);
    }
    return((pwok) ? 0 : -0xff);
  } else return(-0xff);
}

static int nw_set_enpasswd(uint32 obj_id, uint8 *passwd, int dont_ch)
{
  uint8 *prop_name=(uint8*)"PASSWORD";
  if (passwd && *passwd) {
    if ((!dont_ch) || (nw_get_prop_val_str(obj_id, prop_name, NULL) < 1))
       nw_new_obj_prop(obj_id, NULL, 0, 0, 0,
  	                prop_name, P_FL_STAT|P_FL_ITEM,  0x44,
	                passwd, 16);
  } else if (!dont_ch)
    (void)loc_delete_property(obj_id, prop_name, 0);
  return(0);
}

int nw_set_passwd(uint32 obj_id, char *password, int dont_ch)
{
  if (password && *password) {
    uint8 passwd[200];
    uint8 s_uid[4];
    U32_TO_BE32(obj_id, s_uid);
    shuffle(s_uid, password, strlen(password), passwd);
#if 1
    XDPRINTF((2,0, "password %s->0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x",
       password,
       (int)passwd[0],
       (int)passwd[1],
       (int)passwd[2],
       (int)passwd[3],
       (int)passwd[4],
       (int)passwd[5],
       (int)passwd[6],
       (int)passwd[7],
       (int)passwd[8],
       (int)passwd[9],
       (int)passwd[10],
       (int)passwd[11],
       (int)passwd[12],
       (int)passwd[13],
       (int)passwd[14],
       (int)passwd[15]));
#endif
    return(nw_set_enpasswd(obj_id, passwd, dont_ch));
  } else
    return(nw_set_enpasswd(obj_id, NULL, dont_ch));
}


int prop_add_new_member(uint32 obj_id, int prop_id, uint32 member_id)
/* addiert member to set, if member not in set */
{
  int result = prop_find_member(obj_id, prop_id, member_id);
  if (-0xea == result)
    return(prop_add_member(obj_id, prop_id, member_id));
  else if (!result) result = -0xee;  /* already exist */
  return(result);
}

int nw_new_add_prop_member(uint32 obj_id, char *propname, uint32 member_id)
/* addiert member to set, if member not in set */
{
  NETPROP prop;
  int result;
  strmaxcpy(prop.name, propname, sizeof(prop.name));
  result = find_prop_id(&prop, obj_id, 0);
  if (!result) {
    if (-0xea == (result=prop_find_member(obj_id, prop.id, member_id)))
      return(prop_add_member(obj_id, prop.id, member_id));
    else if (!result) result = -0xee;  /* already exist */
  }
  return(result);
}

static void create_nw_db(char *fn, int allways)
{
  char   fname[200];
  struct stat stbuff;
  sprintf(fname, "%s/%s.dir", PATHNAME_BINDERY, fn);
  if (allways || stat(fname, &stbuff)){
    int fd = open(fname, O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (fd > -1) close(fd);
  }
  sprintf(fname, "%s/%s.pag", PATHNAME_BINDERY, fn);
  if (allways || stat(fname, &stbuff)){
    int fd = open(fname, O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (fd > -1) close(fd);
  }
}

static void add_pr_queue(uint32 q_id,
                         char *q_name, char *q_directory,
                         char *q_command,
                         uint32 su_id, uint32 ge_id)
{
  uint8  buff[12];
  XDPRINTF((2,0, "ADD Q=%s, V=%s, C=%s", q_name, q_directory, q_command));
  U32_TO_BE32(su_id,    buff);
  q_id =
  nw_new_obj_prop(q_id, q_name,               0x3,  O_FL_DYNA,  0x31,
	             "Q_OPERATORS",            	 P_FL_SET,  0x31,
	              (char*)buff,  4);

  nw_new_obj_prop(q_id ,NULL,              0  ,   0  ,   0   ,
	             "Q_DIRECTORY",           	 P_FL_ITEM,   0x33,
	              q_directory,  strlen(q_directory));

  /* this is a own property to handler the print job !!! */
  nw_new_obj_prop(q_id ,NULL,              0  ,   0  ,   0   ,
	             "Q_UNIX_PRINT",    P_FL_ITEM| P_FL_DYNA,   0x33,
	              q_command,  strlen(q_command));

  U32_TO_BE32(ge_id,   buff);
  nw_new_obj_prop(q_id , NULL,             0  ,   0  ,   0   ,
	             "Q_USERS",        	  P_FL_SET,  0x31,
	              (char*)buff,  4);

#if 0
  nw_new_obj_prop(q_id , NULL,             0  ,   0  ,   0   ,
	             "Q_SERVERS",             P_FL_SET,  0x31,
	              NULL,  0);
#endif
}

static void add_user_to_group(uint32 u_id,  uint32 g_id)
{
  uint8  buff[4];
  U32_TO_BE32(g_id, buff);
                                      /*   Typ    Flags  Security */
  nw_new_obj_prop(u_id,  NULL,               0x1  , 0x0,   0x33,
	             "GROUPS_I'M_IN",          P_FL_SET,   0x31,
	              (char*)buff,  4);

  nw_new_add_prop_member(g_id, "GROUP_MEMBERS", u_id);

  nw_new_obj_prop(u_id, NULL,                  0  ,   0  ,   0   ,
 	             "SECURITY_EQUALS",        P_FL_SET,   0x32,
                      (char*)buff,  4);

}

static void add_user(uint32 u_id,   uint32 g_id,
                  char   *name,  char  *unname,
                  char *password, int dont_ch)
{
                                      /*   Typ    Flags  Security */
  if (nw_new_obj(&u_id,  name,           0x1  , 0x0,   0x33)
       && dont_ch) return;
  XDPRINTF((1, 0, "Add/Change User='%s', UnixUser='%s'",
     	       	  name, unname));
  add_user_to_group(u_id, g_id);
  if (unname && *unname)
    nw_new_obj_prop(u_id, NULL,                 0  ,   0  ,   0 ,
      	             "UNIX_USER",             P_FL_ITEM,    0x33,
	             (char*)unname,  strlen(unname));

  if (password && *password) {
    if (*password == '-') *password='\0';
    nw_set_passwd(u_id, password, dont_ch);
  }
}

static void add_group(char *name,  char  *unname, char *password)
{
                                       /*   Typ    Flags  Security */
  uint32 g_id  = 0L;
  (void) nw_new_obj(&g_id,  name,       0x2  , 0x0,   0x31);
  if (unname && *unname)
    nw_new_obj_prop(g_id, NULL,                0  ,   0  ,   0 ,
      	             "UNIX_GROUP",           P_FL_ITEM,    0x33,
	             (char*)unname,  strlen(unname));
}

static int get_sys_unixname(uint8 *unixname, uint8 *sysentry)
{
  uint8 sysname[256];
  char  optionstr[256];
  int   founds = sscanf((char*)sysentry, "%s %s %s",sysname, unixname, optionstr);
  if (founds > 1 && *unixname) {
    struct stat statb;
    int result = 0;
    uint8 *pp  = unixname + strlen(unixname);
    if (founds > 2) {
      uint8 *p;
      for (p=optionstr; *p; p++) {
        if (*p=='k') {
          result=1;
          break;
        }
      } /* for */
    } /* if */
    if (*(pp-1) != '/') *pp++ = '/';
    *pp     = '.';
    *(pp+1) = '\0';
    if (stat(unixname, &statb) < 0) {
      errorp(1, "stat error:unix dir for SYS:", "fname='%s'", unixname);
      return(-1);
    }
    *pp  = '\0';
    return(result);
  } else return(-1);
}

static uint8 *test_add_dir(uint8 *unixname, uint8 *pp, int shorten,
              int downshift, int permiss, int gid, int uid, char *fn)
{
  struct stat stb;
  strcpy((char*)pp, fn);
  if (downshift) downstr(pp);
  else upstr(pp);
  if (stat(unixname, &stb) < 0) {
    if (mkdir(unixname, 0777)< 0)
      errorp(1, "mkdir error", "fname='%s'", unixname);
    else {
      chmod(unixname, permiss);
      if (uid >-1  && gid > -1) chown(unixname, uid, gid);
      XDPRINTF((1, 0, "Created dir '%s'", unixname));
    }
  }
  if (shorten) *pp='\0';
  else {
    pp += strlen(pp);
    *pp++='/';
    *pp  = '\0';
  }
  return(pp);
}


int nw_fill_standard(char *servername, ipxAddr_t *adr)
/* fills the Standardproperties */
{
  char   serverna[MAX_SERVER_NAME+2];
  uint32 su_id    = 0x00000001;
  uint32 ge_id    = 0x01000001;
  uint32 serv_id  = 0x03000001;
  uint32 q1_id    = 0x0E000001;
#if 0
  uint32 guest_id = 0x02000001;
  uint32 nbo_id   = 0x0B000001;
  uint32 ngr_id   = 0x0C000001;
  uint32 ps1_id   = 0x0D000001;
#endif
  FILE *f	     = open_nw_ini();
  int  auto_ins_user = 0;
  char auto_ins_passwd[100];
  int  make_tests    = 0;
  char sysentry[256];
  sysentry[0] = '\0';
  ge_id = nw_new_obj_prop(ge_id, "EVERYONE",        0x2,   0x0,  0x31,
	                   "GROUP_MEMBERS",          P_FL_SET,  0x31,
	                      NULL, 0);
  if (f){
    char buff[256];
    int  what;
    while (0 != (what =get_ini_entry(f, 0, (char*)buff, sizeof(buff)))) {
      if (1 == what && !*sysentry) {
        xstrcpy(sysentry, buff);
      } if (6 == what) {  /* Server Version */
        tells_server_version = atoi(buff);
      } else if (7 == what) {  /* password_scheme */
        int pwscheme     = atoi(buff);
        password_scheme  = 0;
        switch (pwscheme) {
          case  9 : password_scheme |= PW_SCHEME_GET_KEY_FAIL;
          case  8 : password_scheme |= PW_SCHEME_LOGIN;
          case  1 : password_scheme |= PW_SCHEME_CHANGE_PW;
                    break;
          default : password_scheme = 0;
                    break;
        } /* switch */
      } else if (21 == what) {  /* QUEUES */
        char name[100];
        char directory[200];
        char command[200];
        char *p=buff;
        char *pp=name;
        char c;
        int  state=0;
        while (0 != (c = *p++)) {
          if (c == 32 || c == '\t') {
            if (!(state & 1)) {
              *pp = '\0';
              state++;
            }
          } else {
            if (state & 1){
              if (state == 1) {
                pp=directory;
                state++;
              } else {
                strcpy(command, p-1);
                if (*command) state++;
                break;
              }
            }
            *pp++ = c;
          }
        }
        *pp='\0';
        if (state == 4) {
          upstr(name);
          add_pr_queue(q1_id, name, directory, command, su_id, ge_id);
          q1_id++;
        }
      } else if (12 == what || 13 == what || 14 == what) {
        /* SUPERVISOR, OTHERS  and GROUPS*/
        char nname[100];
        char uname[100];
        char password[100];
        int  anz=sscanf((char*)buff, "%s %s %s", nname, uname, password);
        if (anz > 1) {
          upstr(nname);
          if (anz > 2) upstr(password);
          else password[0] = '\0';
          if (what == 14)
            add_group(nname, uname, password);
          else
            add_user((12 == what) ? su_id : 0L, ge_id, nname,
                            uname, password, 0);
        }
      } else if (15 == what) {
        char buf[100];
        int  anz=sscanf((char*)buff, "%s %s", buf, auto_ins_passwd);
        auto_ins_user = ((anz == 2) && atoi(buf) && *auto_ins_passwd);
        if (auto_ins_user) auto_ins_user = atoi(buf);
      } else if (16 == what) {
        make_tests = atoi(buff);
      }
    } /* while */
    fclose(f);
  }
  if (servername && adr) {
    strmaxcpy(serverna, servername, MAX_SERVER_NAME);
    upstr(serverna);
    nw_new_obj_prop(serv_id, serverna,       0x4,      O_FL_DYNA, 0x40,
	               "NET_ADDRESS",         P_FL_ITEM | P_FL_DYNA, 0x40,
	                (char*)adr,  sizeof(ipxAddr_t));
  }
  if (auto_ins_user) {
    struct passwd *pw;
    upstr(auto_ins_passwd);
    while (NULL != (pw=getpwent())) {
      char nname[100];
      xstrcpy(nname, pw->pw_name);
      upstr(nname);
      add_user(0L, ge_id, nname, pw->pw_name, auto_ins_passwd,
        (auto_ins_user == 99) ? 0 : 99);
    }
    endpwent();
  }
  if (*sysentry) {
    int result = 0;
    if (make_tests) {
      uint8 unixname[512];
      result = get_sys_unixname(unixname, sysentry);
      if (result > -1) {
        uint32  objs[2000]; /* max. 2000 User should be enough :) */
        int     ocount=0;
        int downshift = (result & 1);
        uint8  *pp = unixname+strlen(unixname);
        uint8  *ppp;
        test_add_dir(unixname,     pp, 1, downshift,0777, 0,0, "LOGIN");
        test_add_dir(unixname,     pp, 1, downshift,0777, 0,0, "SYSTEM");
        test_add_dir(unixname,     pp, 1, downshift,0777, 0,0, "PUBLIC");
        ppp=test_add_dir(unixname, pp, 0, downshift,0777, 0,0, "MAIL");
        if (!dbminit(FNOBJ)){
          for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
            data = fetch(key);
            if (data.dptr) {
	      NETOBJ *obj=(NETOBJ*)data.dptr;
	      if (obj->type == 1) {
	        objs[ocount++] = obj->id;
                if (ocount == 2000) break;
              }
            }
          }
        }
        dbmclose();
        while (ocount--) {
          char sx[20];
          int gid;
          int uid;
          sprintf(sx, "%lx", objs[ocount]);
          if (!get_guid(&gid, &uid, objs[ocount]))
            test_add_dir(unixname, ppp, 1, downshift, 0770, gid, uid, sx);
          else  {
            NETOBJ obj;
            obj.id = objs[ocount];
            nw_get_obj(&obj);
            errorp(0, "Cannot get unix uid/gid", "User=`%s`", obj.name);
          }
        }
        result = 0;
      }
    }
    return(result);
  }
  return(-1);
}

int nw_init_dbm(char *servername, ipxAddr_t *adr)
/*
 * routine inits bindery
 * all dynamic objects and properties will be deletet.
 * and the allways needed properties will be created
 * if not exist.
 */
{
  int     anz=0;
  uint32  objs[10000];   /* max.10000 Objekte    */
  uint8   props[10000];  /* max 10000 Properties */

  create_nw_db(dbm_fn[FNOBJ],  0);
  create_nw_db(dbm_fn[FNPROP], 0);
  create_nw_db(dbm_fn[FNVAL],  0);

  if (!dbminit(FNOBJ)){
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      data = fetch(key);
      if (data.dptr) {
	NETOBJ *obj=(NETOBJ*)data.dptr;
	if ((obj->flags & O_FL_DYNA) || !obj->name[0]) {
	  /* dynamic or without name */
	  objs[anz++] = obj->id;
	  if (anz == 10000) break;
        }
      }
    }
  }
  dbmclose();
  while (anz--) loc_delete_obj(objs[anz]);  /* Now delete */
  anz = 0;
  if (!dbminit(FNPROP)){
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      data = fetch(key);
      if (data.dptr) {
	NETPROP *prop=(NETPROP*)data.dptr;
	if (prop->flags & P_FL_DYNA) { /* Dynamisch */
	  objs[anz]    = prop->obj_id;
	  props[anz++] = prop->id;
	  if (anz == 10000) break;
	}
      }
    }
  }
  dbmclose();
  while (anz--) loc_delete_property(objs[anz], (char*)NULL, props[anz]);  /* now delete */
  anz = nw_fill_standard(servername, adr);
  sync_dbm();
  return(anz);
}

/* ============> this should becomes queue.c or similar < ============== */

int nw_get_q_dirname(uint32 q_id, uint8 *buff)
{
  return(nw_get_prop_val_str(q_id, "Q_DIRECTORY", buff));
}

int nw_get_q_prcommand(uint32 q_id, uint8 *buff)
{
  return(nw_get_prop_val_str(q_id, "Q_UNIX_PRINT", buff));
}
