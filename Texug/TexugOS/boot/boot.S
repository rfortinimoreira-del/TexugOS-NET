.section .text
.code16
.global _start

_start:
    ljmp $0x0000, $boot_entry

boot_entry:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    cld
    sti

    movb %dl, boot_drive
    movb %dl, 0x5400
    movw $boot_message, %si
    call print_string

    movb boot_drive, %dl
    movw $0x55aa, %bx
    movb $0x41, %ah
    int $0x13
    jc chs_load
    cmpw $0xaa55, %bx
    jne chs_load
    testw $1, %cx
    jz chs_load

    movw $1, current_lba
    movw $8, read_chunks
    movw $0x1000, destination_segment

edd_read_next:
    movb boot_drive, %dl
    movw $disk_packet, %si
    movb $0x42, %ah
    int $0x13
    jc chs_load
    addw $64, current_lba
    addl $64, disk_packet + 8
    addw $0x0800, destination_segment
    movw destination_segment, %ax
    movw %ax, disk_packet + 6
    decw read_chunks
    jnz edd_read_next
    jmp payload_loaded

chs_load:
    movb boot_drive, %dl
    movb $0x08, %ah
    int $0x13
    jc disk_error
    andb $0x3f, %cl
    testb %cl, %cl
    jz disk_error
    movb %cl, sectors_per_track
    xorw %ax, %ax
    movb %dh, %al
    incw %ax
    movw %ax, heads_per_cylinder
    movw $1, current_lba
    movw $512, sectors_left
    movw $0x1000, destination_segment

chs_read_next:
    movw current_lba, %ax
    xorw %dx, %dx
    divw sectors_per_track
    movb %dl, %cl
    incb %cl
    xorw %dx, %dx
    divw heads_per_cylinder
    movw %ax, current_cylinder
    movb %dl, %dh
    movw current_cylinder, %ax
    movb %al, %ch
    shrw $2, %ax
    andb $0xc0, %al
    orb %al, %cl
    movb boot_drive, %dl
    movw destination_segment, %ax
    movw %ax, %es
    xorw %bx, %bx
    movb $0x02, %ah
    movb $1, %al
    int $0x13
    jc disk_error
    incw current_lba
    addw $0x0020, destination_segment
    decw sectors_left
    jnz chs_read_next

payload_loaded:
    ljmp $0x1000, $0x0000

disk_error:
    movw $disk_message, %si

boot_error:
    call print_string
halt:
    cli
    hlt
    jmp halt

print_string:
    lodsb
    testb %al, %al
    jz print_done
    movb $0x0e, %ah
    movw $0x0007, %bx
    int $0x10
    jmp print_string
print_done:
    ret

boot_message:
    .asciz "TexugOS: BIOS boot\r\n"
disk_message:
    .asciz "TexugOS: payload read failed\r\n"

.align 4
boot_drive:
    .byte 0

disk_packet:
    .byte 0x10, 0x00
    .word 64
    .word 0x0000
    .word 0x1000
    .quad 1
sectors_per_track:
    .word 0
heads_per_cylinder:
    .word 0
current_lba:
    .word 0
current_cylinder:
    .word 0
sectors_left:
    .word 0
destination_segment:
    .word 0
read_chunks:
    .word 0

.org 510
.word 0xaa55
