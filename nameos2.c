/* nameos2.c 08-Aug-96 : NameSpace OS2 Services, mars_nwe */
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
#include "unxfile.h"
#include "nameos2.h"

#if WITH_NAME_SPACE_CALLS

#define MAX_NAME_OS2_CACHE   0

#if MAX_NAME_OS2_CACHE
typedef struct {
  uint8 *cache[MAX_NAME_OS2_CACHE];
} OS2BUF;

static void init_os2buf(NW_VOL *vol)
{
  vol->os2buf = xcmalloc(sizeof(OS2BUF));
}

static int vgl_name(uint8 *s, uint8 *p)
{
  int hit=0;
  if (!s) return(0);
  for (; *s && *p; s++,p++) {
    if (*s == *p) {
      if (*s == '/')
        ++hit;
    } else if ((!isalpha(*p)) || (!isalpha(*s))
      || (*p | 0x20) != (*s | 0x20))  {
      return(hit);
    }
  }
  return((*s == *p) ? -1 : hit);
}
#endif

static int my_match(uint8 *s, uint8 *p)
{
  int len=0;
  for (; *s && *p; s++,p++) {
    if (*s != *p && ((!isalpha(*p)) || (!isalpha(*s))
      || (*p | 0x20) != (*s | 0x20)))
      return(0);
    ++len;
  }
  return( ((!*s) && (*p=='/' || *p == '\0')) ? len : 0);
}

static int get_match(uint8 *unixname, uint8 *p)
{
  DIR       *d;
  if (!p || !*p)   return(1);
  *p        = '\0';
  if (NULL != (d=opendir(unixname))) {
    struct dirent *dirbuff;
    XDPRINTF((10, 0, "opendir OK unixname='%s' p='%s'", unixname, p+1));
    *p      = '/';
    while ((dirbuff = readdir(d)) != (struct dirent*)NULL){
      int len;
      if (dirbuff->d_ino) {
        XDPRINTF((10, 0, "get match found d_name='%s'", dirbuff->d_name));
        if (0 != (len=my_match(dirbuff->d_name, p+1))) {
          memcpy(p+1, dirbuff->d_name, len);
          XDPRINTF((10, 0, "get match, match OK"));
          closedir(d);
          return(get_match(unixname, p+1+len));
        }
      }
    }
    closedir(d);
  } else {
    XDPRINTF((2, 0, "os2 get_match opendir failed unixname='%s'", unixname));
    *p='/';
  }
  return(0);
}

#if MAX_NAME_OS2_CACHE
static int get_name(uint8 *s, uint8 *unixname, int hit, uint8 *p)
{
  if (hit && s) {
    for (; *s && *p; s++,p++) {
      if (*s=='/') {
        if (!--hit) break;
      } else *p=*s;
    }
  } else
    --p;  /* to get last '/' */
  return(get_match(unixname, p));
}
#endif

void mangle_os2_name(NW_VOL *vol, uint8 *unixname, uint8 *pp)
{
#if MAX_NAME_OS2_CACHE
  int     k           = -1;
  int     besthit     = -1;
  int     maxhits     =  0;
  OS2BUF *b;
  if (!vol->os2buf) init_os2buf(vol);
  b= (OS2BUF*)vol->os2buf;

  while (++k < MAX_NAME_OS2_CACHE) {
    int hits=vgl_name(b->cache[k], pp);
    if (hits < 0) {
      besthit=k;
      break;
    } else if (hits > maxhits) {
      besthit=k;
      maxhits =hits;
    }
  }
  if (maxhits > -1) {
    /* do not completely match */
    if (get_name(maxhits ? b->cache[besthit] : NULL,
           unixname, maxhits, pp)) {
      int k=MAX_NAME_OS2_CACHE-1;
      xfree(b->cache[k]);
      while (k--) {
        b->cache[k+1] = b->cache[k];
      }
      b->cache[0] = NULL;
      new_str(b->cache[0], pp);
    }
  } else {
    strcpy(pp, b->cache[besthit]);
    if (besthit > 2) {
      uint8 *sp=b->cache[besthit];
      while (besthit--) {
        b->cache[besthit+1] = b->cache[besthit];
      }
      b->cache[0] = sp;
    }
  }
#else
  get_match(unixname, pp-1);
#endif
}

int fn_os2_match(uint8 *s, uint8 *p, int soptions)
/* OS/2 name matching routine */
{
  int  pc, sc;
  uint state = 0;
  int  anf, ende;
  int  not = 0;
  uint found = 0;
  while ( (pc = *p++) != 0) {
    if (!(soptions & VOL_OPTION_IGNCASE)) {
      if (soptions & VOL_OPTION_DOWNSHIFT){   /* only downshift chars */
        if (*s >= 'A' && *s <= 'Z') return(0);
      } else {
        if (*s >= 'a' && *s <= 'z') return(0);
      }
    }
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

           default   : break;
         }
      } else if (pc == '\\') continue;

      switch  (pc) {
        case '.' :
        case 1000:  if (*s && ('.' != *s++))
                      return(0);
                     break;

        case '?' :
        case 2000:  if (!*s) return(0);
                    ++s;
                    break;

        case '*' :
        case 3000:  if (!*p) return(1); /* last star */
                    while (*s) {
                      if (fn_os2_match(s, p, soptions) == 1) return(1);
                      else if (*s=='.' && !*(p+1)) return(0);
                      ++s;
                    }
                    if (*p == '.') return(fn_os2_match(s, p, soptions));
                    return(0);

        case '[' :  if ( (*p == '!') || (*p == '^') ){
                       ++p;
                       not = 1;
                     }
                     state = 1;
                     continue;

        default  :  if (soptions & VOL_OPTION_IGNCASE) {
                      if ( pc != *s &&
                       (    (!isalpha(pc))
                         || (!isalpha(*s))
                         || (pc | 0x20) != (*s | 0x20) ) )
                           return(0);
                    } else if (pc != *s) return(0);
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
  if (*s=='.' && *(s+1)=='\0') return(1);
  return ( (*s) ? 0 : 1);
}


#endif
