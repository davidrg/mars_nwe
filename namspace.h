/* namspace.h 09-Nov-96 : NameSpace Services, mars_nwe */

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

#ifndef _NAMSPACE_H_
#define _NAMSPACE_H_
#if WITH_NAME_SPACE_CALLS

#define NAME_DOS   0
#define NAME_MAC   1
#define NAME_NFS   2
#define NAME_FTAM  3
#define NAME_OS2   4

typedef struct {
  uint8  volume;      /* Volumenumber                               */
  uint8  base[4];     /* Base or short Handle, handle is base[0] !! */
  uint8  flag;        /* 0=handle, 1=base, 0xff=not path nor handle */
  uint8  components;  /* nmbrs of pathes, components                */
  uint8  pathes[1];   /* form len+name  (like Pascal)               */
  /* ATTENTION
   * a pathlen of 0 means '..' (cd dir ..) !!
   * first seen with Novell client32
   */
} NW_HPATH;

typedef struct {
  uint8               attributes[4];
  uint8               created_date[2];
  uint8               created_time[2];
  uint8               created_id[4];
  uint8               modified_date[2];
  uint8               modified_time[2];
  uint8               modified_id[4];
  uint8               archived_date[2];
  uint8               archived_time[2];
  uint8               archived_id[4];
  uint8   	      last_access_date[2];
  uint8   	      rightsgrantmask[2];
  uint8   	      rightsrevokemask[2];
  uint8    	      maxspace[4];
} DOS_MODIFY_INFO;

#define INFO_MSK_ENTRY_NAME                 0x00000001
#define INFO_MSK_DATA_STREAM_SPACE          0x00000002
#define INFO_MSK_ATTRIBUTE_INFO             0x00000004
#define INFO_MSK_DATA_STREAM_SIZE           0x00000008
#define INFO_MSK_TOTAL_DATA_STREAM_SIZE     0x00000010
#define INFO_MSK_EXT_ATTRIBUTES             0x00000020
#define INFO_MSK_ARCHIVE_INFO               0x00000040
#define INFO_MSK_MODIFY_INFO                0x00000080
#define INFO_MSK_CREAT_INFO                 0x00000100
#define INFO_MSK_NAME_SPACE_INFO            0x00000200
#define INFO_MSK_DIR_ENTRY_INFO             0x00000400
#define INFO_MSK_RIGHTS_INFO                0x00000800


/* Search Attributes */
#define W_SEARCH_ATTR_DIR                   0x00008000
#define W_SEARCH_ATTR_ALL                   0x00008006


/* OPEN/CREAT Modes */
#define OPC_MODE_OPEN		    	    0x01
#define OPC_MODE_REPLACE		    0x02
#define OPC_MODE_CREAT		            0x08

/* OPEN/CREAT ACTION Results */
#define OPC_ACTION_OPEN		    	    0x01
#define OPC_ACTION_CREAT                    0x02
#define OPC_ACTION_REPLACE		    0x04


/* Modify File or Subdirektory DOS Info infomask */
#define DOS_MSK_ATTRIBUTE                   0x00000002

#define DOS_MSK_CREAT_DATE                  0x00000004
#define DOS_MSK_CREAT_TIME                  0x00000008
#define DOS_MSK_CREAT_ID                    0x00000010

#define DOS_MSK_ARCHIVE_DATE                0x00000020
#define DOS_MSK_ARCHIVE_TIME                0x00000040
#define DOS_MSK_ARCHIVE_ID                  0x00000080

#define DOS_MSK_MODIFY_DATE                 0x00000100
#define DOS_MSK_MODIFY_TIME                 0x00000200
#define DOS_MSK_MODIFY_ID                   0x00000400

#define DOS_MSK_ACCESS_DATE                 0x00000800
#define DOS_MSK_INHERIT_RIGHTS              0x00001000
#define DOS_MSK_MAX_SPACE                   0x00002000

extern int handle_func_0x57(uint8 *p, uint8 *responsedata, int task);
extern int handle_func_0x56(uint8 *p, uint8 *responsedata, int task);

extern int get_namespace_dir_entry(int volume, uint32 basehandle,
                                   int namspace, uint8 *rdata);

#endif
#endif

