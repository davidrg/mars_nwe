/* namspace.c 13-May-98 : NameSpace Services, mars_nwe */

/* !!!!!!!!!!!! NOTE !!!!!!!!!! */
/* Its still dirty, but it should work fairly well */

/* (C)opyright (C) 1993,1998  Martin Stover, Marburg, Germany
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
#include <utime.h>
#ifndef LINUX
#include <errno.h>
#endif

#include "nwfname.h"
#include "nwvolume.h"
#include "connect.h"
#include "nwattrib.h"
#include "trustee.h"
#include "nwconn.h"
#include "nwfile.h"
#include "unxfile.h"
#include "namspace.h"
#include "nameos2.h"

#if WITH_NAME_SPACE_CALLS

#define NW_PATH  /* */

typedef struct {
  int    volume;            /* Volume Number                 */
  int    has_wild;          /* fn has wildcards              */
  int    namespace;         /* which namespace do we use here*/
  struct stat statb;        /* stat buff                     */
  uint8  *fn;               /* points to last entry of path  */
  uint8  path[512];         /* path + fn, unixfilename       */
} N_NW_PATH;

typedef struct {
  DIR        *fdir;         /* for dir searches              */
  uint8      *kpath;        /* points one after unixname     */
  uint8      *unixname;     /* allocated fullname of path    */
                            /* + 257 Byte for filename.      */
} DIR_SEARCH_STRUCT;

typedef struct {
  uint32         basehandle;
  int            slot;        /* act slot in table             */
  int            locked;      /* if locked then do not remove  */
                              /* and do not move till end      */
  N_NW_PATH      nwpath;
} DIR_BASE_ENTRY;

static DIR_BASE_ENTRY **dir_base=NULL;
static int anz_dbe    = 0;
static int max_dir_base_entries=0;

typedef struct {
  int      dev;           /* from searchdir */
  ino_t    inode;         /* from searchdir */
  int      idle;
  off_t    dirpos;        /* last readdirpos                */
  ino_t    found_inode;   /* last found inode at readdirpos */
  int      lastsequence;  /* last sequence                  */
} DIR_SEARCH_HANDLE;

static DIR_SEARCH_HANDLE **dir_search_handles=NULL;
static int count_dsh = 0;
static int max_dir_search_handles=0;

static uint32 new_search_handle(int dev, ino_t inode)
{
  int k=count_dsh;
  int foundfree=-1;
  DIR_SEARCH_HANDLE *d;
  while (k--) {
    if (!dir_search_handles[k]) {
      foundfree=k;
    } else {
      if (dir_search_handles[k]->idle < MAX_I32)
        dir_search_handles[k]->idle++;
      else {
        xfree(dir_search_handles[k]);
        if (foundfree < 0 && k+1==count_dsh) count_dsh--;
        else foundfree=k;
      }
    }
  }
  if (foundfree < 0 && count_dsh < max_dir_search_handles)
    foundfree=count_dsh++;
  if (foundfree < 0) {
    int idle=-1;
    for (k=0; k < count_dsh; k++){
      if (dir_search_handles[k]->idle > idle) {
        foundfree=k;
        idle=dir_search_handles[k]->idle;
      }
    }
  } else {
    dir_search_handles[foundfree]=
       (DIR_SEARCH_HANDLE*)xmalloc(sizeof(DIR_SEARCH_HANDLE));
  }
  d=dir_search_handles[foundfree];
  memset(d, 0, sizeof(DIR_SEARCH_HANDLE));
  d->dev   = dev;
  d->inode = inode;
  return((uint32) foundfree);
}

static void free_search_handles(void)
{
  while (count_dsh--)  {
    xfree(dir_search_handles[count_dsh]);
  }
  count_dsh=0;
  xfree(dir_search_handles);
}

static void init_nwpath(N_NW_PATH *nwpath, int namespace)
{
  nwpath->volume       = -1;
  nwpath->namespace    = namespace;
  nwpath->has_wild     = 0;
  nwpath->fn           = nwpath->path;
  *(nwpath->path)      = '\0';
  nwpath->statb.st_ino = -1;
}

static void norm_nwpath_fn(N_NW_PATH *nwpath)
{
  nwpath->fn = (uint8*)strrchr((char*)nwpath->path, '/');
  if (nwpath->fn) nwpath->fn++;
  else nwpath->fn=nwpath->path;
}

static char *xnwpath_2_unix(N_NW_PATH *nwpath, int modus,
                   int allocate_extra, uint8 *extra_path)
/*
 * returns complete UNIX path
 * modus & 1 : ignore fn, (only path)
 * modus & 2 : no  '/' at end
 *
 * if allocate_extra > 0, then the returned buffer must be
 * deallocated later
 */
{
  static char *last_unixname=NULL;
  char   *unixname;
  int    len;
  int    volume = nwpath->volume;
  int    len_extra = (extra_path) ? strlen(extra_path) : 0;
  char *p, *pp;
  if (volume < 0 || volume >= used_nw_volumes || !nw_volumes[volume].unixnamlen ) {
    char *errorstr="Z/Z/Z";
    errorp(0, "xnwpath_2_unix", "volume=%d not ok", volume);
    len = strlen(errorstr);
    unixname=xmalloc(len_extra+allocate_extra+len+10);
    strcpy(unixname, errorstr);
  } else {
    int m = ((modus & 1) && nwpath->fn > nwpath->path)  /* last path = fn */
               ? nwpath->fn - nwpath->path
               : strlen((char*)nwpath->path);
    len   = nw_volumes[volume].unixnamlen;
    unixname=xmalloc(len_extra+allocate_extra+m+len+10);
    memcpy(unixname, nw_volumes[volume].unixname, len);
       /* first UNIXNAME VOLUME */
    p = pp = unixname+len;
    memcpy(p, nwpath->path, m); /* path or partiell path */
    len += m;
    p   += m;
    if ((modus & 2) && *(p-1) == '/' ) {
      if (p > unixname+1) {
        --p;
        --len;
        --m;
      } else {
        *p++ = '.';
        ++len;
      }
    } else if (len_extra) {
      if (*(p-1) != '/') {
        *p++='/';
        len++;
      }
      memcpy(p, extra_path, len_extra);
      p   += len_extra;
      len += len_extra;
    }
    *p = '\0';
  }
  dos2unixcharset(unixname);
  if (!allocate_extra) {
    xfree(last_unixname);
    last_unixname=unixname;
  }
  return(unixname);
}

#define nwpath_2_unix(nwpath, modus) \
   xnwpath_2_unix((nwpath), (modus), 0, NULL)
#define alloc_nwpath2unix(nwpath, modus) \
   xnwpath_2_unix((nwpath), (modus), 1, NULL)
#define alloc_nwpath2unix_big(nwpath, modus) \
   xnwpath_2_unix((nwpath), (modus), 258, NULL)
#define alloc_nwpath2unix_extra(nwpath, modus, extrastr) \
   xnwpath_2_unix((nwpath), (modus), 1, extrastr)

char *debug_nwpath_name(N_NW_PATH *p)
/* only for debugging */
{
#if DO_DEBUG
  static char *nwpathname=NULL;
  char volname[300];
  int  len;
  if (nw_get_volume_name(p->volume, volname) < 1)
    sprintf(volname, "<%d=NOT-OK>", (int)p->volume);
  len = strlen(volname) + strlen(p->path) + strlen(p->fn) + 40;
  xfree(nwpathname);
  nwpathname=xmalloc(len);
  sprintf(nwpathname, "%d `%s:%s`,fn=`%s`",
      p->namespace, volname, p->path, p->fn);
#else
  static char nwpathname[2];
  nwpathname[0]='\0';
  nwpathname[1]='\0';
#endif
  return(nwpathname);
}

static int nwp_stat(N_NW_PATH *nwpath, char *debstr)
{
  uint8 *uname=alloc_nwpath2unix(nwpath, 2);
  int result=stat(uname, &(nwpath->statb));
  if (nw_debug) {
    char xdebstr[2];
    if (!debstr) {
      xdebstr[0]='\0';
      debstr = xdebstr;
    }
    XDPRINTF((4, 0, "nwp_stat:%s:%d,%s",
               debstr,
              result,
             debug_nwpath_name(nwpath)));
  }
  xfree(uname);
  return(result);
}


static int downsort_dbe_entries(int dbase)
{
  DIR_BASE_ENTRY **dbep=&(dir_base[dbase]);
  DIR_BASE_ENTRY **dbpq=dbep;
  int k=dbase;
  while (k--) {
    --dbpq;
    if (!(*dbpq) || !(*dbpq)->locked) {
      *dbep = *dbpq;
      if (*dbep) (*dbep)->slot = dbase;
      dbep=dbpq;
      dbase=k;
    }
  }
  return(dbase);
}


static void put_dbe_to_disk(DIR_BASE_ENTRY *dbe)
{
  char   buf[255];
  char   volname[100];
  int    l;
  uint8  inode_uc[4];
#if 0
  int    voloptions=get_volume_options(dbe->nwpath.volume);
#endif
  if (nw_get_volume_name(dbe->nwpath.volume, volname) < 1)
    return;
  U32_TO_BE32(dbe->nwpath.statb.st_ino, inode_uc);
  l=sprintf(buf, "%s/%s/%x/%x/%x/%x/%x", path_vol_inodes_cache, volname,
#if 0
            (voloptions & VOL_OPTION_IS_HOME) ? act_connection : 0,
#else
            0,
#endif
            dbe->nwpath.statb.st_dev,
            (int) inode_uc[0],
            (int) inode_uc[1],
            (int) inode_uc[2]);

  seteuid(0);
  unx_xmkdir(buf, 0755);
  sprintf(buf+l, "/%x", (int) inode_uc[3]);
  unlink(buf);
  symlink(dbe->nwpath.path, buf);
  reseteuid();
}

static void del_dbe_from_disk(DIR_BASE_ENTRY *dbe)
{
  char   buf[255];
  char   volname[100];
  uint8  inode_uc[4];
#if 0
  int    voloptions=get_volume_options(dbe->nwpath.volume);
#endif
  if (nw_get_volume_name(dbe->nwpath.volume, volname) < 1)
    return;
  U32_TO_BE32(dbe->nwpath.statb.st_ino, inode_uc);
  sprintf(buf, "%s/%s/%x/%x/%x/%x/%x/%x", path_vol_inodes_cache, volname,
#if 0
            (voloptions & VOL_OPTION_IS_HOME) ? act_connection : 0,
#else
            0,
#endif
            dbe->nwpath.statb.st_dev,
            (int) inode_uc[0],
            (int) inode_uc[1],
            (int) inode_uc[2],
            (int) inode_uc[3]);
  seteuid(0);
  unlink(buf);
  reseteuid();
}


static int get_dbe_data_from_disk(int volume,
                             int dev, ino_t inode, uint8 *path)
/* returns 0 if all ok */
{
  char   buf[255];
  char   volname[100];
  int    l;
  uint8  inode_uc[4];
#if 0
  int    voloptions=get_volume_options(volume);
#endif
  if (nw_get_volume_name(volume, volname) < 1) {
    XDPRINTF((1, 0, "get_dbe_d_f_d: wrong volume=%d", volume));
    return (-1);
  }
  U32_TO_BE32(inode, inode_uc);
  sprintf(buf, "%s/%s/%x/%x/%x/%x/%x/%x", path_vol_inodes_cache, volname,
#if 0
            (voloptions & VOL_OPTION_IS_HOME) ? act_connection : 0,
#else
            0,
#endif
            dev,
            (int) inode_uc[0],
            (int) inode_uc[1],
            (int) inode_uc[2],
            (int) inode_uc[3]);

  seteuid(0);
  l=readlink(buf, path, 511);
  reseteuid();

  if (l > 0) {
    path[l]='\0';
    return(0);
  } else {
    errorp(0, "get_dbe_d_f_d", "readlink of `%s`=%d", buf, l);
    return(-1);
  }
}

static DIR_BASE_ENTRY *allocate_dbe_p(int namespace)
/* returns new allocated dir_base_entry_pointer */
{
  int j     =-1;
  int to_use_free=-1;
  int to_use_file=-1;
  DIR_BASE_ENTRY *dbe;
  while (++j < anz_dbe && NULL != (dbe = dir_base[j])){
    if (j > 3 && !dbe->locked) {
      if (!dbe->basehandle)
        to_use_free=j;
      else if (j > 10 && !S_ISDIR(dbe->nwpath.statb.st_mode))
        to_use_file=j;
    }
    if (dbe->slot != j) {
      errorp(0, "allocate_dbe_p", "slot %d != %d", dbe->slot);
    }
  }

  if (j == anz_dbe) {
    if (anz_dbe == max_dir_base_entries) {
      if (to_use_free > -1) {
        j    = to_use_free;
      } else if (to_use_file > -1) {
        j    = to_use_file;
        /* caching directories is more important than caching files */
      } else {
        while (j && dir_base[--j]->locked) ;;
      }
      if (S_ISDIR(dir_base[j]->nwpath.statb.st_mode)
            || (entry8_flags & 0x20) ) {
        put_dbe_to_disk(dir_base[j]);
      }
      xfree(dir_base[j]);
    } else
      anz_dbe++;
    dir_base[j] = NULL;
  }
  j=downsort_dbe_entries(j);
  dbe=dir_base[j]=(DIR_BASE_ENTRY*)xcmalloc(sizeof(DIR_BASE_ENTRY));
  dbe->slot  = j;
  init_nwpath(&(dbe->nwpath), namespace);
  return(dbe);
}

static void free_dir_bases(void)
/* free's all dir_bases */
{
  if (dir_base) {
    while (anz_dbe--) {
      if (dir_base[anz_dbe]) {
        if (S_ISDIR(dir_base[anz_dbe]->nwpath.statb.st_mode)
              || (entry8_flags & 0x20) ) {
          put_dbe_to_disk(dir_base[anz_dbe]);
        }
        xfree(dir_base[anz_dbe]);
      }
    }
    anz_dbe=0;
    xfree(dir_base);
  }
}

static void xx_free_dbe_p(DIR_BASE_ENTRY **dbe)
{
  if (NULL != dbe && NULL != *dbe) {
    int slot=(*dbe)->slot;
    xfree(*dbe);
    *dbe=dir_base[slot] = NULL;
    while (anz_dbe && ((DIR_BASE_ENTRY*)NULL == dir_base[anz_dbe-1]) )
        --anz_dbe;
  }
}
#define free_dbe_p(dbe)  xx_free_dbe_p(&(dbe))

static int touch_handle_entry_p(DIR_BASE_ENTRY *dbe)
/* routine touchs this entry and returns the new offset */
{
  int dbase = (NULL != dbe) ? dbe->slot : -1;
  XDPRINTF((7, 0, "touch_handle_entry_p entry dbase=%d", dbase));
  if (dbase > 4) {
    dir_base[dbase] = NULL;
    dbase=downsort_dbe_entries(dbase);
    dir_base[dbase] = dbe;
    dbe->slot       = dbase;
  }
  XDPRINTF((7, 0, "touch_handle_entry_p return dbase=%d", dbase));
  return(dbase);
}

static void rmdir_from_structures(char *unname, DIR_BASE_ENTRY *dbe)
/* is called after directory is deleted from disk, frees some structures */
{
   int k=count_dsh;
   int   dev   = dbe->nwpath.statb.st_dev;
   ino_t inode = dbe->nwpath.statb.st_ino;
   free_nw_ext_inode(dbe->nwpath.volume, unname, dev, inode);
   while (k--) {
     DIR_SEARCH_HANDLE *dsh=dir_search_handles[k];
     if (dsh && dsh->inode == inode && dsh->dev == dev) {
       xfree(dir_search_handles[k]);
       if (k+1 == count_dsh)
          count_dsh--;
     }
   }
   del_dbe_from_disk(dbe);
   free_dbe_p(dbe);
}


static int get_comp_pathes_size(NW_HPATH *nwp, uint8 *pp_pathes)
/* returns size of path components in bytes */
{
  int     k         = -1;
  int     size      =  0;
  while (++k < nwp->components) {
    int   len = (int) *(pp_pathes++);
    pp_pathes+= len;
    size     += len;
    size++;
  }
  return(size);
}

static int add_hpath_to_nwpath(N_NW_PATH *nwpath,
                               NW_HPATH *nwp, uint8 *pp_pathes)
/*
 * Routine adds nwp  to  nwpath
 * nwpath must be filled correctly before entry
 * return > -1 if ok
 */
{
  int     result    =  0;
  int     k         = -1;
  int     npbeg     = strlen(nwpath->path);
  uint8  *pp        = nwpath->path+npbeg;
#if 0
  int    perhaps_inode_mode = 0;
#endif
  XDPRINTF((2, 0, "entry add_hpath_to_nwpath: %s",
                    debug_nwpath_name(nwpath)));
  while (!result && ++k < nwp->components) {
    int   len = (int) *(pp_pathes++);
    uint8 *p  =         pp_pathes;
    pp_pathes+=len;
    if (!len) {  /* this means '..' ! */
      if (pp > nwpath->path) {
        pp=strrchr((char*)nwpath->path, '/');
        if (!pp)
          pp=nwpath->path;
        *pp='\0';
        npbeg = pp - nwpath->path;
        continue;
      } else {
        result=-0x9c; /* wrong path */
        goto leave_build_nwpath;
      }
    }
    if (!k && nwpath->volume == -1) { /* first component is volume */
      if ((nwpath->volume=nw_get_volume_number(p, len)) < 0)  {
        /* something not OK */
        result = nwpath->volume;
        goto leave_build_nwpath;
      }
      pp    = nwpath->path;
      *pp   = '\0';
      npbeg = 0;
    } else {  /* here is path (+ fn ) */
      int i=len;
      if (pp > nwpath->path) {  /* not the first entry */
        *pp   = '/';   /* we must add a slash */
        if (*(nwpath->path+npbeg) == '/') ++npbeg;
        *++pp = '\0';
      }

      while (i--) {
        if (*p == 0xae || *p == '.') {
          *pp++ = '.';
#if 0
          if (nwpath->namespace == NAME_DOS) {
            if (i==3 && *(p+1) == '_')
              perhaps_inode_mode++;
          }
#endif
        } else if (*p == 0xaa || *p == '*' ) {
          *pp++ = '*';
          nwpath->has_wild++;
        } else if (*p == 0xbf || *p == '?' ) {
          *pp++ = '?';
          nwpath->has_wild++;
        } else if (*p == '/' || *p == '\\') {
          *pp++ = '/';
        } else if (*p == ':') { /* extract volumename */
          int vlen = (int) (pp - nwpath->path);
          if ((nwpath->volume=nw_get_volume_number(nwpath->path, vlen)) < 0) {
            result = nwpath->volume;
            goto leave_build_nwpath;
          }
          pp    = nwpath->path;
          *pp   = '\0';
          npbeg = 0;
        } else *pp++ = *p;
        p++;
      }  /* while */
      *pp = '\0';
    }  /* else */
  } /* while */
  if (nwpath->volume < 0) result=-0x9c; /* wrong path */

leave_build_nwpath:
  if ((!result) && (!act_obj_id) && !(entry8_flags & 1)) {
    if (nwpath->volume)
      result = -0x9c;   /* wrong path, only volume 0 is OK */
    else {
      char *p=nwpath->path;
      char pp[10];
      while (*p=='/') ++p;
      strmaxcpy(pp, p, 6);
      up_fn(pp);
      p=pp+5;
      if (memcmp(pp, "LOGIN", 5) || (*p!='\0' && *p!='/') )
        result=-0x9c;
    }
  }

  if (!result) {
    NW_VOL *v = &nw_volumes[nwpath->volume];
    if (nwpath->namespace == NAME_DOS || nwpath->namespace == NAME_OS2) {
      if (v->options & VOL_OPTION_IGNCASE /* || perhaps_inode_mode*/) {
        int nplen= (int)(pp - nwpath->path) - npbeg;
        uint8 unixname[1024]; /* should be enough */
        memcpy(unixname, v->unixname, v->unixnamlen);
        strcpy(unixname+v->unixnamlen, nwpath->path);
        pp=unixname+v->unixnamlen;
        if (nwpath->namespace == NAME_OS2) {
          dos2unixcharset(pp);
          pp+=npbeg;
          mangle_os2_name(v, unixname, pp);
          if (nplen > 0) {
            unix2doscharset(pp);
            memcpy(nwpath->path+npbeg, pp, nplen);
          }
          XDPRINTF((5,0, "Mangle OS/2 unixname='%s'", unixname));
        } else if (nwpath->namespace == NAME_DOS) {
          dos2unixcharset(pp);
          pp+=npbeg;

          if (v->options & VOL_OPTION_DOWNSHIFT)
            down_fn(pp);
          else
            up_fn(pp);

          mangle_dos_name(v, unixname, pp);

          if (nplen > 0) {
            unix2doscharset(pp);
            memcpy(nwpath->path+npbeg, pp, nplen);
          }
          XDPRINTF((5,0, "Mangle DOS unixname='%s'", unixname));
        }
      } else {
        if (v->options & VOL_OPTION_DOWNSHIFT)
          down_fn(nwpath->path);
        else
          up_fn(nwpath->path);
#if 0
        if (nwpath->namespace == NAME_DOS){


        }
#endif
      }
    }
  }
  XDPRINTF((2, 0, "add_hpath_to_nwpath: result=0x%x, %s",
                  result, debug_nwpath_name(nwpath)));
  return(result);
}


static uint32 name_2_base(N_NW_PATH *nwpath, int namespace, int no_stat)
/* returns basehandle of path, or 0 if not exist !!   */
/* nwpath must be filled, namespace must be specified */
{
  uint32 basehandle=0L;
  if (no_stat || !nwp_stat(nwpath, "name_2_base")) {
    DEV_NAMESPACE_MAP dnm;
    dnm.dev       = nwpath->statb.st_dev;
    dnm.namespace = namespace;
    basehandle    = nw_vol_inode_to_handle(nwpath->volume,
                                  nwpath->statb.st_ino,
                                  &dnm);
  }
  return(basehandle);
}

static int add_dbe_entry(int namspace, int volume,
                  uint32 basehandle, uint8 *path,
                  struct stat *stb)
/* creats an dbe entry
 * returns slot on success
 * or errorcode
*/
{
  DIR_BASE_ENTRY *dbe=allocate_dbe_p(namspace);
  if (dbe) {
    dbe->nwpath.volume   = volume;
    dbe->basehandle      = basehandle;
    if (path) {
      strcpy(dbe->nwpath.path, path);
      norm_nwpath_fn(&(dbe->nwpath));
    }
    if (stb)
      memcpy(&(dbe->nwpath.statb), stb, sizeof(struct stat));
    else {
      if (nwp_stat(&(dbe->nwpath), "add_dbe_entry")){
        del_dbe_from_disk(dbe);
        free_dbe_p(dbe);
        return(-0x9b);  /* we use wrong path here */
      }
    }
    return(dbe->slot);
  }
  return(-0x96);  /* we use out of memory here */
}

static int find_base_entry(int volume, uint32 basehandle)
/* returns base_entry from volume/basehandle pair */
{
  int k=-1;
  DEV_NAMESPACE_MAP dnm;
  ino_t  ino;
  if (!basehandle) return(-0x9b);
  ino = nw_vol_handle_to_inode(volume, basehandle, &dnm);

  while (++k < anz_dbe) {
    DIR_BASE_ENTRY *e=dir_base[k];
    if ( (DIR_BASE_ENTRY*)NULL != e
      && volume  == e->nwpath.volume
      && ino     == e->nwpath.statb.st_ino
      && dnm.dev == e->nwpath.statb.st_dev)  {
      if (!nwp_stat(&(e->nwpath), "find_base_entry")) {
        return(touch_handle_entry_p(e));
      } else {  /* the path has changed, we say handle is wrong */
        free_dbe_p(e);
        return(-0x9b);
      }
    }
  }

  if (0 < ino) {
    struct stat statb;
    uint8  path[512];

    /* now we test whether it is the root of volume */
    if (ino == get_volume_inode(volume, &statb)) {
      /* its the handle of the volumes root */
      return(add_dbe_entry(dnm.namespace, volume, basehandle, NULL, &statb));
    }

    /* now we try it from disk */
    if (!get_dbe_data_from_disk(volume, dnm.dev, ino, path)) {
      /* we found it on disk */
      return(add_dbe_entry(dnm.namespace, volume, basehandle, path, NULL));
    }

  }

  XDPRINTF((1, 0, "Could not find path of vol=%d, base=0x%x, dev=0x%x, inode=%d",
        volume, basehandle, dnm.dev, ino));
  return(-0x9b);
}

static int insert_get_base_entry(N_NW_PATH *nwpath,
                           int namespace, int creatmode)
{
  uint32 basehandle = name_2_base(nwpath, namespace, 0);
  if (!basehandle && creatmode) { /* now creat the entry (file or dir) */
    int result = 0;
    char *unname = nwpath_2_unix(nwpath, 2);
    int voloptions=get_volume_options(nwpath->volume); 
    if (voloptions&VOL_OPTION_READONLY) return(-0x8a);
    if (creatmode & FILE_ATTR_DIR) {
       /* creat dir */
      if (nw_creat_node(nwpath->volume, unname, 1))
        result=-0x84;
    } else {
       /* creat file */
      if (nw_creat_node(nwpath->volume, unname, 0))
        result=-0x84;
    }
    if (result) return(result);
    basehandle = name_2_base(nwpath, namespace, 0);
  }

  if (basehandle) {
    int k=-1;
    while (++k < anz_dbe) {
      DIR_BASE_ENTRY *e=dir_base[k];
      if ( (DIR_BASE_ENTRY*)NULL != e
        && nwpath->volume       == e->nwpath.volume
        && nwpath->statb.st_ino == e->nwpath.statb.st_ino
        && nwpath->statb.st_dev == e->nwpath.statb.st_dev)  {
        if (nwp_stat(&(e->nwpath), "insert_get_base_entry")
          || strcmp(e->nwpath.path, nwpath->path)) {
         /* the path has changed, we remove this entry */
          free_dbe_p(e);
        } else {
          return(touch_handle_entry_p(e));
        }
        break;
      }
    }
    /* now i know that it's a new base entry */
    return(add_dbe_entry(namespace, nwpath->volume,
                       basehandle,  nwpath->path, &(nwpath->statb)));
  }
  return(-0xff); /* invalid path = -0x9c, -0xff no matching files */
}

static int build_base(int            namespace,
                      NW_HPATH       *nwp,
                      uint8          *pathes,
                      int            mode,
                      uint8          *rets)

/*
 * routine returns the actual dbe entry offset or
 * < 0 if error
 * if mode == 1, then last path element will be ignored
 * and will be put into the rets variable
 */

{
  int  result=0;
  N_NW_PATH  loc_nwpath;
  N_NW_PATH  *nwpath=&loc_nwpath;
  init_nwpath(nwpath, namespace);
  if (!nwp->flag) {  /* short directory handle */
    result=nw_dir_get_vol_path((int)nwp->base[0], nwpath->path);
    if (result > -1){
      nwpath->volume = result;
      result=0;
    }
    XDPRINTF((4, 0, "build_base with short_dir_handle=%d, result=0x%x",
                     (int)nwp->base[0], result));
  } else if (nwp->flag == 1) { /* basehandle */
    if (-1 < (result = find_base_entry(nwp->volume, GET_32(nwp->base)))) {
      DIR_BASE_ENTRY *e = dir_base[result];
      nwpath->volume    = nwp->volume;
      strcpy(nwpath->path, e->nwpath.path);
      result = 0;
    } else if (!GET_32(nwp->base)) {
      nwpath->volume    = nwp->volume;
      nwpath->path[0]   = '\0';
      result = 0;
    }
    XDPRINTF((4, 0, "build_base with basehandle=%ld, result=0x%x",
                     GET_32(nwp->base), result));
  } else if (nwp->flag != 0xff)
    result=-0x9c; /* wrong path */
  
  if (!result) {
    nwpath->namespace = namespace;
    if ((result = add_hpath_to_nwpath(nwpath, nwp, pathes)) > -1) {
      int voloptions=get_volume_options(nwpath->volume); 
      if ( (namespace&NAME_OS2) && !(VOL_NAMESPACE_OS2&voloptions))
        result=-0xbf;  /* invalid namespace */
      if ( (namespace&NAME_NFS) && !(VOL_NAMESPACE_NFS&voloptions))
        result=-0xbf;  /* invalid namespace */
    }
    if (result > -1) {  
      char   *pp=strrchr((char*)nwpath->path, '/');
      if (mode) {
        if (pp) {
          if (rets) strcpy(rets, pp+1);
          *(pp)=0;
          pp=strrchr((char*)nwpath->path, '/');
        } else {
          if (rets) strcpy(rets, nwpath->path);
          *(nwpath->path) = '\0';
        }
      }
      nwpath->fn = (pp) ? (uint8*)pp+1 : nwpath->path;
      result = insert_get_base_entry(nwpath, namespace, 0);
    }
  }
  return(result);
}

static int build_dos_name(DIR_BASE_ENTRY *e, uint8 *fname)
{
  uint8 *ss=e->nwpath.fn;
  int   len=0;
  int   pf=0;
  int   is_ok=1;
  int   options=get_volume_options(e->nwpath.volume);
  for (; *ss; ss++){
    if (*ss == '.') {
      if (pf++) {  /* no 2. point */
        is_ok=0;
        break;
      }
      len=0;
    } else {
      ++len;
      if ((pf && len > 3) || len > 8) {
        is_ok=0;
        break;
      }
      if (!(options & VOL_OPTION_IGNCASE)){
        if (options & VOL_OPTION_DOWNSHIFT){   /* only downshift chars */
          if (*ss >= 'A' && *ss <= 'Z') {
            is_ok=0;
            break;
          }
        } else {            /* only upshift chars   */
          if (*ss >= 'a' && *ss <= 'z') {
            is_ok=0;
            break;
          }
        }
      }
    }
  }
  if (is_ok) {
    strcpy(fname, e->nwpath.fn);
    up_fn(fname);
    return(strlen(fname));
  } else {
    return(sprintf(fname, "%ld.___", (long)e->nwpath.statb.st_ino));
  }
}

#if 0
typedef struct {
  int            anz;
  DIR_BASE_ENTRY **ee;
} BASE_COMPONENTS;

static BASE_COMPONENTS *get_path_components(DIR_BASE_ENTRY *dbe)
{
  BASE_COMPONENTS *be=(BASE_COMPONENTS*)xcmalloc(sizeof(BASE_COMPONENTS));
  uint8 *p=dbe->nwpath.path;
  uint8 *lastpp = p+strlen(p);
  int   len=0;
  int   k=0;
  int   done=(lastpp==dbe->nwpath.path ||
             (lastpp==dbe->nwpath.path+1 && *(dbe->nwpath.path)== '/' ));


  result = insert_get_base_entry(nwpath, namespace, 0);



  int l;
  while (!done) {
    uint8 *pp=lastpp;
    while (pp > dbe->nwpath.path && *pp!='/') --pp;
    if (*pp != '/') {
      done++;
      l = lastpp-pp;
    } else {
      pp++;
      l = lastpp-pp;
      lastpp=pp-1;
    }
    if (!k++) {


    } else {

    }


    if (l > 255)
       return(-0xff);
    len+=(l+1);
    *(p++) = (uint8)l;
    memcpy(p, pp, l);
    *(p+l)='\0';
    k++;
    XDPRINTF((5, 0, "component=%d, path=`%s`", k ,p));
    p+=l;
  }
  /* now as last element the volume */
  l=strlen(nw_volumes[dbe->nwpath.volume].sysname);
  len+=(l+1);
  *(p++) = (uint8)l;
  memcpy(p, nw_volumes[dbe->nwpath.volume].sysname, l);
  *(p+l)='\0';
  k++;
  XDPRINTF((5, 0, "component=%d, path=`%s`", k ,p));

  if (len > 400)
    return(-0xff); /* we support only 'short' pathnames */

}

#endif


int nw_generate_dir_path(int      namespace,
                         NW_HPATH *nwp,
                         uint8    *ns_dir_base,
                         uint8    *dos_dir_base)

/* returns Volume Number >=0  or errcode < 0 if error */
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    U32_TO_32(dbe->basehandle, ns_dir_base);  /* LOW - HIGH */
    if (namespace != NAME_DOS) {
      U32_TO_32(name_2_base(&(dbe->nwpath), NAME_DOS, 1),  dos_dir_base);
    } else {
      U32_TO_32(dbe->basehandle, dos_dir_base);
    }
    XDPRINTF((3, 0, "nw_generate_dir_path path=%s, result=%d, basehandle=0x%x",
       debug_nwpath_name(&(dbe->nwpath)), result, dbe->basehandle));
    result= dbe->nwpath.volume;
  } else {
    XDPRINTF((3, 0, "nw_generate_dir_path NOT OK result=-0x%x", -result));
  }
  return(result);
}

static int build_dir_info(DIR_BASE_ENTRY *dbe,
                          char *unixname,
                          int namespace,
                          uint32 infomask, uint8 *p)
{
  N_NW_PATH      *nwpath=&(dbe->nwpath);
  struct stat    *stb=&(nwpath->statb);
  int    result      = 76;  /* minimumsize */
  uint32 owner       = get_file_owner(stb);
  int    voloptions  = get_volume_options(nwpath->volume);

  memset(p, 0, result+2);

  if ( (!S_ISDIR(stb->st_mode))
     && (voloptions & VOL_OPTION_IS_PIPE) ) {
    (void)time(&(stb->st_mtime));
    stb->st_size  = 0x70000000|(stb->st_mtime&0xfffffff);
    stb->st_atime = stb->st_mtime;
  }


  if (infomask & INFO_MSK_DATA_STREAM_SPACE) {
    U32_TO_32(stb->st_size, p);
  }
  p  += 4;

  if (infomask & INFO_MSK_ATTRIBUTE_INFO) {
    uint32 attrib = get_nw_attrib_dword(nwpath->volume, unixname, stb);
    U32_TO_32(attrib, p);
    p      += 4;
    U16_TO_16((uint16)(attrib & 0xFFFF), p);
    p      += 2;
  } else p+=6;

  if (infomask & INFO_MSK_DATA_STREAM_SIZE) {
    U32_TO_32(stb->st_size, p);
  }
  p      +=4;

  if (infomask & INFO_MSK_TOTAL_DATA_STREAM_SIZE) {
    U32_TO_32(stb->st_size, p);
    p      +=4;
    U16_TO_16(0, p);
    p      +=2;
  } else p+=6;

  if (infomask & INFO_MSK_CREAT_INFO) {
    un_time_2_nw(stb->st_mtime, p, 0);
    p      +=2;
    un_date_2_nw(stb->st_mtime, p, 0);
    p      +=2;
    U32_TO_BE32(owner, p);  /* HI-LOW ! */
    p      +=4;
  } else  p+=8;

  if (infomask & INFO_MSK_MODIFY_INFO) {
    un_time_2_nw(stb->st_mtime, p, 0);
    p      +=2;
    un_date_2_nw(stb->st_mtime, p, 0);
    p      +=2;
    U32_TO_BE32(owner, p); /* HI-LOW ! */
    p      +=4;
    un_date_2_nw(stb->st_atime, p, 0);  /* access date */
    p      +=2;
  } else p+=10;

  if (infomask & INFO_MSK_ARCHIVE_INFO) {
    un_time_2_nw(0, p, 0);
    p      +=2;
    un_date_2_nw(0, p, 0);
    p      +=2;
    U32_TO_BE32(0, p);   /* HI-LOW */
    p      +=4;
  } else p+=8;

  if (infomask & INFO_MSK_RIGHTS_INFO) { /* eff. rights ! */
    U16_TO_16(tru_get_eff_rights(nwpath->volume, unixname, stb), p);
  }
  p      +=2;

  if (infomask & INFO_MSK_DIR_ENTRY_INFO) {
    U32_TO_32(dbe->basehandle, p);
    p      +=4;
#if 0
    U32_TO_32(dbe->basehandle, p);
#else
    U32_TO_32(name_2_base(nwpath, NAME_DOS, 1),  p);
#endif
    p      +=4;
    U32_TO_32(nwpath->volume, p);
    p      +=4;
  } else p+=12;

  if (infomask & INFO_MSK_EXT_ATTRIBUTES) {
    U32_TO_32(0, p);  /* Ext Attr Data Size */
    p      +=4;
    U32_TO_32(0, p);  /* Ext Attr Count */
    p      +=4;
    U32_TO_32(0, p);  /* Ext Attr Key Size */
    p      +=4;
  } else p+=12;

  if (infomask & INFO_MSK_NAME_SPACE_INFO){
    U32_TO_32(namespace, p);  /* using namespace */
  }
  p      +=4;

  /* ---------------------------------------------- */
  if (infomask & INFO_MSK_ENTRY_NAME) {
    result++;
    if (namespace == NAME_DOS) {
      *p=(uint8) build_dos_name(dbe, p+1);
      result += (int) *p;
    } else {
      *p = (uint8) strlen(nwpath->fn);
      if (*p) {
        memcpy(p+1, nwpath->fn, (int) *p);
        result += (int) *p;
      }
    }
  }

  XDPRINTF((3, 0, "build_d_i:space=%d, path=%s, result=%d, handle=0x%x, mask=0x%lx",
          namespace, debug_nwpath_name(nwpath), result, dbe->basehandle, infomask));
  return(result);
}


int nw_optain_file_dir_info(int namespace,    NW_HPATH *nwp,
                            int destnamspace,
                            int searchattrib, uint32 infomask,
                            uint8 *responsedata)
/* returns sizeof info_mask
 * the sizeof info_mask is NOT related by the infomask.
 * But the _valid_ info is.
 * must return -0xff for file not found  !
 */
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    char *unixname = alloc_nwpath2unix(&(dbe->nwpath), 2);
    nwp_stat(&(dbe->nwpath), "nw_optain_file_dir_info");
    result = build_dir_info(dbe, unixname, destnamspace, infomask, responsedata);
    xfree(unixname);
  } else {
    XDPRINTF((3, 0, "nw_optain_file_dir_info NOT OK result=-0x%x",
           -result));
  }
  return(result);
}

static int nsp_get_eff_rights(int namespace,    NW_HPATH *nwp,
                            int destnamspace,
                            int searchattrib, uint32 infomask,
                            uint8 *responsedata)
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    char *unixname = alloc_nwpath2unix(&(dbe->nwpath), 2);
    nwp_stat(&(dbe->nwpath), "nsp_get_eff_rights");
    U16_TO_16(tru_get_eff_rights(dbe->nwpath.volume, unixname,  
                                &(dbe->nwpath.statb)), responsedata);
    responsedata+=2;
    result = 2+build_dir_info(dbe, unixname, destnamspace, infomask, responsedata);
    xfree(unixname);
  } else {
    XDPRINTF((3, 0, "nsp_get_eff_rights NOT OK result=-0x%x",
           -result));
  }
  return(result);
}

static int nw_init_search(int       namespace,
                          NW_HPATH *nwp,
                          uint8    *pathes,
                          uint8    *responsedata)
{
  int result = build_base(namespace, nwp, pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    result = S_ISDIR(dbe->nwpath.statb.st_mode) ? 0 : -0xff;
    if (result > -1) {
      *responsedata++ = dbe->nwpath.volume;
      U32_TO_32(dbe->basehandle, responsedata);
      responsedata+=4;
      memset(responsedata, 0xff, 4); /* INIT sequence */
      result     = 9;
    }
    XDPRINTF((3, 0, "nw_init_search path=%s, result=%d, basehandle=0x%x",
       debug_nwpath_name(&(dbe->nwpath)), result, dbe->basehandle));
  } else {
    XDPRINTF((3, 0, "nw_init_search NOT OK result=%d", result));
  }
  return(result);
}


static int get_add_new_entry(DIR_BASE_ENTRY *qbe, int namespace,
                      uint8 *path, int creatmode)
{
  N_NW_PATH      nwpath;
  nwpath.volume     = qbe->nwpath.volume;
  nwpath.namespace  = namespace;
  strcpy(nwpath.path, qbe->nwpath.path);
  nwpath.fn = nwpath.path+strlen(nwpath.path);
  if (nwpath.fn > nwpath.path && *(nwpath.fn-1) != '/') {
    *(nwpath.fn)    = '/';
    *(++nwpath.fn)  = '\0';
  }
  strcpy(nwpath.fn, path);
  return(insert_get_base_entry(&nwpath, namespace, creatmode));
}

/* the new fn_dos_match routine from Andrew do not work ok
 * for namespace dos searches, because namespace searches do
 * something like  "0xff *"  to match all files.
 * or "0xff ? 0xff ? ... 0xff ? . 0xff ? 0xff ? 0xff ?" to match
 * 12345678.abc
 */

static int x_str_match(uint8 *s, uint8 *p, int soptions)
{
  uint8    pc, sc;
  uint     state = 0;
  uint8    anf, ende;
  int      not = 0;
  uint     found = 0;
  while ( (pc = *p++) != 0) {

    if (state != 100) {
      if (pc == 255 &&  (*p == '*' || *p == '?'
                      || *p==0xaa  || *p==0xae || *p==0xbf || *p=='.')) {
        pc=*p++;
      }

      switch  (pc) {
        case 0xaa:   pc='*';  break;
        case 0xae:   pc='.';  break;
        case 0xbf:   pc='?';  break;
      }
    }

    switch (state){
      case 0 :
      switch  (pc) {
        case '\\':  /* any following char */
                    if (*p++ != *s++) return(0);
                    break;

        case '?' :  if (!*s ||  (sc = *s++) == '.')
                      state = 10;
                    break;

        case  '.' : if (*s && pc != *s++) return(0);
                    break;

	case '*' : if (!*p) return(1); /* last star */
	           while (*s) {
	             if (x_str_match(s, p, soptions) == 1) return(1);
                     else if (*s == '.') return(0);
	             ++s;
	           }
                   state = 30;
                   break;

        case '[' :  if ( (*p == '!') || (*p == '^') ){
                       ++p;
                       not = 1;
                     }
                     state = 100;
                     continue;

        default  :  /* 'normal' chars */
                    if (soptions & VOL_OPTION_IGNCASE) {
                       if (!dfn_imatch(*s, pc))
                        return(0);
                    } else if (pc != *s) return(0);
                    s++;
                    break;

      }  /* switch */
      break;

      case 10 :  if (pc != '*' && pc != '?' ) {
                   if (pc == '.')
                     state = 0;
                   else return(0);
                 }
                 break;

      case 30:   if (pc != '.') return(0);
                 state = 31;
                 break;

      case 31:   if (pc != '*' ) return(0);
                 break;

      case   100  :   /*  Bereich von Zeichen  */
        sc = *s++;
        found = not;
        if (!sc) return(0);
        do {
          if (pc == '\\') pc = *(p++);
          if (!pc) return(0);
          anf = pc;
          if (*p == '-' && *(p+1) != ']'){
            ende = *(++p);
            p++;
          }
          else ende = anf;
          if (found == not) { /* only if not found */
            if (anf == sc || (anf <= sc && sc <= ende))
               found = !not;
          }
        } while ((pc = *(p++)) != ']');
        if (! found ) return(0);
        not   = 0;
        found = 0;
        state = 0;
        break;

      default :  break;
    }  /* switch */
  } /* while */
  if (*s=='.' && *(s+1)=='\0') return(1);  /* I hope this is right */
  return ( (*s) ? 0 : 1);
}

static int fn_dos_match_old(uint8 *s, uint8 *p, int options)
{
  uint8 *ss=s;
  int   len=0;
  int   pf=0;
  for (; *ss; ss++){
    if (*ss == '.') {
      if (pf++) return(0); /* no 2. point */
      len=0;
    } else {
      ++len;
      if ((pf && len > 3) || len > 8) return(0);

      if (!(options & VOL_OPTION_IGNCASE)){
        if (options & VOL_OPTION_DOWNSHIFT){   /* only downshift chars */
          if (*ss >= 'A' && *ss <= 'Z') return(0);
        } else {            /* only upshift chars   */
          if (*ss >= 'a' && *ss <= 'z') return(0);
        }
      }

    }
  }
  return(x_str_match(s, p, options));
}


static int namespace_fn_match(uint8 *s, uint8 *p, int namespace)
/* for *OTHER* namespaces than DOS + OS2 */
{
  int  pc, sc;
  uint state = 0;
  int  anf, ende;
  int  not = 0;
  uint found = 0;
  while ( (pc = *p++) != 0) {
    switch (state){
      case 0 :
      if (pc == 255) {
         switch (pc=*p++) {
           case 0xaa :
           case '*'  : pc=3000; break; /* star  */

           case 0xae :
           case '.'  : pc=1000; break; /* point */

           case 0xbf :
           case '?'  : pc=2000; break; /*  ? */

           default   : pc=*(--p);
                       break;
         }
      } else if (pc == '\\') continue;

      switch  (pc) {
        case 1000: if ('.' != *s++) return(0);
                    break;

        case 2000: if (!*s++) return(0);
                    break;

        case 3000 : if (!*p) return(1);
                   while (*s){
                     if (namespace_fn_match(s, p, namespace) == 1) return(1);
                     ++s;
                   }
                   return(0);

        case '[' :  if ( (*p == '!') || (*p == '^') ){
                       ++p;
                       not = 1;
                     }
                     state = 1;
                     continue;

        default  :  if ( pc != *s )
                      return(0);
                    ++s;
                    break;

      }  /* switch */
      break;

      case   1  :   /*  Bereich von Zeichen  */
        sc = *s++;
        found = not;
        if (!sc) return(0);
        do {
          if (pc == '\\') pc = *(p++);
          if (!pc) return(0);
          anf = pc;
          if (*p == '-' && *(p+1) != ']'){
            ende = *(++p);
            p++;
          }
          else ende = anf;
          if (found == not) { /* only if not found */
            if (anf == sc || (anf <= sc && sc <= ende))
               found = !not;
          }
        } while ((pc = *(p++)) != ']');
        if (! found ) return(0);
        not   = 0;
        found = 0;
        state = 0;
        break;

      default :  break;
    }  /* switch */
  } /* while */
  return ( (*s) ? 0 : 1);
}


static int search_match(struct dirent *dirbuff,
                        int   vol_options,
                        int   namespace,
                        int   inode_search,  /* do we search an inode */
                        uint8 *entry,
                        int   searchattrib,
                        uint8 *dname,
                        DIR_SEARCH_STRUCT *ds)

/* returns 1 if match, 0= no match */
{
  int   flag=0;
  if (dirbuff->d_ino && strlen(dirbuff->d_name) < 256) {
    uint8 *name=(uint8*)(dirbuff->d_name);
    struct stat    statb;
    if (namespace == NAME_DOS || namespace == NAME_OS2) {
      strcpy(dname, name);
      unix2doscharset(dname);
    } else
      strcpy(dname, name);
    XDPRINTF((8,0,"search_match, Name='%s' dname='%s'", name, dname));
    if (!inode_search) {
      if (namespace == NAME_DOS) {
        flag = (*name != '.' &&  fn_dos_match_old(dname, entry, vol_options));
      } else if (namespace == NAME_OS2) {
        flag = (*name != '.' || (*(name+1) != '.' && *(name+1) != '\0' ))
               && fn_os2_match(dname, entry, vol_options);
      } else {
        flag = (!strcmp(name, entry)
               || namespace_fn_match(name, entry, namespace));
      }
    } else
      flag = (dirbuff->d_ino == inode_search);

    if (flag) {
      strcpy(ds->kpath, name);
      XDPRINTF((7,0,"search_match, Name found=%s unixname=%s",
                                         name, ds->unixname));
      if (!stat(ds->unixname, &statb)) {
        flag = (searchattrib & W_SEARCH_ATTR_ALL) == W_SEARCH_ATTR_ALL;
        if (!flag) {
          if (S_ISDIR(statb.st_mode))
            flag=(searchattrib & FILE_ATTR_DIR);
          else
            flag = !(searchattrib & FILE_ATTR_DIR);
        }
        if (!flag) {
          XDPRINTF((10, 0, "type = %s not ok searchattrib=0x%x",
           S_ISDIR(statb.st_mode) ? "DIR" :"FILE" ,searchattrib));
        }
      } else {
        XDPRINTF((2,0,"search_match: stat error fn='%s'",
                        ds->unixname));
      }
      *(ds->kpath) = '\0';
    }
  }
  return(flag);
}

int nw_search_file_dir(int namespace, int datastream,
                       uint32 searchattrib, uint32 infomask, int *count,
                       int volume, uint32 basehandle, uint32 *psequence,
                       int len, uint8  *path, uint8 *info, int *perhaps_more)

{
#if 0
  int max_counts = *count;
#endif
  int result     = find_base_entry(volume, basehandle);
  DIR_BASE_ENTRY    *dest_dbe=NULL;
  DIR_SEARCH_HANDLE *dsh=NULL;
  DIR_BASE_ENTRY    *dbe=NULL;
  uint8  *unixname      =NULL;
  int sequence;
  *perhaps_more  = 0;
  *count         = 0;

  MDEBUG(D_FN_SEARCH, {
    char fname[300];
    strmaxcpy(fname, path, min(sizeof(fname)-1, len));
    xdprintf(1,0,"nwsfd:sequence=%d,%d, base=%d, attrib=0x%x, fn=`%s`",
           *psequence & 0xff, *psequence >> 8, basehandle,
           searchattrib, fname);
  })

  if (len > 255) result=-0x9c; /* we say wrong path here */
  
  if (result > -1) {
    dbe=dir_base[result];
    unixname   = (uint8*)alloc_nwpath2unix_big(&(dbe->nwpath), 2);
    if (tru_eff_rights_exists(dbe->nwpath.volume, unixname,
          &(dbe->nwpath.statb), TRUSTEE_F)) 
      result=-0xff;  /* no search rights */
  }
  if (result > -1) {
    dbe=dir_base[result];
    if (*psequence == MAX_U32) {
      *psequence=new_search_handle(dbe->nwpath.statb.st_dev,
                                   dbe->nwpath.statb.st_ino);
      MDEBUG(D_FN_SEARCH, {
        xdprintf(1,0,"nwsfd:got new search seqence=%d", *psequence);
      })
    }
    sequence    = *psequence >> 8;
    *psequence &= 0xff;

    if (*psequence < count_dsh) {
      dsh=dir_search_handles[*psequence];
      if (dsh && (dbe->nwpath.statb.st_dev != dsh->dev
        || dbe->nwpath.statb.st_ino != dsh->inode))
       dsh=NULL;
    }

    if (!dsh) {
      *psequence=new_search_handle(dbe->nwpath.statb.st_dev,
                                   dbe->nwpath.statb.st_ino);
      *psequence &= 0xff;
      dsh=dir_search_handles[*psequence];
    }
  }

  if (NULL != dsh) {
    DIR_SEARCH_STRUCT *ds=(DIR_SEARCH_STRUCT*) xcmalloc(sizeof(DIR_SEARCH_STRUCT));
    ds->unixname   = unixname;
    if (NULL == (ds->fdir = opendir(ds->unixname)) ) {
      seteuid(0);
      ds->fdir=opendir(ds->unixname);
      reseteuid();
    }
    if (NULL != ds->fdir) {
      uint8          entry[257];
      uint8          *pe=entry;
      int            have_wild=0; /* do we have a wildcard entry */
      int            inode_search=0;
      uint8          *is_ap=NULL; /* one after point */
      struct dirent  *dirbuff=NULL;
      int            dest_entry=-1;
      int vol_options  = get_volume_options(volume);
      ds->kpath        = ds->unixname+strlen(ds->unixname);
      *(ds->kpath)     = '/';
      *(++(ds->kpath)) = '\0';

      dbe->locked++;   /* lock dbe */
      dsh->idle=0;     /* touch dsh */

      while (len--) {
        uint8 c=*path++;
        *pe++=c;
        if (!have_wild) {
          if (c==0xff) {
            if (*path == '?' || *path == '*'
                     || *path == 0xae || *path == 0xbf || *path==0xaa)
              have_wild++;
          } else if (c == '.') is_ap=pe;
        }
      }
      *pe='\0';

      if ((!have_wild) && is_ap && pe - is_ap == 3 && *is_ap== '_'
        && *(is_ap+1) == '_'  && *(is_ap+2) == '_') {
        *(is_ap -1) = '\0';
        inode_search=atoi(entry);
        *(is_ap -1) = '.';
      }

      if ( (namespace == NAME_DOS || namespace == NAME_OS2)
          && !(vol_options & VOL_OPTION_IGNCASE) )  {
        if (vol_options & VOL_OPTION_DOWNSHIFT) {
          down_fn(entry);
        } else {
          up_fn(entry);
        }
      }

      XDPRINTF((5,0,"nw_s_f_d namsp=%d, sequence=%d, search='%s'",
                        namespace, sequence, entry ));

      if ( (!sequence) || (sequence != dsh->lastsequence)) {
        XDPRINTF((5, 0, "dirpos set to 0 dsh->lastsequence = %d, dirpos=%d",
          dsh->lastsequence, dsh->dirpos));
        dsh->dirpos   = (off_t)0;
      }

      if (dsh->dirpos) {
        SEEKDIR(ds->fdir, dsh->dirpos);
        if ((    (dirbuff=readdir(ds->fdir)) == NULL
              || (dirbuff->d_ino != dsh->found_inode))
                 && dsh->found_inode>0) {
          SEEKDIR(ds->fdir, 0L);
          XDPRINTF((5, 0, "dirbuff->d_ino %d != dsh->found_inode=%d ",
                      (dirbuff)?dirbuff->d_ino:0, dsh->found_inode));
          dsh->lastsequence=0;
          while((dirbuff=readdir(ds->fdir)) != NULL
                 && dirbuff->d_ino != dsh->found_inode)
            dsh->lastsequence++;
        }
        if (!dirbuff)  /* inode not found */
          SEEKDIR(ds->fdir, 0L);
      }
      if (dirbuff==NULL) {
        dsh->lastsequence=0;
        while ((dirbuff=readdir(ds->fdir)) != NULL
               && dsh->lastsequence < sequence)
          dsh->lastsequence++;
      }

      while (dirbuff!=NULL) {
        uint8 dname[257];
        if (search_match( dirbuff,
                          vol_options,
                          namespace,
                          inode_search,
                          entry,
                          searchattrib,
                          dname,
                          ds)) {
          if ((dest_entry = get_add_new_entry(dbe, namespace, dname, 0)) > -1)
            break;
          else {
            XDPRINTF((2, 0, "nw_search_file_dir:Cannot add entry '%s'", entry));
          }
        }  /* if */
        dirbuff = readdir(ds->fdir);
        dsh->lastsequence++;
      } /* while  */

      *(ds->kpath) = '\0';

      if (dest_entry > -1) {
        char *funixname;
        dest_dbe=dir_base[dest_entry];
        funixname = alloc_nwpath2unix(&(dbe->nwpath), 2);

        (void) nwp_stat(&(dest_dbe->nwpath), "nw_search_file_dir");
        result = build_dir_info(dest_dbe, funixname, datastream,
                         infomask |INFO_MSK_NAME_SPACE_INFO,
                         info);
        xfree(funixname);
        *count=1;
        dsh->dirpos = TELLDIR(ds->fdir);
        dirbuff     = readdir(ds->fdir);
        dsh->found_inode=0;
        *perhaps_more=0;
        dsh->lastsequence++;  /* set to next */
        while (dirbuff) {
          uint8 dname[257];
          if (search_match( dirbuff,
                          vol_options,
                          namespace,
                          inode_search,
                          entry,
                          searchattrib,
                          dname,
                          ds)) {
            *perhaps_more=0xff;
            dsh->found_inode=dirbuff->d_ino;
            break;
          }
          dsh->dirpos = TELLDIR(ds->fdir);
          dirbuff     = readdir(ds->fdir);
          dsh->lastsequence++;
        } /* while */
        *psequence |= (dsh->lastsequence << 8);
        if (!*perhaps_more) dsh->idle=MAX_I32-4;
        XDPRINTF((5, 0, "more=%d, sequence = 0x%x, file=%s,next=%s",
             *perhaps_more, *psequence, dest_dbe->nwpath.path,
             dirbuff?dirbuff->d_name:""));
      } else {
        dsh->idle   = MAX_I32-2;
        dsh->dirpos = (off_t)0;
        result=-0xff; /* no files matching */
        XDPRINTF((5, 0, "no files matching"));
      }
      dbe->locked=0;
      closedir(ds->fdir);
    } else {  /* if NULL != ds->fdir */
      result=-0xff;    /* thanks Peter Gerhard  :) */
      XDPRINTF((5, 0, "could not opendir=`%s`", ds->unixname));
    }
    xfree(ds);
  } else if (result > -1) result=-0xff;
  MDEBUG(D_FN_SEARCH, {
    char fname[300];
    strmaxcpy(fname, path, min(sizeof(fname-1), len));
    xdprintf(1, (result>-1)?2:0,"nwsfd:result=0x%x,more=%d ", result, *perhaps_more);
    if (result>-1) {
      xdprintf(1, 1,"found='%s'", (dest_dbe) ? (char*) (dest_dbe->nwpath.path) : "" );
    }
  })
  xfree(unixname);
  return(result);
}

static int nw_open_creat_file_or_dir(
                   int namespace,
                   int opencreatmode,
                   int attrib, uint32 infomask,
                   uint32 creatattrib,
                   int access_rights,
                   NW_HPATH *nwp, uint8 *pathes,
                   uint8    *responsedata,
                   int task)
{
  int   result = build_base(namespace, nwp, pathes, 0, NULL);
  int   exist  = result;
  uint8 last_part[258];
  *last_part='\0';
  if (result == -0xff && (opencreatmode & (OPC_MODE_CREAT|OPC_MODE_REPLACE))) {
    /* do not exist */
    result = build_base(namespace, nwp, pathes, 1, last_part);
    XDPRINTF((5, 0, "nw_open_c... result=%d, last_part='%s'",
                result, last_part));
    if (result > -1)
      result = get_add_new_entry(dir_base[result], namespace, last_part,
           (creatattrib & FILE_ATTR_DIR) ? FILE_ATTR_DIR : 1);
  }
  if (result > -1) {
    uint32 fhandle=0L;
    int    actionresult=0;
    DIR_BASE_ENTRY *dbe=dir_base[result];
    if (exist < 0) actionresult |= OPC_ACTION_CREAT;
    if (!(creatattrib & FILE_ATTR_DIR)) {
      int creatmode=0; /* open */
      int attrib=0;
      if (opencreatmode & (OPC_MODE_OPEN | OPC_MODE_CREAT| OPC_MODE_REPLACE) ) {
        if (opencreatmode & OPC_MODE_CREAT) {
#if 0
          if (exist > -1 && !(opencreatmode & OPC_MODE_REPLACE))
            creatmode=2;
          else
#endif
          creatmode = 1;
        }

        if ((result = file_creat_open(dbe->nwpath.volume,
              nwpath_2_unix(&dbe->nwpath, 2), &(dbe->nwpath.statb),
              attrib, access_rights, creatmode, task)) > -1) {
          fhandle = (uint32) result;
#if 1    /* should be ok, 31-Jul-97 0.99.pl1 */
          actionresult |= OPC_ACTION_OPEN;  /* FILE OPEN */
#endif
          if (exist > -1 && (opencreatmode & OPC_MODE_REPLACE))
              actionresult |= OPC_ACTION_REPLACE;  /* FILE REPLACED */
        }
      } else
        result=-0xff;
    } else if
      (exist  > -1) result=-0x84;

    if (result > -1) {
      char *unixname = alloc_nwpath2unix(&(dbe->nwpath), 2);
      U32_TO_32(fhandle, responsedata);
      responsedata += 4;
      *responsedata     =(uint8) actionresult;
      *(responsedata+1) = 0;
      responsedata+=2;
      result = 6 + build_dir_info(dbe, unixname, namespace, infomask, responsedata);
      xfree(unixname);
    }
  }
  XDPRINTF((3, 0, "nw_open_creat mode=0x%x, creatattr=0x%x, access=0x%x, attr=0x%x, result=%d",
       opencreatmode, creatattrib, access_rights, attrib, result));
  return(result);
}

typedef struct {
  int         searchattrib;
  uint8       *ubuf;   /* userbuff */
} FUNC_SEARCH;

static int func_search_entry(DIR_BASE_ENTRY *dbe, int namespace,
    uint8 *path, int len, int searchattrib,
    int (*fs_func)(DIR_BASE_ENTRY *dbe, FUNC_SEARCH *fs), FUNC_SEARCH *fs)
{
  int result=-0xff;
  FUNC_SEARCH    fs_local;
  if (!fs) {
    fs        = &fs_local;
    fs->ubuf  = NULL;
  }
  fs->searchattrib = searchattrib;
  if (!len && ( !dbe->nwpath.path[0] 
            || (dbe->nwpath.path[0]=='/' && dbe->nwpath.path[1]=='\0'))) {
    /* volume path */
    if (searchattrib & (W_SEARCH_ATTR_DIR | FILE_ATTR_DIR)) {
      int res = (*fs_func)(dbe, fs);
      if (res < 0) result=res;
      else result=0;
    }
  } else {
    DIR_SEARCH_STRUCT *ds=(DIR_SEARCH_STRUCT*) xcmalloc(sizeof(DIR_SEARCH_STRUCT));
    ds->unixname     = (uint8*)alloc_nwpath2unix_big(&(dbe->nwpath), 2);
    if (!tru_eff_rights_exists(dbe->nwpath.volume, ds->unixname,
            &(dbe->nwpath.statb), TRUSTEE_F)) {
      if (NULL == (ds->fdir = opendir(ds->unixname)) ) {
        seteuid(0);
        ds->fdir = opendir(ds->unixname);
        reseteuid();
      }
    } else ds->fdir=NULL;
    if (NULL != ds->fdir) {
      uint8          entry[257];
      uint8          *pe=entry;
      int            have_wild    = 0; /* do we have a wildcard entry */
      int            inode_search = 0;
      uint8          *is_ap   = NULL;      /* one after point */
      struct dirent  *dirbuff = NULL;
      int vol_options  = get_volume_options(dbe->nwpath.volume);
      ds->kpath        = ds->unixname+strlen(ds->unixname);
      *(ds->kpath)     = '/';
      *(++(ds->kpath)) = '\0';
      dbe->locked++;   /* lock dbe */

      while (len--) {
        uint8 c=*path++;
        *pe++=c;
        if (!have_wild) {
          if (c==0xff) {
            if (*path == '?' || *path == '*'
                     || *path == 0xae || *path == 0xbf || *path==0xaa)
              have_wild++;
          } else if (c == '.') is_ap=pe;
        }
      }
      *pe='\0';

      if ((!have_wild) && is_ap && pe - is_ap == 3 && *is_ap== '_'
        && *(is_ap+1) == '_'  && *(is_ap+2) == '_') {
        *(is_ap -1) = '\0';
        inode_search=atoi(entry);
        *(is_ap -1) = '.';
      }

      if ( (namespace == NAME_DOS || namespace == NAME_OS2)
          && !(vol_options & VOL_OPTION_IGNCASE) )  {
        if (vol_options & VOL_OPTION_DOWNSHIFT) {
          down_fn(entry);
        } else {
          up_fn(entry);
        }
      }

      while (NULL != (dirbuff=readdir(ds->fdir))) {
        uint8 dname[257];
        if (search_match( dirbuff,
                          vol_options,
                          namespace,
                          inode_search,
                          entry,
                          searchattrib,
                          dname,
                          ds)) {
          int dest_entry = get_add_new_entry(dbe, namespace, dname, 0);
          if (dest_entry > -1) {
            int res = (*fs_func)(dir_base[dest_entry], fs);
            if (res < 0) {
              result=res;
              break;
            } else
              result=0;
          } else {
            XDPRINTF((2, 0, "func_search_entry:Cannot add entry '%s'", entry));
          }
        }
      } /* while  */
      *(ds->kpath) = '\0';
      dbe->locked=0;
      closedir(ds->fdir);
    } else {  /* if NULL != ds->fdir */
      XDPRINTF((5, 0, "func_search_entry:could not opendir=`%s`", ds->unixname));
    }
    xfree(ds->unixname);
    xfree(ds);
  }
  return(result);
}

static int delete_file_dir(DIR_BASE_ENTRY *dbe, FUNC_SEARCH *fs)
/* callbackroutine */
{
  uint8  *unname=(uint8*)nwpath_2_unix(&(dbe->nwpath), 2);
  int    result=nw_unlink_node(dbe->nwpath.volume, unname,&dbe->nwpath.statb);
  if (!result) {
    if (S_ISDIR(dbe->nwpath.statb.st_mode)) /* directory */
      rmdir_from_structures(unname, dbe);
    else  /* file */
      free_dbe_p(dbe);
  }
  return(result);
}

static int nw_delete_file_dir(int namespace, int searchattrib,
                               NW_HPATH *nwp)
{
  uint8 search_entry[258];
  int result = build_base(namespace, nwp, nwp->pathes, 1, search_entry);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    if (get_volume_options(dbe->nwpath.volume) &
       VOL_OPTION_READONLY) result = -0x8a;
    else result=func_search_entry(dbe, namespace,
          search_entry, strlen(search_entry), searchattrib,
          delete_file_dir, NULL);
  }
  return(result);
}

static int nw_alloc_short_dir_handle(int namespace, int hmode,
                               NW_HPATH *nwp, int task, int *volume)
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    if (S_ISDIR(dbe->nwpath.statb.st_mode)) {
      result=xinsert_new_dir(dbe->nwpath.volume, dbe->nwpath.path,
           dbe->nwpath.statb.st_dev, dbe->nwpath.statb.st_ino,
           300, hmode, task);
      *volume=dbe->nwpath.volume;
    } else result=-0xff;
  }
  return(result);
}

static int nw_set_short_dir_handle(int namespace, int desthandle,
                                     NW_HPATH *nwp, int task)
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    if (S_ISDIR(dbe->nwpath.statb.st_mode)) {
      result=alter_dir_handle(desthandle,
              dbe->nwpath.volume, dbe->nwpath.path,
         dbe->nwpath.statb.st_dev, dbe->nwpath.statb.st_ino, task);
    } else result=-0x9c; /* wrong path */
  }
  return((result > 0) ? 0 : result);
}

typedef struct {
  int count; /* count oic */
  NW_OIC *nwoic;
} S_ADD_TRUSTEE_SET;

static int add_trustee_set(DIR_BASE_ENTRY *dbe, FUNC_SEARCH *fs)
/* callbackroutine */
{
  uint8  *unname=(uint8*)nwpath_2_unix(&(dbe->nwpath), 2);
  S_ADD_TRUSTEE_SET *st=(S_ADD_TRUSTEE_SET*)fs->ubuf;
  return(tru_add_trustee_set(dbe->nwpath.volume, unname,
                            &(dbe->nwpath.statb),
                            st->count, st->nwoic));
}

static int nw_add_trustee_set(int namespace, int searchattrib,
                                     NW_HPATH *nwp,
                                     int trustee_rights,
                                     int count, uint8 *oic)
{
  uint8 search_entry[258];
  int result = build_base(namespace, nwp, nwp->pathes, 1, search_entry);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    if (get_volume_options(dbe->nwpath.volume) &
       VOL_OPTION_READONLY) result = -0x8a;
    else {
       FUNC_SEARCH fs;
       S_ADD_TRUSTEE_SET st;
       NW_OIC *nwoic=st.nwoic=(NW_OIC*)xmalloc(sizeof(NW_OIC)*count);
       st.count=0;
       fs.ubuf=(uint8*)&st;
       while (count--) {
         nwoic->id=GET_BE32(oic);
         oic += 4;
         if (trustee_rights == -1)
           nwoic->trustee=GET_16(oic);
         else
           nwoic->trustee=trustee_rights;
         oic+=2;
         if (nwoic->id) {
           st.count++;
           nwoic++;
         }
       }
       result  = func_search_entry(dbe, namespace,
          search_entry, strlen(search_entry), searchattrib,
          add_trustee_set, &fs);
       xfree(st.nwoic);
    }
  }
  return(result);
}

typedef struct {
  int    count; /* count oic */
  uint32 *ids;
} S_DEL_TRUSTEE_SET;

static int del_trustee_set(DIR_BASE_ENTRY *dbe, FUNC_SEARCH *fs)
/* callbackroutine */
{
  uint8  *unname=(uint8*)nwpath_2_unix(&(dbe->nwpath), 2);
  int    result=0;
  S_DEL_TRUSTEE_SET *st=(S_DEL_TRUSTEE_SET*)fs->ubuf;
  int    k=st->count;
  uint32 *ids=st->ids;
  while (k--) {
    int res=tru_del_trustee(dbe->nwpath.volume, unname,
                              &(dbe->nwpath.statb), *ids);
    if (res < 0) {
      result=res;
      break;
    }
    ids++;
  }
  return(result);
}

static int nw_del_trustee_set(int namespace, 
                                     NW_HPATH *nwp,
                                     int count, uint8 *did)
{
  uint8 search_entry[258];
  int result = build_base(namespace, nwp, nwp->pathes, 1, search_entry);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    if (get_volume_options(dbe->nwpath.volume) &
       VOL_OPTION_READONLY) result = -0x8a;
    else {
       FUNC_SEARCH fs;
       S_DEL_TRUSTEE_SET st;
       uint32 *ids=st.ids=(uint32*)xmalloc(sizeof(uint32)*count);
       st.count=0;
       fs.ubuf=(uint8*)&st;
       while (count--) {
         *ids=GET_BE32(did);
         if (*ids) {
           st.count++;
           ids++;
         }
         did+=4;
       }
       result  = func_search_entry(dbe, namespace,
          search_entry, strlen(search_entry), W_SEARCH_ATTR_ALL,
         del_trustee_set, &fs);
       xfree(st.ids);
    }
  }
  return(result);
}

static int nw_get_trustee_set(int namespace, int searchattrib,
                                     NW_HPATH *nwp,
                                     uint32 *scansequence,
                                     uint8  *oic)
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    char *unixname = alloc_nwpath2unix(&(dbe->nwpath), 2);
    uint32   ids[20];
    int trustees[20];
    nwp_stat(&(dbe->nwpath), "nw_get_trustee_set");
    result=tru_get_trustee_set(dbe->nwpath.volume, unixname,
                            &(dbe->nwpath.statb),
                      (int)*scansequence, 20, ids, trustees);
    if (result > 0) {
      int k=-1;
      while (++k < result){
        U32_TO_BE32(ids[k],    oic);
        oic += 4;
        U16_TO_16(trustees[k], oic);
        oic += 2;
      }
      (*scansequence)++;
    } 
    xfree(unixname);
  }
  return(result);
}


static int nw_get_full_path_cookies(int namespace,
                int destnamspace,
                NW_HPATH *nwp, int *cookieflags,
                uint32  *cookie1, uint32 *cookie2,
                int     *components,  uint8 *componentpath)

/* SIMPLE IMPLEMENTATION, needs more work !!!! */
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    uint8 *p      = componentpath;
    uint8 *lastpp = dbe->nwpath.path+strlen(dbe->nwpath.path);
    int   len=0;
    int   k=0;
    int   done=(lastpp==dbe->nwpath.path ||
               (lastpp==dbe->nwpath.path+1 && *(dbe->nwpath.path)== '/' ));
    int l;
    if (*cookie1 != MAX_U32 || *cookie2 != MAX_U32)
      return(-0xff);
    if (strlen(dbe->nwpath.path) > 256)
       return(-0xfb); /* we do support 'short' pathnames only */

    while (!done) {
      uint8 *pp=lastpp;
      while (pp > dbe->nwpath.path && *--pp !='/');;
      if (*pp == '/') {
        if (pp < dbe->nwpath.path + 1) ++done;
        pp++;
        l = lastpp-pp;
        lastpp = pp-1;
      } else {
        done++;
        l = lastpp-pp;
      }
      if (l > 255)
         return(-0xff);
      else if (l > 0) {
        len+=(l+1);
        *(p++) = (uint8)l;
        memcpy(p, pp, l);
        *(p+l)='\0';
        if (!namespace)
           up_fn(p);
        k++;
        XDPRINTF((5, 0, "component=%d, path=`%s`", k ,p));
        p+=l;
      } else {
        XDPRINTF((1, 0, "component=%d, l=%d", k ,l));
      }
    }
    /* now as last element the volume */
    l=strlen(nw_volumes[dbe->nwpath.volume].sysname);
    len+=(l+1);
    *(p++) = (uint8)l;
    memcpy(p, nw_volumes[dbe->nwpath.volume].sysname, l);
    *(p+l)='\0';
    k++;
    XDPRINTF((5, 0, "component=%d, path=`%s`", k ,p));

    if (len > 400)
      return(-0xff); /* we support only 'short' pathnames */

    *components  = k;
    *cookieflags = (S_ISDIR(dbe->nwpath.statb.st_mode)) ? 0 : 1;
    *cookie1     = 0;
    *cookie2     = MAX_U32; /* no more cookies */
    return(len);
  }
  return(result);
}

static int nw_rename_file_dir(int namespace,
                              NW_HPATH *nwps, uint8 *pathes_s,
                              NW_HPATH *nwpd, uint8 *pathes_d,
                              int      searchattrib,
                              int      renameflag)
{
  int result = build_base(namespace, nwps, pathes_s, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe_s = dir_base[result];
    uint8  last_part[258];
    uint8  *unname_s=
        (uint8*)alloc_nwpath2unix(&(dbe_s->nwpath), 2);
    result = build_base(namespace, nwpd, pathes_d, 1, last_part);
    if (result > -1) {
      DIR_BASE_ENTRY *dbe_d = dir_base[result];
      uint8  *unname_dp=  /* directory */
        (uint8*)alloc_nwpath2unix(&(dbe_d->nwpath), 2);
      uint8  *unname_d =  
        (uint8*)alloc_nwpath2unix_extra(&(dbe_d->nwpath), 0, last_part);

      if (tru_eff_rights_exists(dbe_s->nwpath.volume, unname_s, 
               &dbe_s->nwpath.statb, TRUSTEE_W|TRUSTEE_M|TRUSTEE_R))
        result=-0x8b;
      else if (tru_eff_rights_exists(dbe_d->nwpath.volume, unname_dp, 
               &dbe_d->nwpath.statb, TRUSTEE_W))
        result=-0x8b;
      
      if (result > -1) {
        seteuid(0);
        if (S_ISDIR(dbe_s->nwpath.statb.st_mode))
          result = unx_mvdir(unname_s,  unname_d);
        else
          result = unx_mvfile(unname_s, unname_d);
        reseteuid();
        if (result==EEXIST)
           result = -0x92;
      }
      XDPRINTF((5, 0, "Rename:%d '%s' -> '%s'", result, unname_s, unname_d));
      xfree(unname_dp);
      xfree(unname_d);
      if (!result) {
        free_dbe_p(dbe_s);
        if ((result=get_add_new_entry(dbe_d, namespace, last_part, 0)) > -1)
           result = 0;
      }
    }
    xfree(unname_s);
  }
  return(result);
}

static int nw_modify_file_dir(int namespace,
                              NW_HPATH *nwp, uint8 *pathes,
                              int searchattrib,
                               uint32 infomask, DOS_MODIFY_INFO *dmi)
{
  int result = build_base(namespace, nwp, pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    uint8 *uname=alloc_nwpath2unix(&(dbe->nwpath), 2);
    struct stat *stb = &(dbe->nwpath.statb);
    int voloptions = get_volume_options(dbe->nwpath.volume);
    if (-1 != (result=stat(uname, stb))){
      if (S_ISFIFO(stb->st_mode) || (voloptions&VOL_OPTION_IS_PIPE)){
        xfree(uname);
        return(0); /* do nothing but report OK */
      }
      if (tru_eff_rights_exists(dbe->nwpath.volume, uname, stb, 
           TRUSTEE_M))
        result=-0x8c;  /* no modify rights */
      if (infomask & DOS_MSK_ATTRIBUTE){
        result=set_nw_attrib_dword(dbe->nwpath.volume, uname, stb, 
                                   GET_32(dmi->attributes));
      }
      if (!result && (infomask & DOS_MSK_INHERIT_RIGHTS)){
        int mask       = tru_get_inherited_mask(dbe->nwpath.volume, uname, stb);
        int grantmask  = GET_16(dmi->rightsgrantmask);
        int revokemask = GET_16(dmi->rightsrevokemask);
        revokemask    &= (~TRUSTEE_S);  /* do not remove supervisory mask bit*/
        mask &= (~revokemask);
        mask |= grantmask;
        (void)tru_set_inherited_mask(dbe->nwpath.volume, uname, stb, 
                                  mask);
      }
    } else result=-0xff;
    if (!result) {  
      struct  utimbuf ut;
      int     do_utime=0;
      uint8   datetime[4];
      
      un_time_2_nw(stb->st_mtime, datetime,   1);
      un_date_2_nw(stb->st_mtime, datetime+2, 1);
      ut.actime  = stb->st_atime;
      ut.modtime = stb->st_mtime;

      XDPRINTF((5, 0, "modify datetime 1=%2x,%2x,%2x,%2x",
                  (int) *datetime,
                  (int) *(datetime+1),
                  (int) *(datetime+2),
                  (int) *(datetime+3)));

      if (infomask & DOS_MSK_MODIFY_DATE || infomask & DOS_MSK_MODIFY_TIME) {
        if (infomask & DOS_MSK_MODIFY_TIME){
           datetime[0] = dmi->modified_time[1];
           datetime[1] = dmi->modified_time[0];
        }

        if (infomask & DOS_MSK_MODIFY_DATE) {
          datetime[2] = dmi->modified_date[1];
          datetime[3] = dmi->modified_date[0];
        }
        do_utime++;
      } else if (infomask & DOS_MSK_CREAT_DATE
              || infomask & DOS_MSK_CREAT_TIME) {
        if (infomask & DOS_MSK_CREAT_TIME) {
           datetime[0] = dmi->created_time[1];
           datetime[1] = dmi->created_time[0];
        }
        if (infomask & DOS_MSK_CREAT_DATE) {
          datetime[2] = dmi->created_date[1];
          datetime[3] = dmi->created_date[0];
        }
        do_utime++;
      }
      if (do_utime) {
        XDPRINTF((5, 0, "modify datetime 2=%2x,%2x,%2x,%2x",
                  (int) *datetime,
                  (int) *(datetime+1),
                  (int) *(datetime+2),
                  (int) *(datetime+3)));

        ut.modtime = nw_2_un_time(datetime+2, datetime);
        if (-1==utime(uname, &ut)) {
          seteuid(0);
          utime(uname, &ut);
          reseteuid();
        }
      }
    }    
    xfree(uname);
  }
  return(result);
}

int handle_func_0x57(uint8 *p, uint8 *responsedata, int task)
{
  int result    = -0xfb;      /* unknown request                  */
  int ufunc     = (int) *p++;
  /* now p locates at 4 byte boundary ! */
  int namespace = (int) *p;   /* for most calls                   */
  XDPRINTF((3, 0, "0x57 call ufunc=0x%x namespace=%d", ufunc, namespace));
  switch (ufunc) {
    case  0x01 :  /* open creat file or subdir */
      {
        /* NW PATH STRUC */
        int      opencreatmode = (int)      *(p+1);
        int      attrib        = (int) GET_16(p+2);  /* LOW-HI */
        uint32   infomask      =       GET_32(p+4);  /* LOW-HI */
        uint32   creatattrib   =       GET_32(p+8);
        int      access_rights = (int) GET_16(p+12); /* LOW-HI */
        NW_HPATH nwpathstruct;
        memcpy(&nwpathstruct, p+14, sizeof(nwpathstruct));
        result  = nw_open_creat_file_or_dir(namespace, opencreatmode,
                   attrib, infomask, creatattrib, access_rights,
                   &nwpathstruct, p+21, responsedata, task);
      }
      break;


    case  0x02 :  /* Initialize Search */
      {
        /* NW PATH STRUC */
        NW_HPATH nwpathstruct;
        memcpy(&nwpathstruct, p+2, sizeof(nwpathstruct));
        result  = nw_init_search(namespace,
                                 &nwpathstruct,
                                 p+9,    /* pathes */
                                 responsedata);
      }
      break;

    case  0x03 :  /* Search for File or DIR */
      {
        struct OUTPUT {
          uint8   volume;
          uint8   basehandle[4];
          uint8   sequence[4];
          uint8   reserved;
        } *xdata= (struct OUTPUT*)responsedata;
        /* NW PATH STRUC */
#if 0
        int      datastream    = (int) *(p+1);
#endif
        int      searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        uint32   infomask      = GET_32(p+4);       /* LOW-HI */
        int      volume        = *(p+8);
        uint32   basehandle    = GET_32(p+9);       /* LOW-HI */
        uint32   sequence      = GET_32(p+13);      /* LOW-HI */
        int      len           = *(p+17);
        uint8    *path         = p+18;
        int      count = 1;
        int      more_entries;
        result  = nw_search_file_dir(namespace, namespace /*datastream*/,
                                     searchattrib, infomask, &count,
                                     volume, basehandle, &sequence,
                                     len, path,
                                     responsedata+sizeof(struct OUTPUT),
                                     &more_entries);
        if (result > -1) {
          xdata->volume = (uint8)volume;
          U32_TO_32(basehandle, xdata->basehandle);
          U32_TO_32(sequence,   xdata->sequence);
          xdata->reserved = (uint8) 0;
          result+=sizeof(struct OUTPUT);
        }
      }
      break;

    case  0x04 :  /* rename File or Dir */
      {
        int      renameflag    = *(p+1);
        int      searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        NW_HPATH nwps;
        NW_HPATH nwpd;
        int      size;
        p+=4;
        memcpy(&nwps, p, 7); p+=7;
        memcpy(&nwpd, p, 7); p+=7;
        size   = get_comp_pathes_size(&nwps, p);
        result = nw_rename_file_dir(namespace,
                  &nwps, p, &nwpd, p+size, searchattrib, renameflag);
      }
      break;

    case  0x05 :  /* Scan File/Dir for Trustees */
      {
#if 0
        int      reserved      = (int) *(p+1);
#endif        
        int      searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        uint32   scansequence  = GET_32(p+4);       /* LOW-HI */
        NW_HPATH *nwpathstruct = (NW_HPATH *) (p+8);
        result = nw_get_trustee_set(namespace, searchattrib, 
                                            nwpathstruct,
                                           &scansequence,
                                           responsedata+6);
        U32_TO_32(scansequence, responsedata);
        if (result > -1) {
          U16_TO_16(result, responsedata+4); /* count */
          result=(result*6)+6;
        } else {
          U16_TO_16(0, responsedata+4); /* count */
        }
      }
      break;


    case  0x06 :  /* Obtain File or Subdir Info */
      {
#if 0
static int code = 0;
#endif
        int      destnamspace  = (int) *(p+1);
        int      searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        uint32   infomask      = GET_32(p+4);       /* LOW-HI */
        NW_HPATH *nwpathstruct = (NW_HPATH *) (p+8);
        result = nw_optain_file_dir_info(namespace,   nwpathstruct,
                                        destnamspace,
                                        searchattrib, infomask,
                                        responsedata);
#if 0
        if (result < 0)  {
          /* Unix Client errormessages */
          /* there exist *NO* result to force client to use new */
          /* basehandles :-(( */
          /*  most are -> IO/Error */
          /*  -0x9c; -> no such file or dir */
          /*  -0xfb; -> Protokoll Error     */
          /*  -0xff; -> no such file or dir */
          if (code < 0xff) result=-(++code);
          else result=-0xfe;
       }
#endif
      }
      break;

    case  0x07 :  /* Modify File or Dir Info */
      {
        NW_HPATH         nwp;
        DOS_MODIFY_INFO  dmi;
        int      searchattrib  = (int) GET_16(p+2);
        uint32   infomask      = GET_32(p+4);       /* LOW-HI */
        p+=8;
        memcpy(&dmi, p, sizeof(DOS_MODIFY_INFO));
        p+= sizeof(DOS_MODIFY_INFO);
        memcpy(&nwp, p, 7);
        result  = nw_modify_file_dir(namespace, &nwp, p+7,
                                   searchattrib, infomask, &dmi);
      }
      break;
    
    case  0x08 :  /* Delete a File or Subdir */
      {
        int     searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        NW_HPATH *nwpathstruct = (NW_HPATH *) (p+4);
        result = nw_delete_file_dir(namespace, searchattrib, nwpathstruct);
      }
      break;

    case  0x09 : /* Set short Dir Handle*/
      {          /* nw32client needs it */
        int desthandle = (int)*(p+2);
        NW_HPATH *nwp  = (NW_HPATH *)(p+4);
        result = nw_set_short_dir_handle(namespace, desthandle, nwp, task);
        /* NO REPLY Packet */
      }
      break;

    case  0x0a : /* Add trustee set to file or dir*/
      {
        int searchattrib   = (int) GET_16(p+2);    /* LO-HI */
        int trustee_rights = (int) GET_16(p+4);    /* LO-HI */
        /* if trustee_rights == 0xffff, use trustee from OIC structure */
        int count          = (int) GET_16(p+6);    /* LO-HI */
        NW_HPATH *nwp      = (NW_HPATH *)(p+8);
        if (trustee_rights==0xffff) trustee_rights=-1;
        p+=315;
        result = nw_add_trustee_set(namespace, searchattrib, nwp,
                     trustee_rights,
                     count, p); 
        /* OIC structure { uint8 id[4]; uint8 trusttee[2]  LO-HI } */
        /* NO REPLY Packet */
      }
      break;
    
    case  0x0b : /* delete trustee set from file or dir */
      {       
        int    count  = (int) GET_16(p+2);   
        NW_HPATH *nwp = (NW_HPATH *)(p+4);
        p+=311;
        result = nw_del_trustee_set(namespace, nwp, count, p);
            /* object id's   */
        /* NO REPLY Packet */
      }
      break;

    case  0x0c : /* alloc short dir Handle */
      {
        int      hmode  = (int) GET_16(p+2); /* 0=p, 1=temp, 2=speztemp */
        NW_HPATH *nwp   = (NW_HPATH *)(p+4);
        struct OUTPUT {
          uint8   dir_handle;
          uint8   volume;
          uint8   reserved[4];
        } *xdata= (struct OUTPUT*)responsedata;
        int volume;
        result = nw_alloc_short_dir_handle(namespace, hmode, nwp, task,
                  &volume);
        if (result > -1) {
          xdata->dir_handle = (uint8) result;
          xdata->volume     = (uint8) volume;
          U32_TO_32(0L,  xdata->reserved);
          result=sizeof(struct OUTPUT);
        }
      }
      break;

    case  0x14 : /* Search for File or Subdir Set */
        /* SIMPLE IMPLEMENTATION, needs more work !!!! */
      {
        struct OUTPUT {
          uint8   volume;
          uint8   basehandle[4];
          uint8   sequence[4];
          uint8   more_entries;  /* NW4.1 (UNIX) says 0xff here */
          uint8   count[2];      /* count of entries */
        } *xdata= (struct OUTPUT*)responsedata;
#if 0
        int      datastream    = (int) *(p+1);
#endif
        int      searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        uint32   infomask      = GET_32(p+4);       /* LOW-HI */
        int      count         = (int)GET_16(p+8);
        int      volume        = *(p+10);
        uint32   basehandle    = GET_32(p+11);      /* LOW-HI */
        uint32   sequence      = GET_32(p+15);      /* LOW-HI */
        int      len           = *(p+19);
        uint8    *path         = p+20;
        int      more_entries;
        result  = nw_search_file_dir(namespace,  namespace /*datastream*/,
                                     searchattrib, infomask, &count,
                                     volume, basehandle, &sequence,
                                     len, path,
                                     responsedata+sizeof(struct OUTPUT),
                                     &more_entries);
        if (result > -1) {
          xdata->volume        = (uint8)volume;
          U32_TO_32(basehandle, xdata->basehandle);
          U32_TO_32(sequence,   xdata->sequence);
          xdata->more_entries  = (uint8) more_entries;
          U16_TO_16(count,       xdata->count);
          result+=sizeof(struct OUTPUT);
        }
      }
      break;

    case  0x15 : /* Get Path String from short dir new */
      {
        int  dir_handle=(int) *(p+1);
        result=nw_get_directory_path(dir_handle, responsedata+1);
        if (result > -1) {
          *responsedata=(uint8) result;
          result+=1;
        }
      }
      break;

    case  0x16 : /* Generate Dir BASE and VolNumber */
      {
        NW_HPATH *nwpathstruct = (NW_HPATH *) (p+4);
        struct OUTPUT {
          uint8   ns_dir_base[4];   /* BASEHANDLE */
          uint8   dos_dir_base[4];  /* BASEHANDLE */
          uint8   volume;           /* Volumenumber*/
        } *xdata= (struct OUTPUT*)responsedata;
        result = nw_generate_dir_path(namespace,
                 nwpathstruct, xdata->ns_dir_base, xdata->dos_dir_base);
        if (result >-1 ) {
          xdata->volume = result;
          result        = sizeof(struct OUTPUT);
        }
      }
      break;

    case  0x18 :  /* Get Name Spaces Loaded*/
      {
        int volume=*(p+2);
        struct OUTPUT {
          uint8   anz_name_spaces;
          uint8   name_space_list[1];
        } *xdata= (struct OUTPUT*)responsedata;
        result=get_volume_options(volume);
        xdata->anz_name_spaces = (uint8) 0;
        if (result & VOL_NAMESPACE_DOS)
          xdata->name_space_list[xdata->anz_name_spaces++]
            = (uint8) NAME_DOS;
        if (result & VOL_NAMESPACE_OS2)
          xdata->name_space_list[xdata->anz_name_spaces++]
            = (uint8) NAME_OS2;
        if (result & VOL_NAMESPACE_NFS)
          xdata->name_space_list[xdata->anz_name_spaces++]
            = (uint8) NAME_NFS;
        result=xdata->anz_name_spaces+1;
      }
      break;

    case  0x1a :  /* Get Huge NS Info new*/
      {

      }
      break;

    case  0x1c :  /* GetFullPathString new*/
      {           /* nw32 client needs it */
        /* SIMPLE IMPLEMENTATION, needs more work !!!! */
        int      destnamspace  = (int)*(p+1);
        int      cookieflags   = (int)GET_16(p+2);
        uint32   cookie1       = GET_32(p+4);
        uint32   cookie2       = GET_32(p+8);
        NW_HPATH *nwpathstruct = (NW_HPATH *)(p+12);
        int      components;
        struct OUTPUT {
          uint8   cookieflags[2];    /* 0 = DIR; 1 = FILE */
          uint8   cookie1[4];
          uint8   cookie2[4];
          uint8   pathsize[2];       /* complete  components size */
          uint8   pathcount[2];      /* component counts */
        } *xdata= (struct OUTPUT*)responsedata;
        result = nw_get_full_path_cookies(namespace, destnamspace,
                         nwpathstruct,
                          &cookieflags, &cookie1, &cookie2, &components,
                          responsedata+sizeof(struct OUTPUT));
        if (result > -1) {
          U16_TO_16(cookieflags,  xdata->cookieflags);
          U32_TO_32(cookie1,      xdata->cookie1);
          U32_TO_32(cookie2,      xdata->cookie2);
          U16_TO_16(result,       xdata->pathsize);
          U16_TO_16(components,   xdata->pathcount);
          result+=sizeof(struct OUTPUT);
        }
      }
      break;

    case  0x1d :  /* GetEffDirRights new */
      {
        int      destnamspace  = (int) *(p+1);
        int      searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        uint32   infomask      = GET_32(p+4);       /* LOW-HI */
        NW_HPATH *nwpathstruct = (NW_HPATH *) (p+8);
        result = nsp_get_eff_rights(namespace, nwpathstruct,
                                    destnamspace,
                                    searchattrib, infomask,
                                    responsedata);
      }
      break;

    default : result = -0xfb; /* unknown request */
  } /* switch */
  return(result);
}


int fill_namespace_buffer(int volume, uint8 *rdata)
{
  if (volume < used_nw_volumes) {
    int voloptions=get_volume_options(volume);
    uint8 *p=rdata;
    int count=0;

    *p++=5; /* we say 5 known namespaces (index 0=DOS .. 4=OS2 */

    /* names */
    *p++=3; memcpy(p,"DOS",  3); p+=3;
    *p++=9; memcpy(p,"MACINTOSH", 9); p+=9;
    *p++=3; memcpy(p,"NFS",  3); p+=3;
    *p++=4; memcpy(p,"FTAM", 4); p+=4;
    *p++=3; memcpy(p,"OS2",  3); p+=3;

    /* datastreams */
    *p++=3; /* we say 3 datastreams here */

    *p++=NAME_DOS;
    *p++=19; memcpy(p,"Primary Data Stream",  19);    p+=19;

    *p++=NAME_MAC;
    *p++=23; memcpy(p,"Macintosh Resource Fork", 23); p+=23;

    *p++=NAME_FTAM;
    *p++=20; memcpy(p,"FTAM Extra Data Fork", 20);    p+=20;

    if (loaded_namespaces & VOL_NAMESPACE_DOS) ++count;
    if (loaded_namespaces & VOL_NAMESPACE_OS2) ++count;
    if (loaded_namespaces & VOL_NAMESPACE_NFS) ++count;
    *p++ = count;  /* loaded namespaces */
    if (loaded_namespaces & VOL_NAMESPACE_DOS) *p++ = NAME_DOS;
    if (loaded_namespaces & VOL_NAMESPACE_OS2) *p++ = NAME_OS2;
    if (loaded_namespaces & VOL_NAMESPACE_NFS) *p++ = NAME_NFS;

    count=0;
    if (voloptions & VOL_NAMESPACE_DOS) ++count;
    if (voloptions & VOL_NAMESPACE_OS2) ++count;
    if (voloptions & VOL_NAMESPACE_NFS) ++count;
    *p++ = count;  /* volume namespaces */
    if (voloptions & VOL_NAMESPACE_DOS) *p++ = NAME_DOS;
    if (voloptions & VOL_NAMESPACE_OS2) *p++ = NAME_OS2;
    if (voloptions & VOL_NAMESPACE_NFS) *p++ = NAME_NFS;
    *p++ = 1;  /* only one datastream */
    *p++ = 0;  /* DOS datastream */
    return((int)(p-rdata));
  } else return(-0x98);
}

int get_namespace_dir_entry(int volume, uint32 basehandle,
                                   int namspace, uint8 *rdata)
{
  int result = find_base_entry(volume, basehandle);
  XDPRINTF((3, 0, "get_ns_dir_entry: vol=%d, base=0x%lx, namspace=%d",
                                    volume, basehandle, namspace));
  if (result > -1) {
    DIR_BASE_ENTRY  *e = dir_base[result];
    NW_SCAN_DIR_INFO *scif = (NW_SCAN_DIR_INFO*)rdata;
    uint8  fname[20];
    char   *unixname=alloc_nwpath2unix(&(e->nwpath), 2);
    (void) stat(unixname, &(e->nwpath.statb));

    memset(rdata, 0, sizeof(NW_SCAN_DIR_INFO));
#if 0
    U32_TO_32(basehandle, scif->searchsequence);
#else
    U32_TO_32(name_2_base(&(e->nwpath), NAME_DOS, 1), scif->searchsequence);
#endif
    (void)build_dos_name(e, fname);
    if (S_ISDIR(e->nwpath.statb.st_mode)) {
      get_dos_dir_attrib(&(scif->u.d), &e->nwpath.statb,
                              e->nwpath.volume, fname, unixname);
    } else {
      get_dos_file_attrib(&(scif->u.f), &e->nwpath.statb,
                              e->nwpath.volume, fname, unixname);
    }
    xfree(unixname);
    return(sizeof(NW_SCAN_DIR_INFO));
  }
  return(result);
}


int handle_func_0x56(uint8 *p, uint8 *responsedata, int task)
/* extended attribute calls */
{
  int result    = -0xfb;      /* unknown request                  */
  int ufunc     = (int) *p++; /* now p locates at 4 byte boundary */
  XDPRINTF((3, 0, "0x56 call ufunc=0x%x", ufunc));
  switch (ufunc) {

#if 1
    case  0x01 :  /* close extended attribute handle */
      {
        /*
        uint32 ea_handle=GET_BE32(p+2);
        */
        result=0; /* dummy */
      }
      break;

    case  0x02 :  /* write extended attribute handle */
      {
        result=0; /* dummy */
      }
      break;

    case  0x03 :  /* read extended attribute */
      {
#if 0
        int flags         =  GET_16(p);  /* LOW-HIGH */
        /* next 2 entries only if flags bits 0-1 = 00 */
        /* (~(flags & 3)) volume   + basehandle  */
        /* ( flag s & 2)  eahandle + basehandle  */
        int volume        = (int)GET_32(p+2);
        uint32 basehandle = (int)GET_32(p+6);

        uint32 readpos    = GET_32(p+10);
        uint32 size       = GET_32(p+14);
        uint16 keylen     = GET_16(p+18);    /* LOW-HIGH */
#endif
        struct OUTPUT {
          uint8   errorcode[4];     /* LOW-HIGH */
          uint8   ttl_v_length[4];  /* LOW-HIGH */
          uint8   ea_handle[4];     /* ???? */
          uint8   access[4];        /* ???? */
          uint8   value_length[2];  /* LOW-HIGH */
          uint8   value[2];
        } *xdata= (struct OUTPUT*)responsedata;
        memset(xdata, 0, sizeof(struct OUTPUT));
        U32_TO_32(0xc9,  xdata->errorcode); /* NO extended Attributes */
        result        = sizeof(struct OUTPUT);
      }
      break;

    case  0x04 :  /* enumerate extended attributes */
      {
        struct OUTPUT {
          uint8   dontknow1[16];   /* all zero */
          uint8   ea_handle[4];    /* ???? */
          uint8   dontknow3[4];    /* all zero */
        } *xdata= (struct OUTPUT*)responsedata;
        memset(xdata, 0, sizeof(struct OUTPUT));
        result       = sizeof(struct OUTPUT);
      }
      break;

    case  0x05 :  /* duplicate extended attributes */
      {
      }
      break;

#endif
    default : result = -0xfb; /* unknown request */
  } /* switch */
  return(result);
}

void exit_name_space_module(void)
{
  free_dir_bases();
  free_search_handles();
}

void init_name_space_module(int max_baseh, int max_searchh)
{
  exit_name_space_module();
  max_dir_base_entries=(max_baseh < 5) ? MAX_DIR_BASE_ENTRIES : max_baseh;
  dir_base=(DIR_BASE_ENTRY **)xcmalloc(
          sizeof(DIR_BASE_ENTRY*) * max_dir_base_entries);

  max_dir_search_handles=(max_searchh < 10 || max_searchh > 255) ? 50 : max_searchh;
  dir_search_handles=(DIR_SEARCH_HANDLE **)xcmalloc(
          sizeof(DIR_SEARCH_HANDLE*) * max_dir_search_handles);
}
#endif
