.section .text
.code32
.global user_mode_enter
.global user_mode_resume
.extern user_mode_kernel_esp

user_mode_enter:
    movl %esp, user_mode_kernel_esp
    pushfl
    popl %eax
    andl $0xffffbfff, %eax
    movl %eax, user_mode_kernel_eflags
    movl %ebx, user_mode_kernel_ebx
    movl %esi, user_mode_kernel_esi
    movl %edi, user_mode_kernel_edi
    movl %ebp, user_mode_kernel_ebp
    movl 4(%esp), %eax
    movl 8(%esp), %edx
    movl 12(%esp), %ebx
    movw $0x23, %cx
    movw %cx, %ds
    movw %cx, %es
    movw %cx, %fs
    movw %cx, %gs
    pushl $0x23
    pushl %edx
    pushl $0x202
    pushl $0x1b
    pushl %eax
    iret

user_mode_resume:
    movl user_mode_kernel_esp, %esp
    movl user_mode_kernel_ebx, %ebx
    movl user_mode_kernel_esi, %esi
    movl user_mode_kernel_edi, %edi
    movl user_mode_kernel_ebp, %ebp
    pushl user_mode_kernel_eflags
    popfl
    ret

.section .bss
.align 4
.global user_mode_kernel_esp
user_mode_kernel_esp:
    .long 0
user_mode_kernel_ebx:
    .long 0
user_mode_kernel_esi:
    .long 0
user_mode_kernel_edi:
    .long 0
user_mode_kernel_ebp:
    .long 0
user_mode_kernel_eflags:
    .long 0

.section .userimage,"a"
.code32
.global user_test_program_start
.global user_test_program_end
.global user_probe_program_start
.global user_probe_program_end

user_test_program_start:
    pushl $0x54455847
    popl %ecx
    movl $1, %eax
    int $0x80
    cmpl %ecx, %eax
    jne user_test_failed
    xorl %ebx, %ebx
    jmp user_test_exit
user_test_failed:
    movl $1, %ebx
user_test_exit:
    movl $2, %eax
    int $0x80
    hlt
    jmp user_test_exit
user_test_program_end:

user_probe_program_start:
    movl (%ebx), %eax
    movl $2, %eax
    xorl %ebx, %ebx
    int $0x80
    hlt
    jmp user_probe_program_start
user_probe_program_end:

.section .note.GNU-stack,"",@progbits
