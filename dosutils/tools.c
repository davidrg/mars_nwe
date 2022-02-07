/* tools.c: 12-Jan-96 */
#include "net.h"

/****************************************************************
 * (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany   *
 ****************************************************************/

int key_pressed(void)
{
  REGS regsin, regsout;
  regsin.h.ah = 0x01; /* read key-press */
  int86(0x16, &regsin, &regsout);
  return((regsout.x.flags & 0x40) ? 0 : 1); /* zeroflag != 0 */
}

void clear_kb(void)
{
  REGS regsin, regsout;
  while (key_pressed()) {    /* zeroflag != 0 */
    regsin.h.ah = 0x00;      /* read key-press */
    int86(0x16, &regsin, &regsout);
  }
}

int ask_user(char *p, ...)
{
   int key;
   int flag = 0;
   va_list argptr;
   va_start(argptr, p);
   vfprintf(stderr, p, argptr);
   va_end(argptr);
   fprintf(stderr, "\n Please answer: Y)es or N)o!");
   while (1) {
     key = getch();
     if (key == 'J' || key == 'j' || key== 'y' || key == 'Y') {
       fprintf(stderr, "Y\n\n");
       flag = 1;
       break;
     }
     if (key == 'N' || key == 'n') {
       fprintf(stderr, "N\n\n");
       flag = 0;
       break;
     }
   }
   clear_kb();
   return(flag);
}

char *xmalloc(uint size)
{
  char *p = (size) ? (char *)malloc(size) : (char*)NULL;
  if (p == (char *)NULL && size){
    fprintf(stderr, "not enough core, need %d Bytes\n", size);
    exit(1);
  }
  return(p);
}

char *xcmalloc(uint size)
{
  char *p = xmalloc(size);
  if (size) memset(p, 0, size);
  return(p);
}

void x_x_xfree(char **p)
{
  if (*p != (char *)NULL){
    free(*p);
    *p = (char*)NULL;
  }
}

int strmaxcpy(char *dest, char *source, int len)
/* copied max. len chars + '\0' Byte */
{
  int slen = (source != (char *)NULL) ? min(len, strlen(source)) : 0;
  if (dest == (char *)NULL) return(0);
  if (slen) memcpy(dest, source, slen);
  dest[slen] = '\0';
  return(slen);
}

char *xadd_char(char *s, int c, int maxlen)
{
  if (s && maxlen) {
    int namlen = strlen(s);
    if (maxlen > -1 && namlen >= maxlen) namlen=maxlen-1;
    s[namlen++] = c;
    s[namlen]   = '\0';
  }
  return(s);
}

static uint8 down_char(uint8 ch)
{
  if (ch > 64 && ch < 91) return(ch + 32);
  switch(ch){
    case 142:  ch =  132; break;
    case 153:  ch =  148; break;
    case 154:  ch =  129; break;
    default :break;
  }
  return(ch);
}

static uint8 up_char(uint8 ch)
{
  if (ch > 96 && ch < 123) return(ch - 32);
  switch(ch) {
    case 132:  ch =  142; break;
    case 148:  ch =  153; break;
    case 129:  ch =  154; break;
    default :  break;
  }
  return(ch);
}

uint8 *upstr(uint8 *s)
{
  if (!s) return((uint8*)NULL);
  for (;*s;s++) *s=up_char(*s);
  return(s);
}

void deb(uint8 *s)
{
  if (!s || !*s) return;
  else {
    uint8 *p = s + strlen(s);
    while (p > s && (*--p==32 || *p==9));;
    if (*p==32 || *p==9) *p='\0';
    else *(p+1) = '\0';
  }
}

void leb(uint8 *s)
{
  if (!s || !*s || (*s != 32 && *s != 9)) return;
  else {
    uint8 *p = s;
    for (;*p && *p!=32 && *p!=9;p++);;
    strcpy(s, p);
  }
}

void korrpath(char *s)
{
  if (!s) return;
  for (;*s;s++) {
    if (*s=='\\') *s='/';
    else *s=down_char(*s);
  }
}

void get_path_fn(char *s, char *p, char *fn)
{
  int j= strlen(s);
  if (p  != (char *)NULL)  p[0]  = 0;
  if (fn != (char*) NULL)  fn[0] = 0;
  if (!j) return;
  if (s[0] == '.' && (s[1] == 0 || (s[1] == '.' && s[2] == 0) ) ) {
    if (p != (char *)NULL) {
      strcpy(p, s);
      strcat(p, "/");
    }
    if (fn != (char *)NULL) fn[0] = 0;
    return;
  }
  while (j--){
    if ((s[j] == '/') || (s[j] == ':') ) {
      if (fn != (char *)NULL) strcpy(fn, s+j+1);
      if (p != (char *)NULL) {
        strncpy(p, s, j+1);
        p[j+1] = 0;
      }
      return;
    }
  }
  if (fn != (char *)NULL) strcpy(fn, s);  /* no path */
}


typedef struct {
  uint16  adr1;
  uint16  adr2;
  char    reserve[6];
  uint32  ladrs[3];
  uint16  father_psp_seg;
  char    handles[20];
  uint16  environ_seg;
} PROG_PSP;

typedef struct {
  uint8  kennung;
  uint16 prozess_seg;
  uint16 blocks;
} SPEICH_BLOCK;

static char *getglobenvironment(uint16 *maxsize, uint16 *aktsize)
{
  static uint16 globmaxenvsize=0;
  static char  *globenviron=NULL;
  if (globenviron == (char *) NULL) {
    PROG_PSP *mypsp     = MK_FP(_psp, 0);
    PROG_PSP *fatherpsp = MK_FP(mypsp->father_psp_seg,   0);
    SPEICH_BLOCK *spb   = MK_FP(fatherpsp->environ_seg-1, 0);
    globenviron = (char *)MK_FP(fatherpsp->environ_seg, 0);
    globmaxenvsize      = spb->blocks * 16;
  }
  if (globmaxenvsize){
    char *search    = globenviron;
    char *maxsearch = search+globmaxenvsize;
    while (*search && search < maxsearch) {
      int slen=strlen(search);
      search+=(slen+1);
    }
    *aktsize = max(2, (uint16)(search+1 - globenviron));
  } else *aktsize=0;
  *maxsize = globmaxenvsize;
  /*
  printf("globenv=%p maxsize=%d, aktsize=%d\n", globenviron, globmaxenvsize, *aktsize);
  */
  return(globenviron);
}

char *getglobenv(char *option)
{
  uint16 maxenvsize;
  uint16 aktenvsize;
  char   *search = getglobenvironment(&maxenvsize, &aktenvsize);
  int length = (option == NULL) ? 0 : strlen(option);
  if (aktenvsize && length){
    char *maxsearch=search+aktenvsize;
    while (*search && search < maxsearch) {
      int slen=strlen(search);
      if (slen > length && (*(search + length) == '=')
         && (strncmp(search, option, length) == 0)) {
        /*
        printf("GET GLOB %s=%s\n", option, search+length+1);
        */
        return(search + length + 1);
      }
      search+=(slen+1);
    }
  }
  return(NULL);
}

int putglobenv(char *option)
{
  uint16 maxenvsize;
  uint16 aktenvsize;
  char   *search   = getglobenvironment(&maxenvsize, &aktenvsize);
  int    optionlen = (option == NULL) ? 0 : strlen(option);
  /*
  printf("PUT GLOB option=%s\n", option);
  */
  if (optionlen && maxenvsize){
    int    length;
    char   *equal;
    for (equal = option; *equal && *equal != '='; equal++);;
    length = (int) (equal - option);
    if (length > 0 && *equal == '='){
      char *maxsearch=search+aktenvsize;
      while (*search && search < maxsearch) {
        int slen    = strlen(search);
        char *nextp = search+slen+1;
        if (slen > length && (*(search + length) == '=')
          && (strncmp(search, option, length) == 0)) { /* gefunden */
          int diffsize   = optionlen-slen;
          if (diffsize){
            int movesize = (int)(maxsearch  - nextp);
            if (diffsize > (int)(maxenvsize - aktenvsize))
               return(-1); /* Kein Platz mehr */
            if (!*(equal+1)) diffsize -= (length+2);
            xmemmove(nextp+diffsize, nextp, movesize);
          }
          if (*(equal+1)) strcpy(search, option);
          return(0);
        }
        search=nextp;
      }
      /* nicht gefunden , nun eintragen, falls m”glich */
      if (*(equal+1) && optionlen < maxenvsize - aktenvsize) {
        strcpy(search, option);
        *(search+optionlen+1) = '\0'; /* letzter Eintrag '\0' nicht vergessen */
        return(0);
      } else return(-1);
    }
  }
  return(-1);
}

