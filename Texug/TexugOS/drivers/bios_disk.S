.section .text
.code32
.global bios_int13_call

bios_int13_call:
    cli
    pushal
    movl 36(%esp), %eax
    movb %al, 0x5401
    movl %esp, bios_saved_esp
    movl $bios16_start, %esi
    movl $0x7000, %edi
    movl $(bios16_end - bios16_start), %ecx
    cld
    rep movsb
    sgdt bios_saved_gdt
    lgdt bios_thunk_gdt_descriptor
    sidt bios_saved_idt
    lidt bios_real_idt
    movl %cr0, %eax
    movl %eax, bios_saved_cr0
    andl $0x7fffffff, %eax
    movl %eax, %cr0
    ljmp $0x18, $(bios_pm16_entry - bios16_start)

.section .text.bios16,"ax"
.code16
bios16_start:

.code16
bios_pm16_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl $0x7c00, %esp
    movl %cr0, %eax
    andl $0xfffffffe, %eax
    movl %eax, %cr0
    ljmp $0x0700, $(bios_real_entry - bios16_start)

bios_real_entry:
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    movb 0x5401, %al
    cmpb $1, %al
    je bios_probe
    cmpb $2, %al
    je bios_parameters
    cmpb $4, %al
    je bios_write
    cmpb $5, %al
    je bios_video_mode
    cmpb $6, %al
    je bios_text_mode
    cmpb $7, %al
    je bios_set_video_mode

bios_read:
    movb 0x5400, %dl
    movw $0x6000, %si
    movb $0x42, %ah
    sti
    int $0x13
    cli
    jmp bios_check_result

bios_write:
    movb 0x5400, %dl
    movw $0x6000, %si
    xorw %ax, %ax
    movb $0x43, %ah
    sti
    int $0x13
    cli
    jmp bios_check_result

bios_probe:
    movb 0x5400, %dl
    movw $0x55aa, %bx
    movb $0x41, %ah
    sti
    int $0x13
    cli
    jc bios_failure
    cmpw $0xaa55, %bx
    jne bios_failure
    testw $1, %cx
    jz bios_failure
    jmp bios_success

bios_parameters:
    movb 0x5400, %dl
    movw $0x001e, 0x6400
    movw $0x6400, %si
    movb $0x48, %ah
    sti
    int $0x13
    cli
    jmp bios_check_result

bios_video_mode:
    xorw %ax, %ax
    movw %ax, %es
    movw $0x6600, %di
    movw $0x4f01, %ax
    movw $0x0101, %cx
    sti
    int $0x10
    cli
    cmpw $0x004f, %ax
    jne bios_failure
    jmp bios_success

bios_set_video_mode:
    movw $0x4101, %bx
    movw $0x4f02, %ax
    sti
    int $0x10
    cli
    cmpw $0x004f, %ax
    jne bios_failure
    jmp bios_success

bios_text_mode:
    movw $0x0003, %ax
    sti
    int $0x10
    cli
    jmp bios_success

bios_check_result:
    jc bios_failure
bios_success:
    movb $1, 0x5402
    jmp bios_return

bios_failure:
    movb $0, 0x5402

bios_return:
    cli
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $0x18, $(bios_pm16_return - bios16_start)

bios_pm16_return:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl $0x7c00, %esp
    addr32 data32 lidt bios_saved_idt
    addr32 movl bios_saved_cr0, %eax
    movl %eax, %cr0
    ljmpl $0x08, $bios_pm32_return

.global bios16_end
bios16_end:
.code32
.section .text
bios_pm32_return:
    lgdt bios_saved_gdt
    ljmp $0x08, $bios_pm32_restored_gdt

bios_pm32_restored_gdt:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    lidt bios_saved_idt
    movl bios_saved_esp, %esp
    popal
    movzbl 0x5402, %eax
    ret

.section .data
.align 4
bios_saved_esp:
    .long 0
bios_saved_cr0:
    .long 0
bios_saved_gdt:
    .space 6
bios_saved_idt:
    .space 6
bios_real_idt:
    .word 0x03ff
    .long 0
bios_thunk_gdt:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
    .quad 0x00009a007000ffff
bios_thunk_gdt_end:
bios_thunk_gdt_descriptor:
    .word bios_thunk_gdt_end - bios_thunk_gdt - 1
    .long bios_thunk_gdt

.section .note.GNU-stack,"",@progbits
