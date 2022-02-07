/* capture.c 05-Apr-96 */

/****************************************************************
 * (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany   *
 ****************************************************************/

#include "net.h"

static int usage(void)
{
  fprintf(stderr, "usage:\t%s  level\n", funcname);
  fprintf(stderr, "\tlevel=0 .. 99\n" );
  return(-1);
}

static int parse_argv(uint8 *devname, uint8 *queuename,
                  int argc, char *argv[], int parsemode)
{
  int  k      = 0;
  *devname    = '\0';
  *queuename  = '\0';
  while (++k < argc) {
    uint8 *p  = argv[k];
    if (k == 1) {
      strmaxcpy(devname,   p, 20);
      upstr(devname);
      if (!strcmp(devname, "PRN"))
        strcpy(devname, "LPT1");
    } else if (k == 2) {
      strmaxcpy(queuename, p, 20);
      upstr(queuename);
    }
  }
  return(0);
}

static int do_capture(uint8 *drvstr, uint8 *queuestr, int delete)
{
  int result = redir_device_drive(delete ? -1 : 0x3, drvstr, queuestr);
  return(result);
}

static int show_capture(uint8 *drvstr)
{
  int result;
  int k =-1;
  uint8 devname[20];
  uint8 remotename[130];
  int  devicetyp;
  while ((result = list_redir(++k, &devicetyp, devname, remotename)) > -1){
    if (result > -1 && devicetyp == 0x3) {
      upstr(devname);
      upstr(remotename);
      if (!drvstr || !*drvstr || !strcmp(devname, drvstr))
        fprintf(stdout, "%-10s captured to %s\n", devname, remotename);
    }
  }
  return(result);
}

int func_capture(int argc, char *argv[], int mode)
{
  uint8  devname [22];
  uint8  queuestr[22];
  if (!parse_argv(devname, queuestr, argc, argv, mode)) {
    int result=0;
    if (*queuestr || mode == 1) {
      result=do_capture(devname, queuestr, mode);
      if (result< 0)
        fprintf(stderr, "capture error:%d, device:%s \n", result, devname);
    }
    if (mode != 1)
      show_capture(devname);
    else if (result > -1)
      fprintf(stdout, "Capture of %s removed\n", devname);
    return(result);
  }
  return(1);
}

