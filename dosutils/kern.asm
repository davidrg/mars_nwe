; kern.asm: 20-Nov-93, 21:52
IDEAL
P286
MODEL LARGE
; Fuer Turboc gerettet werden muessen folgende Register:
; BP, SS, SP, DS, CS u. SI, DI

MACRO P_START
   push    bp
   mov     bp, sp
ENDM

MACRO P_END
   pop     bp
ENDM

MACRO PUSH_REGS
   push    ds
   push    si
   push    di
ENDM

MACRO POP_REGS
   pop     di
   pop     si
   pop     ds
ENDM

;; EXTRN   _esr_routine:FAR

PUBLIC  _IPXinit;
PUBLIC  _IPXopen_socket;
PUBLIC  _IPXclose_socket;
PUBLIC  _IPXlisten;
;; PUBLIC  _asm_esr_routine;
PUBLIC  _xmemmove;
PUBLIC  _Net_Call;

DATASEG
enterIPX  DD FAR

CODESEG
PROC _IPXinit;
   P_START
   PUSH_REGS
   mov     ax,  7A00h
   int     2Fh
   cmp     al,  0FFh
   jne     @@fertig
   mov     cx, @data
   mov     ds, cx
   mov     [WORD PTR enterIPX],   di
   mov     ax, es
   mov     [WORD PTR enterIPX+2], ax
   mov     al, 1 ; OK
@@fertig:
   mov    ah, 0
   POP_REGS
   P_END
   ret         ;  OK = 1 ; nicht ok = 0
ENDP

PROC  _xmemmove;
      ARG     z:DATAPTR, q:DATAPTR, nmbr:WORD; Argumente
      cli             ; Disable Interrupts
      push    bp
      mov     bp,sp
      mov     cx, [nmbr];
      or      cx, cx;
      jz      @@fertig; Anzahl ist 0;
      push    ds;
      push    si;
      push    di;
      pushf
      lds     si, [q] ; Quelle
      les     di, [z] ; Ziel
      cmp     di, si  ;
      jl      @@L1    ; Ziel ist kleiner
      std             ; Richtungsflag setzen
      dec     cx
      add     di, cx  ; Von oben nach unten kopieren
      add     si, cx  ;
      inc     cx      ; alten Wert wiederherstellen
      jmp     @@L2;
      @@L1:
      cld             ; Richtungsflag loeschen
      @@L2:           ; und nun das eigentliche kopieren
      REP     movsb   ;
      popf
      pop     di;
      pop     si;
      pop     ds;
      @@fertig:
      pop     bp;
      sti             ; enable Interrupts
      ret
ENDP

PROC  _IPXopen_socket;
      ARG  sock:WORD, live:WORD
      P_START
      PUSH_REGS
      mov     ax, [live]
      mov     dx, [sock]
      mov     bx, @data
      mov     ds, bx
      mov     bx, 0
      call    [enterIPX]
      cmp     al,  0FFh
      jne     @@L1
      mov     ax, -1 ; Socket already open
      jmp     @@L3
@@L1:
      cmp     al,  0FEh
      jne     @@L2
      mov     ax, -2 ; Socket Table full
      jmp     @@L3
@@L2:
      mov     ax, dx
@@L3:
      POP_REGS
      P_END
      ret
ENDP

PROC  _IPXclose_socket;
      ARG  sock:WORD
      P_START
      PUSH_REGS
      mov     dx, [sock]
      mov     bx, @data
      mov     ds, bx
      mov     bx, 1
      call    [enterIPX]
      POP_REGS
      P_END
      ret
ENDP

PROC  _IPXlisten;
      ARG  ecb:DATAPTR
      P_START
      PUSH_REGS
      les     si, [ecb] ; Adresse ecb
      mov     bx, @data
      mov     ds, bx
      mov     bx, 4
      call    [enterIPX]
      POP_REGS
      P_END
      mov  ah, 0
      ret
ENDP

;; PROC  _asm_esr_routine;
;;       push    bp;
;;       PUSH_REGS;
;;       mov     ax, @data
;;       mov     ds, ax     ;  FÅr C PROGRAMM
;;       push    es; Adressegment vom EBC
;;       push    si; Adressoffset vom ECB
;;       call    _esr_routine;  C ROUTINE
;;       pop     si;
;;       pop     es;
;;       POP_REGS;
;;       pop     bp;
;;       cli   ; no Interrupt says NOVELL
;;       ret
;; ENDP


PROC  _Net_Call;
      ARG     func:WORD, req:DATAPTR, repl:DATAPTR; Argumente
      push    bp
      mov     bp, sp
      mov     ax, [func];
      push    ds;
      push    si;
      push    di;
      pushf
      lds     si, [req]  ; Request
      les     di, [repl] ; Reply
      int     21h
      popf
      pop     di;
      pop     si;
      pop     ds;
      pop     bp;
      mov     ah, 0
      ret
ENDP

END








