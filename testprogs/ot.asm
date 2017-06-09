        .org    $100

        LD      HL,0

-:      LD      A,13
        RST     08h
        .DB     $9E
        RST     08h
        .DB     $A9
        INC     HL
        JR      {-}
