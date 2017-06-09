    ctc     .equ 28h

    .org    100h        ;run as a cpm transient

    call   setint

-:
    LD      A,13
    RST     08h
    .DB     $9E
    ld      HL,(counter)
    RST     08h
    .DB     $A9
    jr      {-}


setint
    di                  ;stop any interrupts for now
    ld      b,2         ;do this loop twice so vdp knows its a setup
    ld      a,3         ;clear all channels of ctc
kill
    out     (ctc),a
    out     (ctc+1),a
    out     (ctc+2),a
    out     (ctc+3),a   ;all channels now done
    djnz    kill
    im      2           ;we want interrupt mode 2
    ld      hl,intsrv   ;our interrupt routine must be on
                        ;an eight byte boundary
    ld      a,h
    ld      i,a         ;put msb in int vector
    ld      a,l
    out     (ctc),a     ;lsb into ctc
    ld      a,0c5h      ;command word see text
    out     (ctc),a
    ld      a,0         ;every line
    out     (ctc),a
    in      a,(9)       ;clear vdp int flag
    ei                  ;enable ints
    ret


    .align  8
intsrv
    .dw     intrtn,intrtn,intrtn,intrtn

intrtn:
    di                  ;no interrupts please
    in      a,(9)       ;has flag been set
    bit     7,a         ;if yes bit 7 will be set
    jr      z,{+}

    ld      hl,counter  ;THIS IS INT ROUTINE
    inc     (hl)
    in      a,(9)       ;clear any pending vdp ints

+:  ei
    reti

counter
    .db     00
