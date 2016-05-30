        .section .debug_info
.Lcu1_begin:
        .4byte        .Lcu1_end - .Lcu1_start
.Lcu1_start:
        .2byte        4                 /* Version */
        .4byte        .Labbrev1_begin   /* Abbrevs */
        .byte        4                  /* Pointer size */
        .uleb128        2               /* Abbrev (DW_TAG_compile_unit) */
        .4byte        main
        .4byte        main+0x10000
.Llabel1:
        .uleb128        3               /* Abbrev (DW_TAG_base_type) */
        .sleb128        4
        .sleb128        0x5
        .ascii        "integer\0"
        .uleb128        4               /* Abbrev (DW_TAG_subprogram) */
        .ascii        "main\0"
        .byte        1
        .4byte        main
        .4byte        main+0x10000
        .uleb128        5               /* Abbrev (DW_TAG_lexical_block) */
        .uleb128        6               /* Abbrev (DW_TAG_variable) */
        .ascii        "testvar\0"
        .4byte        .Llabel1 - .Lcu1_begin
        .byte        1
        .uleb128        .Lexpr_end3 - .Lexpr_start2/* expression */
.Lexpr_start2:
        .byte        0x03               /* DW_OP_addr */
        .4byte        main
.Lexpr_end3:
        .byte        0x0                /* Terminate children */
        .byte        0x0                /* Terminate children */
        .byte        0x0                /* Terminate children */
.Lcu1_end:
        .section .debug_abbrev
.Labbrev1_begin:
        .uleb128        2               /* Abbrev start */
        .uleb128        0x11            /* DW_TAG_compile_unit */
        .byte        1                  /* has_children */
        .uleb128        0x11            /* DW_AT_low_pc */
        .uleb128        0x01            /* DW_FORM_addr */
        .uleb128        0x12            /* DW_AT_high_pc */
        .uleb128        0x01            /* DW_FORM_addr */
        .byte        0x0                /* Terminator */
        .byte        0x0                /* Terminator */
        .uleb128        3               /* Abbrev start */
        .uleb128        0x24            /* DW_TAG_base_type */
        .byte        0                  /* has_children */
        .uleb128        0x0b            /* DW_AT_byte_size */
        .uleb128        0x0d            /* DW_FORM_sdata */
        .uleb128        0x3e            /* DW_AT_encoding */
        .uleb128        0x0d            /* DW_FORM_sdata */
        .uleb128        0x03            /* DW_AT_name */
        .uleb128        0x08            /* DW_FORM_string */
        .byte        0x0                /* Terminator */
        .byte        0x0                /* Terminator */
        .uleb128        4               /* Abbrev start */
        .uleb128        0x2e            /* DW_TAG_subprogram */
        .byte        1                  /* has_children */
        .uleb128        0x03            /* DW_AT_name */
        .uleb128        0x08            /* DW_FORM_string */
        .uleb128        0x3f            /* DW_AT_external */
        .uleb128        0x0c            /* DW_FORM_flag */
        .uleb128        0x11            /* DW_AT_low_pc */
        .uleb128        0x01            /* DW_FORM_addr */
        .uleb128        0x12            /* DW_AT_high_pc */
        .uleb128        0x01            /* DW_FORM_addr */
        .byte        0x0                /* Terminator */
        .byte        0x0                /* Terminator */
        .uleb128        5               /* Abbrev start */
        .uleb128        0x0b            /* DW_TAG_lexical_block */
        .byte        1                  /* has_children */
        .byte        0x0                /* Terminator */
        .byte        0x0                /* Terminator */
        .uleb128        6               /* Abbrev start */
        .uleb128        0x34            /* DW_TAG_variable */
        .byte        0                  /* has_children */
        .uleb128        0x03            /* DW_AT_name */
        .uleb128        0x08            /* DW_FORM_string */
        .uleb128        0x49            /* DW_AT_type */
        .uleb128        0x13            /* DW_FORM_ref4 */
        .uleb128        0x3f            /* DW_AT_external */
        .uleb128        0x0c            /* DW_FORM_flag */
        .uleb128        0x02            /* DW_AT_location */
        .uleb128        0x09            /* SPECIAL_expr */
        .byte        0x0                /* Terminator */
        .byte        0x0                /* Terminator */
        .byte        0x0                /* Terminator */
        .byte        0x0                /* Terminator */
