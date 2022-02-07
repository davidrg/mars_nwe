/* nwdbm.c  05-Dec-95  data base for mars_nwe */
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
#ifdef LINUX
#  include <ndbm.h>
#else
#  include </usr/ucbinclude/ndbm.h>
#endif

int        tells_server_version=0;

static char *fnprop   = "nwprop";
static char *fnval    = "nwval";
static char *fnobj    = "nwobj";

static datum key;
static datum data;
static DBM   *my_dbm=NULL;

static int dbminit(char *s)
{
  char buff[256];
  sprintf(buff, "%s/%s", PATHNAME_BINDERY, s);
  my_dbm = dbm_open(buff, O_RDWR|O_CREAT, 0666);
  return( (my_dbm == NULL) ? -1 : 0);
}

static int dbmclose()
{
  if (my_dbm != NULL) {
    dbm_close(my_dbm);
    my_dbm = NULL;
  }
  return(0);
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
  XDPRINTF((2, "findobj_id OBJ=%s, type=0x%x, lastid=0x%lx \n",
	      o->name, (int)o->type, last_obj_id));

  if (!dbminit(fnobj)){

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
	  XDPRINTF((2, "found OBJ=%s, id=0x%lx\n", obj->name, obj->id));
	  result = 0;
	  memcpy((char *)o, (char*)obj, sizeof(NETOBJ));
	} else {
	  XDPRINTF((3,"not found,but NAME=%s, type=0x%x, id=0x%lx \n",
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
    XDPRINTF((2, "loc_delete_property obj_id=%d, prop=%s\n", obj_id, prop_name));
    if (!dbminit(fnprop)){
      for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
	NETPROP *p=(NETPROP*)key.dptr;
	if (p->obj_id == obj_id) {
	  data = fetch(key);
	  p = (NETPROP*)data.dptr;
	  if (p != NULL && name_match(p->name, prop_name)){
	    XDPRINTF((2, "found prop: %s, id=%d for deleting\n", p->name, (int)p->id));
	    if ((int)(p->id) > result) result = (int)(p->id);
	    xset[p->id]++;
	  }
	}
      } /* for */
    } else result = -0xff;
    dbmclose();
  } else {
    XDPRINTF((2, "loc_delete_property obj_id=%d, prop_id=%d\n", obj_id, (int)prop_id));
    xset[prop_id]++;
    result = prop_id;
  }
  if (result > 0) {
    if (!dbminit(fnval)){
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
      if (!dbminit(fnprop)){  /* now delete properties */
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
  if (!dbminit(fnobj)){
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
  XDPRINTF((2, "nw_delete_obj obj_id=%d, obj_name=%s\n", obj->id, obj->name));
  if (!result) result=loc_delete_obj(obj->id);
  return(result);
}

int nw_rename_obj(NETOBJ *o, uint8 *newname)
/* rename object */
{
  int result = find_obj_id(o, 0);
  if (!result) {
    result = -0xff;
    if (!dbminit(fnobj)){
      key.dsize = NETOBJ_KEY_SIZE;
      key.dptr  = (char*)o;
      data      = fetch(key);
      if (data.dptr != NULL){
        NETOBJ *obj=(NETOBJ*)data.dptr;
        XDPRINTF((2, "rename_obj:got OBJ name=%s, id = 0x%x,\n", obj->name, (int)obj->id));
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
    if (!dbminit(fnobj)){
      key.dsize = NETOBJ_KEY_SIZE;
      key.dptr  = (char*)o;
      data      = fetch(key);
      if (data.dptr != NULL){
        NETOBJ *obj=(NETOBJ*)data.dptr;
        XDPRINTF((2, "change_obj_security:got OBJ name=%s, id = 0x%x,\n", obj->name, (int)obj->id));
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
  XDPRINTF((2, "nw_get_obj von OBJ id = 0x%x,\n", (int)o->id));
  if (!dbminit(fnobj)){
    key.dsize = NETOBJ_KEY_SIZE;
    key.dptr  = (char*)o;
    data      = fetch(key);
    if (data.dptr != NULL){
      NETOBJ *obj=(NETOBJ*)data.dptr;
      XDPRINTF((2, "got OBJ name=%s, id = 0x%x,\n", obj->name, (int)obj->id));
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
  XDPRINTF((2, "find Prop id von name=0x%x:%s, lastid=%d\n",
           obj_id, p->name, last_prop_id));
  if (!dbminit(fnprop)){
    int  flag = (last_prop_id) ? 0 : 1;
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      NETPROP *prop=(NETPROP*)key.dptr;
      if (prop->obj_id == obj_id) {
        if (!flag) flag = (last_prop_id == prop->id);
        else {
	  data = fetch(key);
	  prop = (NETPROP*)data.dptr;
	  if (data.dptr != NULL  && name_match(prop->name, p->name) )  {
	    XDPRINTF((2, "found PROP %s, id=0x%x\n", prop->name, (int) prop->id));
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
  XDPRINTF((2, "loc_change_prop_security Prop id von name=0x%x:%s\n", obj_id, p->name));
  if (!dbminit(fnprop)){
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      NETPROP *prop=(NETPROP*)key.dptr;
      if (prop->obj_id == obj_id) {
	data = fetch(key);
	prop = (NETPROP*)data.dptr;
	if (data.dptr != NULL  && name_match(prop->name, p->name) )  {
	  uint8 security = p->security;
	  XDPRINTF((2, "found PROP %s, id=0x%x\n", prop->name, (int) prop->id));
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
  if (!dbminit(fnval)){
    key.dsize   = NETVAL_KEY_SIZE;
    key.dptr    = (char*)&val;
    val.obj_id  = obj_id;
    val.prop_id = (uint8)prop_id;
    val.segment = (uint8)segment;
    data        = fetch(key);
    if (data.dptr != NULL){
      NETVAL  *v = (NETVAL*)data.dptr;
      if (NULL != property_value) memcpy(property_value, v->value, 128);
      XDPRINTF((2, "found VAL 0x%x, %d, %d\n", obj_id, prop_id, segment));
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
  if (!dbminit(fnval)){
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
      XDPRINTF((2, "found VAL 0x%x, %d\n", obj_id, prop_id));
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
  if (!dbminit(fnval)){
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
  if (!dbminit(fnval)){
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
  if (!dbminit(fnval)){
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
  XDPRINTF((2, "nw_get_prop_val_by_obj_id,id=0x%x, prop=%s, segment=%d\n",
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
  XDPRINTF((2, "nw_delete_property obj=%s, prop=%s, type=0x%x\n",
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
  XDPRINTF((2, "nw_is_obj_in_set obj=%s,0x%x, member=%s,0x%x, prop=%s\n",
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
  XDPRINTF((2, "nw_add_obj_to_set obj=%s,0x%x, member=%s,0x%x, prop=%s\n",
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
  XDPRINTF((2, "nw_delete_obj_from_set obj=%s,0x%x, member=%s,0x%x, prop=%s\n",
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
  XDPRINTF((2, "nw_write_prop_value obj=%s, prop=%s, type=0x%x, segment=%d\n",
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
  XDPRINTF((2, "nw_change_prop_security obj=%s,0x%x, prop=%s\n",
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
  XDPRINTF((2, "nw_scan_property obj=%s, prop=%s, type=0x%x, last_scan=0x%lx\n",
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
  int    result=nw_get_prop_val_by_obj_id(q_id, 1, propname, strlen(propname),
                  buff, &more_segments, &property_flags);
  if (result > -1) {
    result=strlen(buff);
    XDPRINTF((2, "nw_get_prop_val_str:%s strlen=%d\n", propname, result));
  } else
    XDPRINTF((2, "nw_get_prop_val_str:%s, result=0x%x\n", propname, result));
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
  XDPRINTF((2, "creat OBJ=%s,type=0x%x\n", obj->name, (int)obj->type));
  if (!dbminit(fnobj)){
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
  int result = (dbminit(fnprop)) ? -0xff : 0;
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
  if (!dbminit(fnprop)){
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
  XDPRINTF((2, "nw_create_prop obj=%s, prop=%s, type=0x%x\n",
      obj.name, prop.name, object_type));
  obj.type    = (uint16) object_type;
  if ((result = find_obj_id(&obj, 0)) == 0){
    prop.flags    = (uint8)prop_flags;
    prop.security = (uint8)prop_security;
    result = nw_create_obj_prop(&obj, &prop);
  }
  return(result);
}

uint32 nw_new_create_prop(uint32 wanted_id,
                  char *objname, int objtype, int objflags, int objsecurity,
	          char *propname, int propflags, int propsecurity,
	          char *value, int valuesize)
/*
 * creats new property value, if needed creats Object
 * and the property, if valuesize == 0, then only obj or property
 * will be created, returns obj-id
 */
{
  NETOBJ  obj;
  NETPROP prop;
  if (objname && *objname){
    strmaxcpy(obj.name, objname, sizeof(obj.name));
    obj.type      =  (uint8)objtype;
    obj.flags     =  (uint8)objflags;
    obj.security  =  (uint8)objsecurity;
    obj.id        =  0; /* Erstmal */
    nw_create_obj(&obj, wanted_id);
  } else obj.id = wanted_id;
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
  return(obj.id);
}

struct passwd *nw_getpwnam(uint32 obj_id)
{
  static struct passwd pwstat;
  char buff[200];
  if (nw_get_prop_val_str(obj_id, "UNIX_USER", buff) > 0){
    struct passwd *pw = getpwnam(buff);
    if (NULL != pw) {
      memcpy(&pwstat, pw, sizeof(struct passwd));
      XDPRINTF((2, "FOUND obj_id=0x%x, pwnam=%s, gid=%d, uid=%d\n",
                 obj_id, buff, pw->pw_gid, pw->pw_uid));
      endpwent ();
      return(&pwstat);
    }
    endpwent ();
  }
  XDPRINTF((2, "NOT FOUND PWNAM of obj_id=0x%x\n",  obj_id));
  return(NULL);
}

int get_guid(int *gid, int *uid, uint32 obj_id)
/* searched for gid und uid of actual obj */
{
  struct passwd *pw = nw_getpwnam(obj_id);
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

int nw_test_passwd(uint32 obj_id, uint8 *vgl_key, uint8 *akt_key)
/* returns 0, if password ok and -0xff if not ok */
{
  char buf[200];
  if (nw_get_prop_val_str(obj_id, "PASSWORD", buf) > 0) {
    uint8 keybuff[8];
    memcpy(keybuff, vgl_key, sizeof(keybuff));
    nw_encrypt(keybuff, buf, keybuff);
    return (memcmp(akt_key, keybuff, sizeof(keybuff)) ? -0xff : 0);
  } else return(0); /* no password */
}

int nw_set_enpasswd(uint32 obj_id, uint8 *passwd)
{
  nw_new_create_prop(obj_id, NULL, 0, 0, 0,
	          "PASSWORD", P_FL_STAT|P_FL_ITEM,  0x44,
	           passwd, 16);
  return(0);
}

int nw_set_passwd(uint32 obj_id, char *password)
{
  uint8 passwd[200];
  uint8 s_uid[4];
  U32_TO_BE32(obj_id, s_uid);
  shuffle(s_uid, password, strlen(password), passwd);
#if 0
  XDPRINTF((2, "password %s->0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",
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
  return(nw_set_enpasswd(obj_id, passwd));
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
  XDPRINTF((2, "ADD Q=%s, V=%s, C=%s\n", q_name, q_directory, q_command));
  U32_TO_BE32(su_id,    buff);
  q_id =
  nw_new_create_prop(q_id, q_name,               0x3,  O_FL_DYNA,  0x31,
	             "Q_OPERATORS",            	 P_FL_SET,  0x31,
	              (char*)buff,  4);

  nw_new_create_prop(q_id ,NULL,              0  ,   0  ,   0   ,
	             "Q_DIRECTORY",           	 P_FL_ITEM,   0x33,
	              q_directory,  strlen(q_directory));

  /* this is a own property to handler the print job !!! */
  nw_new_create_prop(q_id ,NULL,              0  ,   0  ,   0   ,
	             "Q_UNIX_PRINT",    P_FL_ITEM| P_FL_DYNA,   0x33,
	              q_command,  strlen(q_command));

  U32_TO_BE32(ge_id,   buff);
  nw_new_create_prop(q_id , NULL,             0  ,   0  ,   0   ,
	             "Q_USERS",        	  P_FL_SET,  0x31,
	              (char*)buff,  4);

#if 0
  nw_new_create_prop(q_id , NULL,             0  ,   0  ,   0   ,
	             "Q_SERVERS",             P_FL_SET,  0x31,
	              NULL,  0);
#endif


}


static uint32 add_user(uint32 u_id,   uint32 g_id,
                       char   *name,  char  *unname, char *password)
{
  uint8  buff[4];
  U32_TO_BE32(g_id, buff);
  u_id =                                  /*   Typ    Flags  Security */
  nw_new_create_prop(u_id,  name,              0x1  , 0x0,   0x33,
	             "GROUPS_I'M_IN",            P_FL_SET,   0x31,
	              (char*)buff,  4);

  nw_new_create_prop(u_id, NULL,                0  ,   0  ,   0   ,
	             "SECURITY_EQUALS",        P_FL_SET,    0x32,
	             (char*)buff,  4);

  nw_new_add_prop_member(g_id, "GROUP_MEMBERS", u_id);

  if (unname && *unname)
    nw_new_create_prop(u_id, NULL,              0  ,   0  ,   0 ,
      	             "UNIX_USER",             P_FL_ITEM,    0x33,
	             (char*)unname,  strlen(unname));

  if (password && *password) nw_set_passwd(u_id, password);
}

void nw_fill_standard(char *servername, ipxAddr_t *adr)
/* fills the Standardproperties */
{
  char   serverna[50];
  uint8  buff[12];
  uint32 su_id    = 0x00000001;
  uint32 ge_id    = 0x01000001;

  uint32 guest_id = 0x02000001;
  uint32 serv_id  = 0x03000001;
  uint32 nbo_id   = 0x0B000001;
  uint32 ngr_id   = 0x0C000001;
  uint32 ps1_id   = 0x0D000001;
  uint32 q1_id    = 0x0E000001;
  FILE *f	  = open_nw_ini();
  ge_id =
  nw_new_create_prop(ge_id, "EVERYONE",        0x2,   0x0,  0x31,
	             "GROUP_MEMBERS",          P_FL_SET,  0x31,
	              NULL, 0);
  if (f){
    char buff[500];
    int  what;
    while (0 != (what =get_ini_entry(f, 0, (char*)buff, sizeof(buff)))) {
      if (6 == what) {  /* Server Version */
        tells_server_version = atoi(buff);
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
      } else if (12 == what || 13 == what) {  /* SUPERVISOR, OTHERS */
        char nname[100];
        char uname[100];
        char password[100];
        int  anz=sscanf((char*)buff, "%s %s %s", nname, uname, password);
        if (anz > 1) {
          upstr(nname);
          if (anz > 2) upstr(password);
          else password[0] = '\0';
          add_user((12 == what) ? su_id : 0L, ge_id, nname,
               uname, password);
        }
      }
    } /* while */
    fclose(f);
  }
  if (servername && adr) {
    strmaxcpy(serverna, servername, 48);
    upstr(serverna);
    nw_new_create_prop(serv_id, serverna,       0x4,      O_FL_DYNA, 0x40,
	               "NET_ADDRESS",         P_FL_ITEM | P_FL_DYNA, 0x40,
	                (char*)adr,  sizeof(ipxAddr_t));
  }
}

void nw_init_dbm(char *servername, ipxAddr_t *adr)
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

  create_nw_db(fnobj,  0);
  create_nw_db(fnprop, 0);
  create_nw_db(fnval,  0);

  if (!dbminit(fnobj)){
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      data = fetch(key);
      if (data.dptr) {
	NETOBJ *obj=(NETOBJ*)data.dptr;
	if (obj->flags & O_FL_DYNA) /* Dynamisch */
	  objs[anz++] = obj->id;
      }
    }
  }
  dbmclose();
  while (anz--) loc_delete_obj(objs[anz]);  /* Now delete */
  anz = 0;
  if (!dbminit(fnprop)){
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      data = fetch(key);
      if (data.dptr) {
	NETPROP *prop=(NETPROP*)data.dptr;
	if (prop->flags & P_FL_DYNA) { /* Dynamisch */
	  objs[anz]    = prop->obj_id;
	  props[anz++] = prop->id;
	}
      }
    }
  }
  dbmclose();
  while (anz--) loc_delete_property(objs[anz], (char*)NULL, props[anz]);  /* Nun l”schen */
  nw_fill_standard(servername, adr);
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
