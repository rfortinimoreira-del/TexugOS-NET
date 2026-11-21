.section .text
.code32
.global _start

_start:
    movl $3, %eax
    movl $72, %ebx
    int $0x80
    movl $3, %eax
    movl $101, %ebx
    int $0x80
    movl $3, %eax
    movl $108, %ebx
    int $0x80
    movl $3, %eax
    movl $108, %ebx
    int $0x80
    movl $3, %eax
    movl $111, %ebx
    int $0x80
    movl $3, %eax
    movl $32, %ebx
    int $0x80
    movl $3, %eax
    movl $84, %ebx
    int $0x80
    movl $3, %eax
    movl $69, %ebx
    int $0x80
    movl $3, %eax
    movl $70, %ebx
    int $0x80
    movl $3, %eax
    movl $33, %ebx
    int $0x80
    movl $3, %eax
    movl $10, %ebx
    int $0x80
    movl $2, %eax
    xorl %ebx, %ebx
    int $0x80
