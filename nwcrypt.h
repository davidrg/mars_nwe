/* nwcrypt.h 22-Jun-96 */
extern void shuffle(unsigned char *lon,
                    const unsigned char *buf, int buflen,
	            unsigned char *target);

extern void nw_encrypt(unsigned char *fra,
                         unsigned char *buf,unsigned char *til);


extern void nw_decrypt_newpass(char *oldpwd, char *newpwd, char *undecr);
extern void newpassencrypt(char *old, char *new, char *out);

