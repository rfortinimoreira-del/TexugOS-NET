#ifndef TEXUGOS_BIOS_VIDEO_H
#define TEXUGOS_BIOS_VIDEO_H

#include <stdint.h>

struct bios_video_mode {
    uint32_t framebuffer;
    uint32_t pitch;
    uint8_t bytes_per_pixel;
};

int bios_video_set_640x480_mode(struct bios_video_mode *mode);
int bios_video_set_text_mode(void);

#endif
