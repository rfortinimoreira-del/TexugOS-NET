.section .boot16,"ax"
.code16
.global stage2_entry

stage2_entry:
    cli
    cld
    movw $0x1000, %ax
    movw %ax, %ds
    xorw %ax, %ax
    movw %ax, %es

    movw $(memory_message - stage2_entry), %si
    call real_mode_print

    xorl %ebx, %ebx
    xorw %bp, %bp
    movw $0x5000, %di
e820_next:
    xorw %ax, %ax
    movw %ax, %es
    movl $0xe820, %eax
    movl $0x534d4150, %edx
    movl $24, %ecx
    int $0x15
    jc e820_error
    cmpl $0x534d4150, %eax
    jne e820_error
    incw %bp
    addw $24, %di
    testl %ebx, %ebx
    jz e820_done
    cmpw $32, %bp
    jb e820_next
    jmp e820_error

e820_done:
    xorw %ax, %ax
    movw %ax, %es
    movw %bp, %es:0x4ff0

    inb $0x92, %al
    orb $0x02, %al
    andb $0xfe, %al
    outb %al, $0x92

    lgdtl (gdt_descriptor - stage2_entry)
    movl %cr0, %eax
    orl $0x01, %eax
    movl %eax, %cr0
    ljmpl $0x08, $protected_mode

e820_error:
    movw $(e820_message - stage2_entry), %si
    call real_mode_print
halt_real:
    cli
    hlt
    jmp halt_real

real_mode_print:
    lodsb
    testb %al, %al
    jz real_mode_print_done
    movb $0x0e, %ah
    movw $0x0007, %bx
    int $0x10
    jmp real_mode_print
real_mode_print_done:
    ret

memory_message:
    .asciz "TexugOS: collecting E820 memory map\r\n"
e820_message:
    .asciz "TexugOS: E820 memory map failed\r\n"

.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
    .quad 0x00009a007000ffff
    .quad 0x000092000000ffff
gdt_end:
gdt_descriptor:
    .word gdt_end - gdt - 1
    .long gdt

.code32
protected_mode:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl $0x00090000, %esp
    xorl %ebp, %ebp

    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb
    movl $kernel_boot_stack_top, %esp

    movl $0x5000, %esi
    movzwl 0x4ff0, %ecx
    pushl %ecx
    pushl %esi
    pushl $0x54455847
    call kernel_main

halt_protected:
    cli
    hlt
    jmp halt_protected

.section .bss
.align 16
.global kernel_boot_stack_top
kernel_boot_stack:
    .skip 16384
kernel_boot_stack_top:

.section .note.GNU-stack,"",@progbits
