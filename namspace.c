/* namspace.c 07-Feb-96 : NameSpace Services, mars_nwe */

/* !!!!!!!!!!!! NOTE !!!!!!!!!! */
/* Its very dirty till now. */
/* namespace calls should be only activated for testings */

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
#include <utime.h>
#ifndef LINUX
#include <errno.h>
#endif

#include "nwvolume.h"
#include "connect.h"
#include "nwfile.h"
#include "namspace.h"

#if WITH_NAME_SPACE_CALLS

#define NW_PATH  /* */


typedef struct {
  int    volume;            /* Volume Number                 */
  int    has_wild;          /* fn has wildcards              */
  struct stat statb;
  uint8  *fn;               /* points to last entry of path  */
  uint8  path[512];         /* path + fn 	  	     */
} N_NW_PATH;

typedef struct {
  DIR        *fdir;         /* for dir searches              */
  uint8      *kpath;        /* points one after unixname     */
  uint8      *unixname;     /* is allocates fullname of path */
  	     		    /* + 257 Byte for filename.      */
} DIR_SEARCH_STRUCT;

typedef struct {
  uint32      basehandle;
  int          namespace;   /* namespace of this entry       */
  DIR_SEARCH_STRUCT *dir;   /* for dir searches              */
  N_NW_PATH       nwpath;
} DIR_BASE_ENTRY;

#define MAX_DIR_BASE_ENTRIES   300

static DIR_BASE_ENTRY *dir_base[MAX_DIR_BASE_ENTRIES];
static int anz_dbe    = 0;

static void init_nwpath(N_NW_PATH *nwpath)
{
  nwpath->volume    = -1;
  nwpath->has_wild  = 0;
  nwpath->fn        = nwpath->path;
  *(nwpath->path)   = '\0';
}

static char *xnwpath_2_unix(N_NW_PATH *nwpath, int modus,
                           int allocate_extra)
/*
 * returns complete UNIX path
 * modus & 1 : ignore fn, (only path)
 * modus & 2 : no  '/' at end
 *
 * if allocate_extra > 0, then not the static buffer will be returned
 * but a fresh allocated buffer, which must be deallocated later.
 */
{
  static char unixname[512];    /* should be big enouugh */
  int    len;
  int volume = nwpath->volume;
  char *p, *pp;
  if (volume < 0 || volume >= used_nw_volumes) {
    fprintf(stderr, "nwpath_2_unix volume=%d not ok\n", volume);
    strcpy(unixname, "Z/Z/Z/Z"); /*  */
    len = strlen(unixname);
  } else {
    int m = ((modus & 1) && nwpath->fn > nwpath->path)  /* last path = fn */
               ? nwpath->fn - nwpath->path
               : strlen((char*)nwpath->path);
    len   = nw_volumes[volume].unixnamlen;
    if (m+len >= sizeof(unixname)) {
      fprintf(stderr, "nwpath_2_unix buffer to small, needs %d Byte",
                     m+len+1);
      strcpy(unixname, "Z/Z/Z/Z"); /*  */
      len = strlen(unixname);
    } else {
      memcpy(unixname, nw_volumes[volume].unixname, len); /* first UNIXNAME VOLUME */
      p = pp = unixname+len;
      memcpy(p, nwpath->path, m); /* path or partiell path */
      len += m;
      p   += m;
      if ((modus & 2) && *(p-1) == '/' ) *(--p) = '\0';
      else *p = '\0';
      if (nw_volumes[volume].options & 1) downstr((uint8*)pp);
    }
  }
  if (allocate_extra) {
    char *ret=xmalloc(len+allocate_extra+2);
    strcpy(ret, unixname);
    return(ret);
  }
  return(unixname);
}

#define nwpath_2_unix(nwpath, modus) xnwpath_2_unix((nwpath), (modus), 0)

static void free_dbe_dir(DIR_BASE_ENTRY *dbe)
{
  DIR_SEARCH_STRUCT *d = dbe->dir;
  if (NULL != d) {
    if (d->fdir) closedir(d->fdir);
    xfree(d->unixname);
    xfree(dbe->dir);
  }
}

static int allocate_dbe_dir(DIR_BASE_ENTRY *dbe)
{
  DIR_SEARCH_STRUCT *d;
  if (dbe->dir) free_dbe_dir(dbe);
  dbe->dir = d  = (DIR_SEARCH_STRUCT*) xcmalloc(sizeof(DIR_SEARCH_STRUCT));
  d->unixname   = (uint8*)xnwpath_2_unix(&(dbe->nwpath), 2, 258);
  if (NULL == (d->fdir = opendir(d->unixname))) {
    free_dbe_dir(dbe);
    return(-0xff);
  } else {
    d->kpath      = d->unixname+strlen(d->unixname);
    *(d->kpath)   = '/';
    *(++d->kpath) = '\0';
    return(0);
  }
}

static void free_dbe_ptr(DIR_BASE_ENTRY *dbe)
{
  if (dbe != (DIR_BASE_ENTRY*)NULL) {
    free_dbe_dir(dbe);
    xfree(dbe);
  }
}

static int base_open_seek_dir(DIR_BASE_ENTRY *dbe, uint32 offset)
{
  int result = ((dbe->nwpath.statb.st_mode & S_IFMT) != S_IFDIR) ? -0xff : 0;
  if (!result) {
    if (NULL == dbe->dir) result=allocate_dbe_dir(dbe);
    if (result>-1) seekdir(dbe->dir->fdir, offset);
  }
  if (result < 0 && NULL != dbe->dir) free_dbe_dir(dbe);
  XDPRINTF((3, 0, "base_open_seek_dir offset=%d, result=%d", offset, result));
  return(result);
}


static int allocate_dbe(int namespace)
/* returns new allocated dir_base_entry */
{
  int j=-1;
  DIR_BASE_ENTRY **pdbe=(DIR_BASE_ENTRY**) NULL;
  if (namespace) return(-0xff);   /* TODO: more namespaces */
  while (++j < anz_dbe && NULL != *(pdbe = &(dir_base[j]))  ) ;;
  if (j == anz_dbe) {
    if (anz_dbe == MAX_DIR_BASE_ENTRIES) {  /* return(-0xff); */
      pdbe = &(dir_base[--j]);
      free_dbe_ptr(*pdbe);
    } else pdbe = &(dir_base[anz_dbe++]);
  }
  *pdbe  = (DIR_BASE_ENTRY*)xcmalloc(sizeof(DIR_BASE_ENTRY));
  (*pdbe)->namespace = namespace;
  init_nwpath(&((*pdbe)->nwpath));
  return(j);
}

static void free_dbe(int dbase)
{
  if (dbase > -1 && dbase < anz_dbe) {
    free_dbe_ptr(dir_base[dbase]);
    dir_base[dbase] = (DIR_BASE_ENTRY*)NULL;
    if (dbase+1 == anz_dbe) {
      while (anz_dbe && ((DIR_BASE_ENTRY*)NULL == dir_base[anz_dbe-1]) )
        --anz_dbe;
    }
  }
}

int touch_handle_entry(int dbase)
/* routine touchs this entry and returns the new offset */
{
  XDPRINTF((4, 0, "touch_handle_entry entry dbase=%d", dbase));
  if (dbase > 2) {
    DIR_BASE_ENTRY *dbe=dir_base[dbase];
    while (dbase--) dir_base[dbase+1] = dir_base[dbase];
    dbase=0;
    dir_base[0] = dbe;
  }
  XDPRINTF((4, 0, "touch_handle_entry return dbase=%d", dbase));
  return(dbase);
}

char *debug_nwpath_name(N_NW_PATH *p)
/* for debugging */
{
#ifdef DB
  static char nwpathname[512];
  char volname[300];
  if (nw_get_volume_name(p->volume, volname) < 0)
    sprintf(volname, "<%d=NOT-OK>", (int)p->volume);
  sprintf(nwpathname, "`%s:%s`,fn=`%s`", volname, p->path, p->fn);
#else
  static char nwpathname[2];
  nwpathname[0]='\0';
  nwpathname[1]='\0';
#endif
  return(nwpathname);
}


static int add_hpath_to_nwpath(N_NW_PATH *nwpath,
                               NW_HPATH *nwp, uint8 *pp_pathes)
/* Routine adds nwp  to  nwpath */
/* nwpath must be setup correctly before entry */
/* return > -1 if ok  */
{
  int     result    =  0;
  int     k         = -1;
  uint8  *pp        = nwpath->path+strlen(nwpath->path);
  while (!result && ++k < nwp->components) {
    int   len = (int) *(pp_pathes++);
    uint8 *p  =         pp_pathes;
    pp_pathes+=len;

    if (!k && nwpath->volume == -1) { /* first component is volume */
      if ((nwpath->volume=nw_get_volume_number(p, len)) < 0)  {
        result = nwpath->volume;
        goto leave_build_nwpath;
      }
    } else {  /* here is path (+ fn ) */
      int i=len;
      if (pp > nwpath->path) {  /* not the first entry */
        *pp='/';
        *++pp='\0';
      }
      while (i--) {
        if (*p == 0xae) *pp++ = '.';
        else if (*p > 0x60 && *p < 0x7b) {
          *pp++ = *p - 0x20;  /* all is upshift */
        } else if (*p == 0xaa || *p == '*' ) {
          *pp++ = '*';
          nwpath->has_wild++;
        } else if (*p == 0xbf || *p == '?' ) {
          *pp++ = '?';
          nwpath->has_wild++;
        } else if (*p == '/' || *p == '\\') {
          *pp++ = '/';
        } else if (*p == ':') { /* extract volume */
          int vlen = (int) (pp - nwpath->path);
          if ((nwpath->volume=nw_get_volume_number(nwpath->path, vlen)) < 0) {
            result = nwpath->volume;
            goto leave_build_nwpath;
          }
          pp=nwpath->path;
          *pp='\0';
        } else *pp++ = *p;
        p++;
      }  /* while */
      *pp = '\0';
    }  /* else */
  } /* while */
  if (nwpath->volume < 0) result=-0x9c;
leave_build_nwpath:
  XDPRINTF((2, 0, "add_hpath_to_nwpath: result=0x%x, nwpath=%s",
                  result, debug_nwpath_name(nwpath)));
  return(result);
}

static uint32 build_base_handle(N_NW_PATH *nwpath, int namespace)
/* returns basehandle of path, or 0 if not exist !! */
{
  uint32 basehandle=0L;
  if (!stat(nwpath_2_unix(nwpath, 2), &(nwpath->statb))) {
    DEV_NAMESPACE_MAP dnm;
    dnm.dev       = nwpath->statb.st_dev;
    dnm.namespace = namespace;
    basehandle=nw_vol_inode_to_handle(nwpath->volume,
                                  nwpath->statb.st_ino,
                                  &dnm);
  } else {
    XDPRINTF((4,0, "build_base_handle failed:`%s`",
             debug_nwpath_name(nwpath)));
  }
  return(basehandle);
}

static int find_base_entry(int volume, uint32 basehandle)
{
  int k=-1;
  while (++k < anz_dbe) {
    DIR_BASE_ENTRY *e=dir_base[k];
    if ( (DIR_BASE_ENTRY*)NULL != e
      && basehandle == e->basehandle
      && volume     == e->nwpath.volume) return(k);
  }
  return(-0x9b);
}

static int insert_get_base_entry(N_NW_PATH *nwpath, int dbase,
                                 int namespace, int creatmode)
{
  uint32 basehandle = build_base_handle(nwpath, namespace);
  if (!basehandle && creatmode) {
    int result = 0;
    char *unname = nwpath_2_unix(nwpath, 2);
    if (creatmode & FILE_ATTR_DIR) {
       /* creat dir */
      if (mkdir(unname, 0777)) result=-0x84;
    } else {
       /* creat file */
      if ((result = creat(unname, 0777)) > -1) {
        close(result);
        result = 0;
      } else result=-0x84;
    }
    if (result) return(result);
    basehandle = build_base_handle(nwpath, namespace);
  }
  if (basehandle) {
    DIR_BASE_ENTRY   *dbe=dir_base[dbase];
    int k=-1;
    while (++k < anz_dbe) {
      DIR_BASE_ENTRY *e=dir_base[k];
      if ( (DIR_BASE_ENTRY*)NULL != e
        && basehandle == e->basehandle
        && nwpath->volume == e->nwpath.volume) {
        free_dbe(dbase);
        return(touch_handle_entry(k));
      }
    } /* while */
    /* now i know that it's a new base entry */
    dbe->basehandle = basehandle;
    return(touch_handle_entry(dbase));
  }
  return(-0x9c);
}

static int build_dos_base(NW_HPATH *nwp, uint8 *pathes,
                          int dbase, int mode, uint8 *rets)
/* routine returns the actual dbe entry offset or */
/* < 0 if error */
/* if mode == 1, then last_path will be ignored and will be put */
/* into the rets variable */
{
  DIR_BASE_ENTRY   *dbe=dir_base[dbase];
  N_NW_PATH        *nwpath=&(dbe->nwpath);
  int  result=0;
  if (!nwp->flag) {  /* short handle */
    int dir_handle = nwp->base[0];
    if (dir_handle > 0 && --dir_handle < (int)used_dirs
           && dirs[dir_handle].inode) {
      nwpath->volume = dirs[dir_handle].volume;
      strcpy(nwpath->path, dirs[dir_handle].path);
      result = (nwpath->volume > -1) ? 0 : -0x98;
    } else result = -0x9b;
  } else if (nwp->flag == 1) { /* basehandle */
    uint32 basehandle = GET_32(nwp->base);
    int k=-1;
    result = -0x9b;
    while (++k < anz_dbe) {
      if (k != dbase) {
        DIR_BASE_ENTRY *e=dir_base[k];
        if ( (DIR_BASE_ENTRY*)NULL != e
            && e->nwpath.volume == nwp->volume
            && e->basehandle    == basehandle)  {
          nwpath->volume = e->nwpath.volume;
          strcpy(nwpath->path, e->nwpath.path);
          result = (nwpath->volume > -1) ? 0 : -0x98;
          break;
        }
      }
    }
  } else if (nwp->flag != 0xff) result=-0xff;
  if (!result) {
    if ((result = add_hpath_to_nwpath(nwpath, nwp, pathes)) > -1) {
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
      if (pp) nwpath->fn=(uint8*)pp+1;
      result = insert_get_base_entry(nwpath, dbase, NAME_DOS, 0);
    }
  }
  return(result);
}

int nw_generate_dir_path(int      namespace,
                         NW_HPATH *nwp,
                         uint8 	  *ns_dir_base,
                         uint8 	  *dos_dir_base)

/* returns Volume Number >=0  or errcode < 0 if error */
{
  int dbase  = allocate_dbe(namespace);
  int result = -0xfb;
  if (dbase > -1) {
    if ((result = build_dos_base(nwp, nwp->pathes, dbase, 0, NULL)) > -1) {
      DIR_BASE_ENTRY *dbe=dir_base[result];
      U32_TO_32(dbe->basehandle, ns_dir_base);  /* LOW - HIGH */
      U32_TO_32(dbe->basehandle, dos_dir_base);
      XDPRINTF((3, 0, "nw_generate_dir_path path=%s, result=%d, basehandle=0x%x",
         debug_nwpath_name(&(dbe->nwpath)), result, dbe->basehandle));
      result= dbe->nwpath.volume;
    } else free_dbe(dbase);
  }
  if (result < 0) {
    XDPRINTF((3, 0, "nw_generate_dir_path NOT OK dbase=%d, result=%d",
                     dbase, result));
  }
  return(result);
}

static int build_dir_info(DIR_BASE_ENTRY *dbe, uint32 infomask, uint8 *p)
{
  N_NW_PATH      *nwpath=&(dbe->nwpath);
  struct stat    *stb=&(nwpath->statb);
  int    result      = 76;
  memset(p, 0, result);
  if (infomask & INFO_MSK_DATA_STREAM_SPACE) {
    U32_TO_32(stb->st_size, p);
  }
  p      += 4;
  if (infomask & INFO_MSK_ATTRIBUTE_INFO) {
    uint32 mask=0L;
    if (S_ISDIR(stb->st_mode)) mask |= FILE_ATTR_DIR;
    U32_TO_32(mask, p);
    p      += 4;
    U16_TO_16((uint16)(mask & 0xFFFF), p);
    p      +=2;
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
    xun_time_2_nw(stb->st_mtime, p);
    p      +=2;
    xun_date_2_nw(stb->st_mtime, p);
    p      +=2;
    U32_TO_32(1, p);
    p      +=4;
  } else  p+=8;
  if (infomask & INFO_MSK_MODIFY_INFO) {
    xun_time_2_nw(stb->st_mtime, p);
    p      +=2;
    xun_date_2_nw(stb->st_mtime, p);
    p      +=2;
    U32_TO_32(1, p);
    p      +=4;
    xun_date_2_nw(stb->st_atime, p);  /* access date */
    p      +=2;
  } else p+=10;
  if (infomask & INFO_MSK_ARCHIVE_INFO) {
    xun_time_2_nw(0, p);
    p      +=2;
    xun_date_2_nw(0, p);
    p      +=2;
    U32_TO_32(0, p);
    p      +=4;
  } else p+=8;
  if (infomask & INFO_MSK_RIGHTS_INFO) {
    U16_TO_16(0, p);
  }
  p      +=2;
  if (infomask & INFO_MSK_DIR_ENTRY_INFO) {
    U32_TO_32(dbe->basehandle, p);
    p      +=4;
    U32_TO_32(dbe->basehandle, p);
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
    U32_TO_32(0, p);  /* Creator of the name space number */
  }
  p      +=4;
  /* ---------------------------------------------- */
  if (infomask & INFO_MSK_ENTRY_NAME) {
    *p = (uint8) strlen(nwpath->fn);
    result++;
    if (*p) {
      memcpy(p+1, nwpath->fn, (int) *p);
      result += (int) *p;
    }
  }
  XDPRINTF((3, 0, "build_dir_info:path=%s, result=%d, basehandle=0x%x",
          debug_nwpath_name(nwpath), result, dbe->basehandle));
  return(result);
}
int nw_optain_file_dir_info(int namespace,    NW_HPATH *nwp,
                            int destnamspace,
                            int searchattrib, uint32 infomask,
                            uint8 *responsedata)
/* returns sizeof info_mask
 * the sizeof info_mask is NOT related by the infomask.
 * But the _valid_ info is.
 */
{
  int dbase  = allocate_dbe(namespace);
  int result = -0xfb;
  if (dbase > -1) {
    if ((result = build_dos_base(nwp, nwp->pathes, dbase, 0, NULL)) > -1) {
      result = build_dir_info(dir_base[result], infomask, responsedata);
    } else free_dbe(dbase);
  }
  if (result < 0) {
    XDPRINTF((3, 0, "nw_optain_file_dir_info NOT OK dbase=%d, result=0x%x",
                     dbase, result));
  }
  return(result);
}

static int nw_init_search(int namespace,
                          NW_HPATH *nwp,
                          uint8 *responsedata)
{
  int dbase  = allocate_dbe(namespace);
  int result = -0xfb;
  if (dbase > -1) {
    if ((result = build_dos_base(nwp, nwp->pathes, dbase, 0, NULL)) > -1) {
      DIR_BASE_ENTRY *dbe=dir_base[result];
      result = base_open_seek_dir(dbe, 0L);
      if (result > -1) {
        *responsedata++ = dbe->nwpath.volume;
        U32_TO_32(dbe->basehandle, responsedata);
        responsedata+=4;
        U32_TO_32(0L, responsedata); /* searchsequenz */
        result     = 9;
      }
      XDPRINTF((3, 0, "nw_init_search path=%s, result=%d, basehandle=0x%x",
         debug_nwpath_name(&(dbe->nwpath)), result, dbe->basehandle));
    } else free_dbe(dbase);
  }
  if (result < 0) {
    XDPRINTF((3, 0, "nw_init_search NOT OK dbase=%d, result=%d",
                     dbase, result));
  }
  return(result);
}

int get_add_new_entry(DIR_BASE_ENTRY *qbe, uint8 *path, int creatmode)
{
  int dbase = allocate_dbe(qbe->namespace);
  if (dbase > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[dbase];
    N_NW_PATH      *nwpath=&(dbe->nwpath);
    int            result = -0x9c;
    nwpath->volume=qbe->nwpath.volume;
    strcpy(nwpath->path, qbe->nwpath.path);
    nwpath->fn = nwpath->path+strlen(nwpath->path);
    if (nwpath->fn > nwpath->path && *(nwpath->fn-1) != '/') {
      *(nwpath->fn)    = '/';
      *(++ nwpath->fn) = '\0';
    }
    strcpy(nwpath->fn, path);

    result = insert_get_base_entry(nwpath, dbase, qbe->namespace, creatmode);
    if (result < 0) free_dbe(dbase);
    return(result);
  }
  return(dbase);
}

int nw_search_file_dir(int namespace, int datastream,
                       uint32 searchattrib, uint32 infomask,
                       int volume, uint32 basehandle, uint32 sequence,
                       int len, uint8  *path, uint8 *responsedata)

{
  int result = find_base_entry(volume, basehandle);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    if ((result = base_open_seek_dir(dbe, sequence)) > -1) {
      uint8             entry[256];
      struct dirent     *dirbuff;
      struct stat       statb;
      int               dest_entry=-1;
      DIR_SEARCH_STRUCT *ds=dbe->dir;
      int  vol_options = get_volume_options(volume, 0);
      strmaxcpy(entry, path, min(255, len));
      if (vol_options & VOL_OPTION_DOWNSHIFT) downstr(entry);
      XDPRINTF((5,0,"nw_search_file_dir searchpath=%s", entry));
      while ((dirbuff = readdir(ds->fdir)) != (struct dirent*)NULL){
        if (dirbuff->d_ino) {
          uint8 *name=(uint8*)(dirbuff->d_name);
          XDPRINTF((10,0,"nw_search_file_dir Name=%s",
                             name));
          if ( (name[0] != '.' && (
                          (!strcmp(name, entry)) ||
                          (entry[0] == '*' && entry[1] == '\0')
                       || fn_match(name, entry, vol_options)))) {
            strcpy(ds->kpath, name);
            XDPRINTF((5,0,"nw_search_file_dir Name found=%s unixname=%s",
                                               name, ds->unixname));
            if (!stat(ds->unixname, &statb)
               && ( !S_ISDIR(statb.st_mode) ||
                    (searchattrib & FILE_ATTR_DIR) ) ) {
              strcpy(entry, name);
              if (vol_options & 1) upstr(entry);
              if ((dest_entry = get_add_new_entry(dbe, entry, 0)) > -1)
                  break;
            }
          }
        }  /* if */
      } /* while */
      *(ds->kpath) = '\0';
      if (dest_entry > -1) {
        DIR_BASE_ENTRY *dest_dbe=dir_base[dest_entry];
        sequence = (uint32) telldir(ds->fdir);
        *responsedata = (uint8) volume;
        responsedata++;
        U32_TO_32(basehandle, responsedata);
        responsedata+=4;
        U32_TO_32(sequence, responsedata);
        responsedata+=4;
        *responsedata = (uint8) 0;  /* reserved */
        responsedata++;
        result = 10 +
          build_dir_info(dest_dbe,
                         infomask|INFO_MSK_NAME_SPACE_INFO,
                         responsedata);
      } else result=-0xfe; /* no files matching */
    } /* if result */
  }
  return(result);
}

static int nw_open_creat_file_or_dir(int namespace,
                   int opencreatmode,
                   int attrib, uint32 infomask,
                   uint32 creatattrib,
                   int access_rights,
                   NW_HPATH *nwp, uint8 *pathes,
                   uint8    *responsedata)
{
  int dbase  = allocate_dbe(namespace);
  int result = -0xfb;
  if (dbase > -1) {
    int    exist=-1;
    uint8 last_part[258];
    *last_part='\0';
    if ((result = build_dos_base(nwp, pathes, dbase, 0, NULL)) > -1) {
      exist = result;
    } else if (opencreatmode & OPC_MODE_CREAT) {
      result = build_dos_base(nwp, pathes, dbase, 1, last_part);
      if (result > -1)
        result = get_add_new_entry(dir_base[result], last_part,
             (creatattrib & FILE_ATTR_DIR) ? FILE_ATTR_DIR : 1);
    }
    if (result > -1) {
      DIR_BASE_ENTRY *dbe=dir_base[result];
      uint32 fhandle=0L;
      int    actionresult=0;
      if (exist < 0) actionresult |= OPC_ACTION_CREAT;
      if (!(creatattrib & FILE_ATTR_DIR)) {
        int creatmode=0; /* open */
        int attrib=0;
        if (opencreatmode & (OPC_MODE_OPEN | OPC_MODE_CREAT) ) {
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
                attrib, access_rights, creatmode)) > -1) {

            fhandle = (uint32) result;
            actionresult |= OPC_ACTION_OPEN;  /* FILE OPEN */
            if (exist > -1 && (opencreatmode & OPC_MODE_REPLACE))
                actionresult |= OPC_ACTION_REPLACE;  /* FILE REPLACED */
          }
        } else result=-0xff;
      } else if (exist  > -1) result=-0x84;
      if (result > -1) {
        U32_TO_BE32(fhandle,   responsedata);
        responsedata += 4;
        *responsedata =(uint8) actionresult;
        responsedata++ ;
        result = 5 + build_dir_info(dbe,infomask, responsedata);
      }
    }
  }
  XDPRINTF((3, 0, "nw_open_creat mode=0x%x, creatattr=0x%x, access=0x%x, attr=0x%x, result=%d",
       opencreatmode, creatattrib, access_rights, attrib, result));
  return(result);
}

static int nw_delete_file_dir(int namespace, int searchattrib,
                               NW_HPATH *nwp)
{
  int dbase  = allocate_dbe(namespace);
  int result = -0xfb;
  if (dbase > -1) {
    if ((result = build_dos_base(nwp, nwp->pathes, dbase, 0, NULL)) > -1) {
      DIR_BASE_ENTRY *dbe=dir_base[dbase=result];
      uint8  *unname=(uint8*)nwpath_2_unix(&(dbe->nwpath), 2);

      if (S_ISDIR(dbe->nwpath.statb.st_mode)) result = rmdir(unname);
      else result = unlink(unname);
      if (result < 0) {
        switch (errno) {
          case EEXIST: result=-0xa0; /* dir not empty */
          default:     result=-0x8a; /* No privilegs */
        }
      } else {
        result = 0;
        free_dbe(dbase) ;
      }
    } else free_dbe(dbase);
  }
  return(result);
}

int handle_func_0x57(uint8 *p, uint8 *responsedata)
{
  int result    = -0xfb;      /* unknown request                  */
  int ufunc     = (int) *p++; /* now p locates at 4 byte boundary */
  int namespace  = (int) *p;   /* for most calls     	 	  */
  XDPRINTF((3, 0, "0x57 call ufunc=0x%x namespace=%d", ufunc, namespace));
  switch (ufunc) {
    case  0x01 :  /* open creat file or subdir */
      {
        /* NW PATH STRUC */
        int opencreatmode      = *(p+1);
        int      attrib        = (int) GET_16(p+2);  /* LOW-HI */
        uint32   infomask      =       GET_32(p+4);  /* LOW-HI */
        uint32   creatattrib   =       GET_32(p+8);
        int      access_rights = (int) GET_16(p+12); /* LOW-HI */
        NW_HPATH nwpathstruct;
        memcpy(&nwpathstruct, p+14, sizeof(nwpathstruct));
        result  = nw_open_creat_file_or_dir(namespace, opencreatmode,
                   attrib, infomask, creatattrib, access_rights,
                   &nwpathstruct, p+21, responsedata);
      }
      break;


    case  0x02 :  /* Initialize Search */
      {
        /* NW PATH STRUC */
        NW_HPATH *nwpathstruct = (NW_HPATH *) (p+2);
        result  = nw_init_search(namespace, nwpathstruct, responsedata);
      }
      break;

    case  0x03 :  /* Search for File or DIR */
      {
        /* NW PATH STRUC */
        int      datastream    = *(p+1);
        int      searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        uint32   infomask      = GET_32(p+4);       /* LOW-HI */
        int      volume        = *(p+8);
        uint32   basehandle    = GET_32(p+9);       /* LOW-HI */
        uint32   sequence      = GET_32(p+13);      /* LOW-HI */
        int      len           = *(p+17);
        uint8	 *path         = p+18;
        result  = nw_search_file_dir(namespace,  datastream,
                                     searchattrib, infomask,
                                     volume, basehandle, sequence,
                                     len, path, responsedata);
      }
      break;


    case  0x06 :  /* Obtain File or Subdir Info */
      {
        int      destnamspace  = (int) p+1;
        int      searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        uint32   infomask      = GET_32(p+4);       /* LOW-HI */
        NW_HPATH *nwpathstruct = (NW_HPATH *) (p+8);
        result = nw_optain_file_dir_info(namespace, nwpathstruct,
                                        destnamspace,
                                        searchattrib, infomask,
                                        responsedata);
      }
      break;


    case  0x07 :  /* Modify File or Dir Info */
      {

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
      {

      }
      break;

    case  0x0c : /* alloc short dir Handle */
      {

      }
      break;


    case  0x15 : /* Get Path String from short dir new */
      {

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
        result=get_volume_options(volume, 0);
        if (result >-1) {
          xdata->anz_name_spaces    = (uint8) 1;
          xdata->name_space_list[0] = (uint8) NAME_DOS;
          result=xdata->anz_name_spaces+1;
        }
      }
      break;

    case  0x1a :  /* Get Huge NS Info new*/
      {

      }
      break;
    case  0x1c :  /* GetFullPathString new*/
      {

      }
      break;
    case  0x1d :  /* GetEffDirRights new */
      {

      }
      break;

    default : result = -0xfb; /* unknown request */
  } /* switch */
  return(result);
}

#endif
