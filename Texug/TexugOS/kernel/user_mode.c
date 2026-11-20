#include <stdint.h>

#include "console.h"
#include "frame_allocator.h"
#include "paging.h"
#include "user_mode.h"

#define USER_PAGE_SIZE 4096u
#define USER_IDENTITY_LIMIT 0x01000000u
#define USER_EXIT_SYSCALL 2u
#define USER_TEST_SYSCALL 1u
#define USER_TEST_RESULT 0x54455847u

extern const uint8_t user_test_program_start[];
extern const uint8_t user_test_program_end[];
extern const uint8_t user_probe_program_start[];
extern const uint8_t user_probe_program_end[];
extern void user_mode_enter(uint32_t entry, uint32_t stack,
                            uint32_t probe_address);
extern void user_mode_resume(void) __attribute__((noreturn));

static volatile uint32_t syscall_count;
static volatile uint32_t test_result;
static volatile uint32_t fault_vector;
static volatile uint32_t fault_error_code;
static volatile uint32_t fault_address;
static volatile uint32_t probe_address;

static void copy_program(uint8_t *destination, const uint8_t *start,
                         const uint8_t *end)
{
    uint32_t size = (uint32_t)(end - start);
    for (uint32_t index = 0u; index < size; ++index) {
        destination[index] = start[index];
    }
}

uint32_t user_mode_handle_syscall(uint32_t number, uint32_t argument)
{
    if (number == USER_TEST_SYSCALL && syscall_count == 0u) {
        ++syscall_count;
        return USER_TEST_RESULT;
    }
    if (number == USER_EXIT_SYSCALL) {
        test_result = argument;
        user_mode_resume();
    }

    test_result = 1u;
    user_mode_resume();
}

void user_mode_abort(uint32_t vector, uint32_t error_code)
{
    fault_vector = vector;
    fault_error_code = error_code;
    fault_address = paging_fault_address();
    test_result = vector == 14u && fault_address == probe_address &&
                          (error_code & 0x07u) == 0x05u
                      ? 0u
                      : 1u;
    user_mode_resume();
}

int user_mode_self_test(void)
{
    uint32_t code_frame = frame_allocate();
    uint32_t stack_frame = frame_allocate();
    if (code_frame == FRAME_INVALID || stack_frame == FRAME_INVALID ||
        code_frame >= USER_IDENTITY_LIMIT ||
        stack_frame >= USER_IDENTITY_LIMIT ||
        (uint32_t)(user_test_program_end - user_test_program_start) >
            USER_PAGE_SIZE ||
        (uint32_t)(user_probe_program_end - user_probe_program_start) >
            USER_PAGE_SIZE) {
        if (code_frame != FRAME_INVALID) {
            (void)frame_free(code_frame);
        }
        if (stack_frame != FRAME_INVALID) {
            (void)frame_free(stack_frame);
        }
        console_write("Ring 3: user page allocation failed.\n");
        return 0;
    }

    uint8_t *code = (uint8_t *)(uintptr_t)code_frame;
    copy_program(code, user_test_program_start, user_test_program_end);
    if (!paging_set_user_page(code_frame, 1, 0) ||
        !paging_set_user_page(stack_frame, 1, 1)) {
        (void)paging_set_user_page(code_frame, 0, 0);
        (void)paging_set_user_page(stack_frame, 0, 0);
        (void)frame_free(code_frame);
        (void)frame_free(stack_frame);
        console_write("Ring 3: user page mapping failed.\n");
        return 0;
    }

    syscall_count = 0u;
    test_result = 1u;
    fault_vector = 0u;
    fault_error_code = 0u;
    fault_address = 0u;
    user_mode_enter(code_frame, stack_frame + USER_PAGE_SIZE - 16u, 0u);
    int passed = syscall_count == 1u && test_result == 0u;
    if (!passed) {
        console_write("Ring 3: syscall test failed; calls=");
        console_write_u32(syscall_count);
        console_write(", result=0x");
        console_write_hex32(test_result);
        console_write("\n");
    }

    if (passed) {
        copy_program(code, user_probe_program_start,
                     user_probe_program_end);
        test_result = 1u;
        fault_vector = 0u;
        fault_error_code = 0u;
        fault_address = 0u;
        probe_address = (uint32_t)(uintptr_t)&syscall_count;
        user_mode_enter(code_frame, stack_frame + USER_PAGE_SIZE - 16u,
                        probe_address);
        passed = test_result == 0u;
        if (!passed) {
            console_write("Ring 3: isolation test failed; vector=");
            console_write_u32(fault_vector);
            console_write(", error=0x");
            console_write_hex32(fault_error_code);
            console_write(", CR2=0x");
            console_write_hex32(fault_address);
            console_write("\n");
        }
    }

    int code_protected = paging_set_user_page(code_frame, 0, 0);
    int stack_protected = paging_set_user_page(stack_frame, 0, 0);
    int code_released = frame_free(code_frame);
    int stack_released = frame_free(stack_frame);
    if (!code_protected || !stack_protected ||
        !code_released || !stack_released) {
        console_write("Ring 3: user page cleanup failed.\n");
        return 0;
    }
    return passed;
}
