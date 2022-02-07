/* nwtests.c 20-May-96 */

/****************************************************************
 * (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany   *
 ****************************************************************/

#include "net.h"

static int usage(void)
{
  return(-1);
}

int func_tests(int argc, char *argv[], int mode)
{
  int level     = ncp_17_02(NWCONN, 6);
  int dirhandle = alloc_temp_dir_handle(0, "SYS:", 'd', NULL);
  int result    = -1;
  uint8  *path  = (argc < 2) ? "SYS:\\TMP" : argv[1];
  if (dirhandle > -1) {
    result = ncp_16_02(dirhandle, "SYSTEM/", NULL, NULL, NULL, NULL);
    result = ncp_16_02(dirhandle, "SYSTEM", NULL, NULL, NULL, NULL);
  }
  fprintf(stdout, "dirhandle=%d, result=%d\n", dirhandle, result);
  result = redir_device_drive(0x4, "u:", path);
  fprintf(stdout, "redir path=%s, result=%d\n", path, result);

  path="Q1";
  result = redir_device_drive(0x3, "LPT1", path);
  fprintf(stdout, "redir path=%s, result=%d\n", path, result);

  {
    int k =-1;
    uint8 devname[20];
    uint8 remotename[130];
    int  devicetyp;
    while ((result = list_redir(++k, &devicetyp, devname, remotename)) > -1){
       fprintf(stdout, "index=%d, dev=%s(%d), %s result=%d\n",
            k, devname, devicetyp, remotename, result);
    }
  }
  if (level > -1) (void) ncp_17_02(NWCONN, level);
  return(0);
}
