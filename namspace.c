/* namspace.c 26-Nov-95 : NameSpace Services, mars_nwe */

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

#include "net.h"

typedef struct {
   int x;

} DIR_BASE_ENTRY;

#define MAX_DIR_BASE   20

int nw_generate_dir_path(uint8 *nwpathstruct,
                         uint8 *ns_dir_base, uint8 *dos_dir_base)
/* returns Volume Number >=0  or  errcode < 0 if error */
{
  return(-0xfb); /* TODO: complete call */
}


int handle_func_0x57(uint8 *p, uint8 *responsedata)
{
  int result    = -0xfb;      /* unknown request                  */
  int ufunc     = (int) *p++; /* now p locates at 4 byte boundary */
  int namspace  = (int) *p;   /* for most calls     	 	  */
  switch (ufunc) {
    case  0x02 :  /* Initialize Search */
      {
        /* NW PATH STRUC */
      }
      break;
    case  0x07 :  /* Modify File or Dir Info */
      {

      }
      break;
    case  0x09 : /* Set short Dir Handle*/
      {

      }
      break;
    case  0x15 : /* Get Path String from short dir neu*/
      {

      }
      break;
    case  0x16 : /* Generate Dir BASE and VolNumber */
      {
        uint8 *nwpathstruct  =  p+3;
        struct OUTPUT {
          uint8   ns_dir_base[4];   /* BASEHANDLE */
          uint8   dos_dir_base[4];  /* BASEHANDLE */
          uint8   volume;           /* Volumenumber*/
        } *xdata= (struct OUTPUT*)responsedata;
        result = nw_generate_dir_path(nwpathstruct,
              xdata->ns_dir_base, xdata->dos_dir_base);

        if (result >-1) {
          xdata->volume = result;
          result        = sizeof(struct OUTPUT);
        }
      }
      break;
    case  0x0c : /* alloc short dir Handle */
      {

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
