#include <stdint.h>

#include "bios_video.h"
#include "pic.h"

#define BIOS_VIDEO_INFO_ADDRESS 0x6600u
#define BIOS_VIDEO_INFO_OPERATION 5u
#define BIOS_TEXT_OPERATION 6u
#define BIOS_VIDEO_SET_OPERATION 7u
#define BIOS_VIDEO_WIDTH 640u
#define BIOS_VIDEO_HEIGHT 480u
#define BIOS_VIDEO_BYTES_PER_PIXEL 1u

extern int bios_int13_call(uint32_t operation);

static uint16_t read_u16(volatile const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_u32(volatile const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

int bios_video_set_640x480_mode(struct bios_video_mode *mode)
{
    if (mode == 0) {
        return 0;
    }

    uint32_t flags = pic_enter_bios_mode();
    int result = bios_int13_call(BIOS_VIDEO_INFO_OPERATION);
    pic_leave_bios_mode(flags);
    if (!result) {
        return 0;
    }

    volatile const uint8_t *info =
        (volatile const uint8_t *)BIOS_VIDEO_INFO_ADDRESS;
    uint16_t attributes = read_u16(info);
    uint16_t width = read_u16(&info[18]);
    uint16_t height = read_u16(&info[20]);
    uint16_t linear_pitch = read_u16(&info[50]);
    uint32_t framebuffer = read_u32(&info[40]);
    if ((attributes & 0x0091u) != 0x0091u ||
        width != BIOS_VIDEO_WIDTH || height != BIOS_VIDEO_HEIGHT ||
        info[25] != 8u || info[27] != 4u ||
        linear_pitch < BIOS_VIDEO_WIDTH * BIOS_VIDEO_BYTES_PER_PIXEL ||
        framebuffer == 0u ||
        framebuffer > UINT32_MAX -
                          (uint32_t)linear_pitch * BIOS_VIDEO_HEIGHT) {
        return 0;
    }

    flags = pic_enter_bios_mode();
    result = bios_int13_call(BIOS_VIDEO_SET_OPERATION);
    pic_leave_bios_mode(flags);
    if (!result) {
        return 0;
    }

    mode->framebuffer = framebuffer;
    mode->pitch = linear_pitch;
    mode->bytes_per_pixel = BIOS_VIDEO_BYTES_PER_PIXEL;
    return 1;
}

int bios_video_set_text_mode(void)
{
    uint32_t flags = pic_enter_bios_mode();
    int result = bios_int13_call(BIOS_TEXT_OPERATION);
    pic_leave_bios_mode(flags);
    return result;
}
