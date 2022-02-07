/* nwcrypt.h */
extern void shuffle(unsigned char *lon,
                    const unsigned char *buf, int buflen,
	            unsigned char *target);

extern void nw_encrypt(unsigned char *fra,
                         unsigned char *buf,unsigned char *til);
