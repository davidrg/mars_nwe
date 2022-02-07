/* dbmtool.c  08-Sep-96  data base tool program for mars_nwe */
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
#include "nwdbm.h"

/* dummy to make nwdbm happy */
int b_acc(uint32 obj_id, int security, int forwrite)
{
  return(0);
}

static int usage(char *s)
{
  char *p=strrchr(s, '/');
  fprintf(stderr, "usage:\t%s e | i | r [path]\n", p ? p+1 : s);
  fprintf(stderr, "\te = export\n");
  fprintf(stderr, "\ti = import\n");
  fprintf(stderr, "\tr = repair\n");
  return(1);
}

int main(int argc, char *argv[])
{
  init_tools(0, 0);
  nw_debug=5;
  if (argc < 2)             return(usage(argv[0]));
  if      (*argv[1] == 'e') return(do_export_dbm(argv[2]));
  else if (*argv[1] == 'i') return(do_import_dbm(argv[2]));
  else if (*argv[1] == 'r') if (!do_export_dbm(argv[2]))
                              return(do_import_dbm(argv[2]));
                            else return(1);
  else usage(argv[0]);
  return(0);
}
