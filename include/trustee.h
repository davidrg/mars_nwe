/* trustee.h  13-Apr-00 */
#ifndef _TRUSTEE_H_
#define _TRUSTEE_H_

/* some TRUSTEE defines */
#define TRUSTEE_R    0x01  /* file Read rights      */
#define TRUSTEE_W    0x02  /* file Write rights     */
#define TRUSTEE_O    0x04  /* file Open rights      */
#define TRUSTEE_C    0x08  /* file/dir Creat rights */
#define TRUSTEE_E    0x10  /* file/dir Erase rights */
#define TRUSTEE_A    0x20  /* Access control,change trustees,inherited rights */
#define TRUSTEE_F    0x40  /* File scan rights     */
#define TRUSTEE_M    0x80  /* Modify filename, attrib rights  */
/* ......extended Trustees ................. */
#define TRUSTEE_S   0x100  /* Supervisor rights     */

/* mars_nwe only, idea and patches from Christoph Scheeder  */
#define TRUSTEE_T   0x200  /* See this dir/file only */

#define MAX_TRUSTEE_MASK  0x1FF

typedef struct {
  uint32 id;
  int    trustee;
} NW_OIC;

extern void tru_free_cache(int volume);
extern void tru_init_trustees(int count, uint32 *grps);

extern unsigned int tru_vol_sernum(int volume, int mode);

extern void tru_free_file_trustees_from_disk(int volume, 
                         int dev, ino_t inode);

extern int tru_del_trustee(int volume, uint8 *unixname, 
                       struct stat *stb, uint32 id);

extern int tru_get_id_trustee(int volume, uint8 *unixname, 
                         struct stat *stb, uint32 id);

extern int tru_add_trustee_set(int volume, uint8 *unixname, 
                              struct stat *stb,
                              int count, NW_OIC *nwoic);

extern int tru_get_trustee_set(int volume, uint8 *unixname, 
                       struct stat *stb,
                       int sequence,
                       int maxcount, uint32 *ids, int *trustees); 

extern int tru_set_inherited_mask(int volume, uint8 *unixname, 
                       struct stat *stb, int new_mask);

extern int tru_get_inherited_mask(int volume, uint8 *unixname, 
                       struct stat *stb);

extern int tru_get_eff_rights(int volume, uint8 *unixname, struct stat *stb);
extern int tru_eff_rights_exists(int volume, uint8 *unixname, struct stat *stb,
                           int lookfor);


#endif
