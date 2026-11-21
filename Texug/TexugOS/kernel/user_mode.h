#ifndef TEXUGOS_USER_MODE_H
#define TEXUGOS_USER_MODE_H

#include <stdint.h>

int user_mode_self_test(void);
int user_mode_execute(const uint8_t *binary, uint32_t binary_size,
                      uint32_t entry_offset, uint32_t *exit_code);
uint32_t user_mode_handle_syscall(uint32_t number, uint32_t argument);
void user_mode_abort(uint32_t vector, uint32_t error_code)
    __attribute__((noreturn));

#endif
