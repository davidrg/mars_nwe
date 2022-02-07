/*
 * nameos2.h: 08-Aug-96
 *
 * (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany
 */
#ifndef _NAMEOS2_H_
#define _NAMEOS2_H_
#if WITH_NAME_SPACE_CALLS

extern void mangle_os2_name(NW_VOL *vol, uint8 *unixname, uint8 *pp, int len);
extern int fn_os2_match(uint8 *s, uint8 *p, int soptions);

#endif
#endif
