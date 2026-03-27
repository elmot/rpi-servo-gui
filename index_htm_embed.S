/* Embed index.htm as a binary blob */
    .section .rodata.index_htm, "a", %progbits
    .global file_index_html
    .global file_index_html_end
    .balign 4
file_index_html:
    .incbin "index.htm"
file_index_html_end:
