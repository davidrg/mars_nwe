/* nwattrib.h  01-Feb-98 */

#ifndef _NWATTRIB_H_
#define _NWATTRIB_H_

extern void   free_attr_from_disk(int dev, ino_t inode);
extern uint32 get_nw_attrib_dword(struct stat *stb, int voloptions);
extern int    set_nw_attrib_dword(struct stat *stb, int voloptions, uint32 attrib);
extern int    set_nw_attrib_byte (struct stat *stb, int voloptions, int   battrib);
#endif

