#include <stdint.h>

#include "secure_random.h"

#define CPUID_RDRAND_BIT (1u << 30)
#define EFLAGS_ID_BIT (1u << 21)

static int cpuid_available(void)
{
#if defined(__x86_64__)
    return 1;
#elif defined(__i386__)
    uint32_t original;
    uint32_t toggled;
    __asm__ volatile ("pushfl; popl %0" : "=r"(original));
    toggled = original ^ EFLAGS_ID_BIT;
    __asm__ volatile ("pushl %0; popfl" : : "r"(toggled) : "cc");
    __asm__ volatile ("pushfl; popl %0" : "=r"(toggled));
    __asm__ volatile ("pushl %0; popfl" : : "r"(original) : "cc");
    return ((original ^ toggled) & EFLAGS_ID_BIT) != 0u;
#else
    return 0;
#endif
}

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf), "c"(0u));
}

static int rdrand_supported(void)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    if (!cpuid_available()) {
        return 0;
    }
    cpuid(0u, &eax, &ebx, &ecx, &edx);
    if (eax < 1u) {
        return 0;
    }
    cpuid(1u, &eax, &ebx, &ecx, &edx);
    return (ecx & CPUID_RDRAND_BIT) != 0u;
}

int wpa2_secure_random_available(void)
{
    return rdrand_supported();
}

static int rdrand32(uint32_t *value)
{
    uint8_t success;
    __asm__ volatile (".byte 0x0f, 0xc7, 0xf0; setc %1"
                      : "=a"(*value), "=qm"(success)
                      :
                      : "cc");
    return success != 0u;
}

int wpa2_secure_random_fill(void *context, uint8_t *buffer,
                            uint32_t length)
{
    (void)context;
    if ((buffer == 0 && length != 0u) || length > 1024u ||
        !wpa2_secure_random_available()) {
        return 0;
    }
    uint32_t offset = 0u;
    while (offset < length) {
        uint32_t value = 0u;
        uint8_t generated = 0u;
        for (uint32_t attempt = 0u; attempt < 10u; ++attempt) {
            if (rdrand32(&value)) {
                generated = 1u;
                break;
            }
        }
        if (generated == 0u) {
            return 0;
        }
        for (uint32_t index = 0u; index < 4u && offset < length; ++index) {
            buffer[offset++] = (uint8_t)(value >> (index * 8u));
        }
    }
    return 1;
}
