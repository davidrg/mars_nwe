/* kern.h  Assembler Routinen 20-Nov-93 */
extern int   IPXinit(void);
extern int   IPXopen_socket(UI sock, int live);
extern void  IPXclose_socket(UI sock);
extern int   IPXlisten(ECB *ecb);
extern void  asm_esr_routine(void);
extern void  esr_routine(ECB *ecb);
extern void  xmemmove(void *ziel, void *quelle, UI anz);
extern int   Net_Call(UI func, void *req, void *repl);



