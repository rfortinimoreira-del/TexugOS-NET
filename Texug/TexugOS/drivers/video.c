#include <stdint.h>

#include "bios_video.h"
#include "frame_allocator.h"
#include "mouse_cursor.h"
#include "paging.h"
#include "video.h"

#define VGA_MISC_WRITE 0x3c2u
#define VGA_SEQ_INDEX 0x3c4u
#define VGA_SEQ_DATA 0x3c5u
#define VGA_CRTC_INDEX 0x3d4u
#define VGA_CRTC_DATA 0x3d5u
#define VGA_GC_INDEX 0x3ceu
#define VGA_GC_DATA 0x3cfu
#define VGA_ATTR_INDEX_DATA 0x3c0u
#define VGA_INPUT_STATUS 0x3dau
#define VGA_DAC_WRITE_INDEX 0x3c8u
#define VGA_DAC_DATA 0x3c9u
#define VGA_FRAMEBUFFER ((volatile uint8_t *)0xa0000u)
#define VGA_FONT_HEIGHT 16u
#define VGA_FONT_STRIDE 32u
#define VGA_FONT_CHARACTERS 256u
#define VGA_FRAMEBUFFER_BYTES (VIDEO_VBE_WIDTH * VIDEO_VBE_HEIGHT)
#define VGA_PAGE_SIZE 4096u
#define VGA_FRAME_COUNT (VGA_FRAMEBUFFER_BYTES / VGA_PAGE_SIZE + 1u)
#define VGA_IDENTITY_LIMIT (16u * 1024u * 1024u)

static const uint8_t mode13_sequencer[5] = {
    0x03, 0x01, 0x0f, 0x00, 0x0e,
};
static const uint8_t mode13_crtc[25] = {
    0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0xbf, 0x1f,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9c, 0x0e, 0x8f, 0x28, 0x40, 0x96, 0xb9, 0xa3, 0xff,
};
static const uint8_t mode13_graphics[9] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0f, 0xff,
};
static const uint8_t mode13_attribute[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x41, 0x00, 0x0f, 0x00, 0x00,
};

static const uint8_t mode3_sequencer[5] = {
    0x03, 0x00, 0x03, 0x00, 0x02,
};
static const uint8_t mode3_crtc[25] = {
    0x5f, 0x4f, 0x50, 0x82, 0x55, 0x81, 0xbf, 0x1f,
    0x00, 0x4f, 0x0d, 0x0e, 0x00, 0x00, 0x00, 0x50,
    0x9c, 0x0e, 0x8f, 0x28, 0x1f, 0x96, 0xb9, 0xa3, 0xff,
};
static const uint8_t mode3_graphics[9] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0e, 0x00, 0xff,
};
static const uint8_t mode3_attribute[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x0c, 0x00, 0x0f, 0x08, 0x00,
};

static const uint8_t desktop_palette[16][3] = {
    {2, 4, 8},   {4, 7, 11},   {7, 11, 17},  {11, 16, 24},
    {56, 22, 26}, {41, 34, 62}, {63, 47, 21}, {49, 52, 55},
    {20, 24, 30}, {20, 33, 61}, {14, 50, 34}, {17, 54, 57},
    {51, 30, 57}, {60, 34, 42}, {63, 52, 25}, {61, 62, 63},
};

static const uint8_t font[62][7] = {
    {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11}, /* A */
    {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e}, /* B */
    {0x0e,0x11,0x10,0x10,0x10,0x11,0x0e}, /* C */
    {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e}, /* D */
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f}, /* E */
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10}, /* F */
    {0x0e,0x11,0x10,0x17,0x11,0x11,0x0f}, /* G */
    {0x11,0x11,0x11,0x1f,0x11,0x11,0x11}, /* H */
    {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e}, /* I */
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0c}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1f}, /* L */
    {0x11,0x1b,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e}, /* O */
    {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10}, /* P */
    {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d}, /* Q */
    {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11}, /* R */
    {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e}, /* S */
    {0x1f,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0e}, /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0a,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0a}, /* W */
    {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11}, /* X */
    {0x11,0x11,0x0a,0x04,0x04,0x04,0x04}, /* Y */
    {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}, /* Z */
    {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e}, /* 0 */
    {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e}, /* 1 */
    {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f}, /* 2 */
    {0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e}, /* 3 */
    {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02}, /* 4 */
    {0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e}, /* 5 */
    {0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e}, /* 6 */
    {0x1f,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e}, /* 8 */
    {0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e}, /* 9 */
    {0x00,0x0e,0x01,0x0f,0x11,0x13,0x0d}, /* a */
    {0x10,0x10,0x1e,0x11,0x11,0x11,0x1e}, /* b */
    {0x00,0x0e,0x11,0x10,0x10,0x11,0x0e}, /* c */
    {0x01,0x01,0x0f,0x11,0x11,0x13,0x0d}, /* d */
    {0x00,0x0e,0x11,0x1f,0x10,0x11,0x0e}, /* e */
    {0x06,0x09,0x08,0x1c,0x08,0x08,0x08}, /* f */
    {0x00,0x0f,0x11,0x0f,0x01,0x11,0x0e}, /* g */
    {0x10,0x10,0x1e,0x11,0x11,0x11,0x11}, /* h */
    {0x04,0x00,0x0c,0x04,0x04,0x04,0x0e}, /* i */
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0c}, /* j */
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, /* k */
    {0x0c,0x04,0x04,0x04,0x04,0x04,0x0e}, /* l */
    {0x00,0x1a,0x15,0x15,0x15,0x15,0x15}, /* m */
    {0x00,0x1e,0x11,0x11,0x11,0x11,0x11}, /* n */
    {0x00,0x0e,0x11,0x11,0x11,0x11,0x0e}, /* o */
    {0x00,0x1e,0x11,0x11,0x1e,0x10,0x10}, /* p */
    {0x00,0x0f,0x11,0x11,0x0f,0x01,0x01}, /* q */
    {0x00,0x16,0x19,0x10,0x10,0x10,0x10}, /* r */
    {0x00,0x0f,0x10,0x0e,0x01,0x11,0x0e}, /* s */
    {0x08,0x08,0x1c,0x08,0x08,0x09,0x06}, /* t */
    {0x00,0x11,0x11,0x11,0x11,0x13,0x0d}, /* u */
    {0x00,0x11,0x11,0x11,0x11,0x0a,0x04}, /* v */
    {0x00,0x11,0x11,0x15,0x15,0x15,0x0a}, /* w */
    {0x00,0x11,0x0a,0x04,0x0a,0x11,0x11}, /* x */
    {0x00,0x11,0x11,0x0f,0x01,0x11,0x0e}, /* y */
    {0x00,0x1f,0x02,0x04,0x08,0x10,0x1f}, /* z */
};

static uint8_t saved_text_font[VGA_FONT_CHARACTERS * VGA_FONT_HEIGHT];
static uint8_t text_font_saved;
static uint32_t render_frames[VGA_FRAME_COUNT];
static uint32_t displayed_frames[VGA_FRAME_COUNT];
static uint8_t framebuffers_allocated;
static uint8_t displayed_buffer_valid;
static uint8_t vbe_mode_active;
static uint32_t vbe_framebuffer;
static uint32_t vbe_pitch;
static uint8_t vbe_bytes_per_pixel;
static uint8_t vbe_framebuffer_mapped;
static uint32_t vbe_mapped_address;
static uint32_t vbe_mapped_size;

uint32_t video_display_width(void)
{
    return vbe_mode_active ? VIDEO_VBE_WIDTH : VIDEO_WIDTH;
}

uint32_t video_display_height(void)
{
    return vbe_mode_active ? VIDEO_VBE_HEIGHT : VIDEO_HEIGHT;
}

static void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int allocate_framebuffer(uint32_t *frames)
{
    uint32_t allocated = 0u;
    for (; allocated < VGA_FRAME_COUNT; ++allocated) {
        uint32_t address = frame_allocate();
        if (address == FRAME_INVALID || address >= VGA_IDENTITY_LIMIT) {
            if (address != FRAME_INVALID) {
                (void)frame_free(address);
            }
            while (allocated != 0u) {
                (void)frame_free(frames[--allocated]);
            }
            return 0;
        }
        frames[allocated] = address;
    }
    return 1;
}

static void release_framebuffer(uint32_t *frames)
{
    for (uint32_t index = 0u; index < VGA_FRAME_COUNT; ++index) {
        (void)frame_free(frames[index]);
        frames[index] = FRAME_INVALID;
    }
}

static uint8_t framebuffer_read(const uint32_t *frames, uint32_t index)
{
    uint32_t page = index / VGA_PAGE_SIZE;
    uint32_t offset = index % VGA_PAGE_SIZE;
    const uint8_t *address = (const uint8_t *)(uintptr_t)frames[page];
    return address[offset];
}

static void framebuffer_write(uint32_t *frames, uint32_t index,
                              uint8_t color)
{
    uint32_t page = index / VGA_PAGE_SIZE;
    uint32_t offset = index % VGA_PAGE_SIZE;
    uint8_t *address = (uint8_t *)(uintptr_t)frames[page];
    address[offset] = color;
}

static void write_indexed(uint16_t index_port, uint16_t data_port,
                          uint8_t index, uint8_t value)
{
    out8(index_port, index);
    out8(data_port, value);
}

static void set_mode(uint8_t misc, const uint8_t *sequencer,
                     const uint8_t *crtc, const uint8_t *graphics,
                     const uint8_t *attribute)
{
    out8(VGA_MISC_WRITE, misc);
    for (uint8_t index = 0; index < 5u; ++index) {
        write_indexed(VGA_SEQ_INDEX, VGA_SEQ_DATA, index, sequencer[index]);
    }

    write_indexed(VGA_CRTC_INDEX, VGA_CRTC_DATA, 0x03u,
                  (uint8_t)(crtc[3] | 0x80u));
    write_indexed(VGA_CRTC_INDEX, VGA_CRTC_DATA, 0x11u,
                  (uint8_t)(crtc[0x11] & 0x7fu));
    for (uint8_t index = 0; index < 25u; ++index) {
        write_indexed(VGA_CRTC_INDEX, VGA_CRTC_DATA, index, crtc[index]);
    }
    for (uint8_t index = 0; index < 9u; ++index) {
        write_indexed(VGA_GC_INDEX, VGA_GC_DATA, index, graphics[index]);
    }
    for (uint8_t index = 0; index < 21u; ++index) {
        (void)in8(VGA_INPUT_STATUS);
        out8(VGA_ATTR_INDEX_DATA, index);
        out8(VGA_ATTR_INDEX_DATA, attribute[index]);
    }
    (void)in8(VGA_INPUT_STATUS);
    out8(VGA_ATTR_INDEX_DATA, 0x20u);
}

static void set_desktop_palette(void)
{
    out8(VGA_DAC_WRITE_INDEX, 0u);
    for (uint32_t color = 0u; color < 16u; ++color) {
        for (uint32_t channel = 0u; channel < 3u; ++channel) {
            out8(VGA_DAC_DATA, desktop_palette[color][channel]);
        }
    }
}

static void select_text_font_plane(void)
{
    write_indexed(VGA_SEQ_INDEX, VGA_SEQ_DATA, 0x02u, 0x04u);
    write_indexed(VGA_SEQ_INDEX, VGA_SEQ_DATA, 0x04u, 0x07u);
    write_indexed(VGA_GC_INDEX, VGA_GC_DATA, 0x04u, 0x02u);
    write_indexed(VGA_GC_INDEX, VGA_GC_DATA, 0x05u, 0x00u);
    write_indexed(VGA_GC_INDEX, VGA_GC_DATA, 0x06u, 0x04u);
    write_indexed(VGA_GC_INDEX, VGA_GC_DATA, 0x08u, 0xffu);
}

static void save_text_font(void)
{
    select_text_font_plane();
    for (uint32_t character = 0u; character < VGA_FONT_CHARACTERS;
         ++character) {
        for (uint32_t row = 0u; row < VGA_FONT_HEIGHT; ++row) {
            saved_text_font[character * VGA_FONT_HEIGHT + row] =
                VGA_FRAMEBUFFER[character * VGA_FONT_STRIDE + row];
        }
    }
    text_font_saved = 1u;
}

static void restore_text_font(void)
{
    if (!text_font_saved) {
        return;
    }

    select_text_font_plane();
    for (uint32_t character = 0u; character < VGA_FONT_CHARACTERS;
         ++character) {
        for (uint32_t row = 0u; row < VGA_FONT_HEIGHT; ++row) {
            VGA_FRAMEBUFFER[character * VGA_FONT_STRIDE + row] =
                saved_text_font[character * VGA_FONT_HEIGHT + row];
        }
    }
    set_mode(0x67u, mode3_sequencer, mode3_crtc,
             mode3_graphics, mode3_attribute);
}

int video_set_graphics_mode(void)
{
    if (!framebuffers_allocated) {
        if (!allocate_framebuffer(render_frames)) {
            return 0;
        }
        if (!allocate_framebuffer(displayed_frames)) {
            release_framebuffer(render_frames);
            return 0;
        }
        framebuffers_allocated = 1u;
    }
    displayed_buffer_valid = 0u;
    save_text_font();
    struct bios_video_mode mode;
    if (bios_video_set_640x480_mode(&mode)) {
        uint32_t framebuffer_size = mode.pitch * VIDEO_VBE_HEIGHT;
        int mapped = vbe_framebuffer_mapped &&
                     mode.framebuffer == vbe_mapped_address &&
                     framebuffer_size <= vbe_mapped_size;
        if (!mapped &&
            paging_map_mmio(mode.framebuffer, framebuffer_size)) {
            vbe_framebuffer_mapped = 1u;
            vbe_mapped_address = mode.framebuffer;
            vbe_mapped_size = framebuffer_size;
            mapped = 1;
        }
        if (mapped) {
            vbe_mode_active = 1u;
            vbe_framebuffer = mode.framebuffer;
            vbe_pitch = mode.pitch;
            vbe_bytes_per_pixel = mode.bytes_per_pixel;
        } else {
            vbe_mode_active = 0u;
            vbe_framebuffer = 0u;
            vbe_pitch = 0u;
            vbe_bytes_per_pixel = 0u;
            (void)bios_video_set_text_mode();
            set_mode(0x63u, mode13_sequencer, mode13_crtc,
                     mode13_graphics, mode13_attribute);
        }
    } else {
        vbe_mode_active = 0u;
        vbe_framebuffer = 0u;
        vbe_pitch = 0u;
        vbe_bytes_per_pixel = 0u;
        set_mode(0x63u, mode13_sequencer, mode13_crtc,
                 mode13_graphics, mode13_attribute);
    }
    set_desktop_palette();
    displayed_buffer_valid = 0u;
    video_clear(0u);
    video_present();
    return 1;
}

int video_set_text_mode(void)
{
    if (vbe_mode_active) {
        (void)bios_video_set_text_mode();
    }
    set_mode(0x67u, mode3_sequencer, mode3_crtc,
             mode3_graphics, mode3_attribute);
    vbe_mode_active = 0u;
    vbe_framebuffer = 0u;
    vbe_pitch = 0u;
    vbe_bytes_per_pixel = 0u;
    restore_text_font();
    if (framebuffers_allocated) {
        release_framebuffer(displayed_frames);
        release_framebuffer(render_frames);
        framebuffers_allocated = 0u;
        displayed_buffer_valid = 0u;
    }
    return 1;
}

void video_clear(uint8_t color)
{
    for (uint32_t index = 0; index < VGA_FRAMEBUFFER_BYTES; ++index) {
        framebuffer_write(render_frames, index, color);
    }
}

void video_present(void)
{
    uint32_t width = video_display_width();
    uint32_t height = video_display_height();
    uint32_t bytes = width * height;
    for (uint32_t index = 0; index < bytes; ++index) {
        uint8_t color = framebuffer_read(render_frames, index);
        if (!displayed_buffer_valid ||
            framebuffer_read(displayed_frames, index) != color) {
            if (vbe_mode_active) {
                uint32_t y = index / width;
                uint32_t x = index % width;
                volatile uint8_t *pixel =
                    (volatile uint8_t *)(uintptr_t)(
                        vbe_framebuffer + y * vbe_pitch +
                        x * vbe_bytes_per_pixel);
                *pixel = color;
            } else {
                VGA_FRAMEBUFFER[index] = color;
            }
            framebuffer_write(displayed_frames, index, color);
        }
    }
    displayed_buffer_valid = 1u;
}

static void framebuffer_pixel(int32_t x, int32_t y, uint8_t color)
{
    uint32_t width = video_display_width();
    uint32_t height = video_display_height();
    if (x >= 0 && x < (int32_t)width &&
        y >= 0 && y < (int32_t)height) {
        framebuffer_write(render_frames,
                          (uint32_t)y * width + (uint32_t)x, color);
    }
}

static void framebuffer_fill_span(uint32_t index, uint32_t length,
                                 uint8_t color)
{
    while (length != 0u) {
        uint32_t page = index / VGA_PAGE_SIZE;
        uint32_t offset = index % VGA_PAGE_SIZE;
        uint32_t span = VGA_PAGE_SIZE - offset;
        if (span > length) {
            span = length;
        }
        uint8_t *address = (uint8_t *)(uintptr_t)render_frames[page] + offset;
        for (uint32_t column = 0u; column < span; ++column) {
            address[column] = color;
        }
        index += span;
        length -= span;
    }
}

void video_fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height,
                     uint8_t color)
{
    int32_t left = x;
    int32_t top = y;
    int32_t right = x + (int32_t)width;
    int32_t bottom = y + (int32_t)height;
    if (vbe_mode_active) {
        left *= 2;
        right *= 2;
        top = (top * 12) / 5;
        bottom = (bottom * 12) / 5;
    }
    int32_t display_width = (int32_t)video_display_width();
    int32_t display_height = (int32_t)video_display_height();
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > display_width) {
        right = display_width;
    }
    if (bottom > display_height) {
        bottom = display_height;
    }
    if (left >= right || top >= bottom) {
        return;
    }
    uint32_t span = (uint32_t)(right - left);
    for (int32_t py = top; py < bottom; ++py) {
        framebuffer_fill_span((uint32_t)py * (uint32_t)display_width +
                                  (uint32_t)left,
                              span, color);
    }
}

void video_fill_circle(int32_t center_x, int32_t center_y, uint32_t radius,
                       uint8_t color)
{
    int32_t signed_radius = (int32_t)radius;
    if (vbe_mode_active) {
        center_x *= 2;
        center_y = (center_y * 12) / 5;
        signed_radius *= 2;
    }
    int32_t radius_squared = signed_radius * signed_radius;
    for (int32_t offset_y = -signed_radius;
         offset_y <= signed_radius; ++offset_y) {
        int32_t extent = 0;
        int32_t remaining =
            radius_squared - offset_y * offset_y;
        while ((extent + 1) * (extent + 1) <= remaining) {
            ++extent;
        }
        for (int32_t offset_x = -extent; offset_x <= extent; ++offset_x) {
            framebuffer_pixel(center_x + offset_x, center_y + offset_y,
                              color);
        }
    }
}

static void draw_cursor_bitmap(int32_t x, int32_t y, const uint8_t *pixels,
                               uint32_t width, uint32_t height)
{
    if (pixels == 0) {
        return;
    }

    int32_t origin_x = x;
    int32_t origin_y = y;
    if (vbe_mode_active) {
        origin_x *= 2;
        origin_y = (origin_y * 12) / 5;
    }
    for (uint32_t row = 0u; row < height; ++row) {
        for (uint32_t column = 0u; column < width; ++column) {
            uint8_t color = pixels[row * width + column];
            if (color != 0xffu) {
                framebuffer_pixel(origin_x + (int32_t)column,
                                  origin_y + (int32_t)row, color);
            }
        }
    }
}

void video_draw_mouse_cursor(int32_t x, int32_t y)
{
    draw_cursor_bitmap(x, y, mouse_cursor_pixels,
                       MOUSE_CURSOR_WIDTH, MOUSE_CURSOR_HEIGHT);
}

static void fill_round_rect(int32_t x, int32_t y, uint32_t width,
                            uint32_t height, uint32_t radius, uint8_t color)
{
    if (width <= radius * 2u || height <= radius * 2u) {
        video_fill_rect(x, y, width, height, color);
        return;
    }
    video_fill_rect(x + (int32_t)radius, y, width - radius * 2u, height,
                    color);
    video_fill_rect(x, y + (int32_t)radius, width, height - radius * 2u,
                    color);
    video_fill_circle(x + (int32_t)radius, y + (int32_t)radius, radius,
                      color);
    video_fill_circle(x + (int32_t)width - (int32_t)radius - 1,
                      y + (int32_t)radius, radius, color);
    video_fill_circle(x + (int32_t)radius,
                      y + (int32_t)height - (int32_t)radius - 1,
                      radius, color);
    video_fill_circle(x + (int32_t)width - (int32_t)radius - 1,
                      y + (int32_t)height - (int32_t)radius - 1,
                      radius, color);
}

static const uint8_t *glyph_for(char character)
{
    if (character >= 'A' && character <= 'Z') {
        return font[(uint32_t)(character - 'A')];
    }
    if (character >= '0' && character <= '9') {
        return font[26u + (uint32_t)(character - '0')];
    }
    if (character >= 'a' && character <= 'z') {
        return font[36u + (uint32_t)(character - 'a')];
    }

    static const uint8_t exclamation[7] = {
        0x04,0x04,0x04,0x04,0x04,0x00,0x04,
    };
    static const uint8_t question[7] = {
        0x0e,0x11,0x01,0x02,0x04,0x00,0x04,
    };
    static const uint8_t equals[7] = {
        0x00,0x1f,0x00,0x1f,0x00,0x00,0x00,
    };
    static const uint8_t plus[7] = {
        0x00,0x04,0x04,0x1f,0x04,0x04,0x00,
    };
    static const uint8_t comma[7] = {
        0x00,0x00,0x00,0x00,0x00,0x04,0x08,
    };
    static const uint8_t semicolon[7] = {
        0x00,0x04,0x00,0x00,0x04,0x04,0x08,
    };
    static const uint8_t left_parenthesis[7] = {
        0x02,0x04,0x08,0x08,0x08,0x04,0x02,
    };
    static const uint8_t right_parenthesis[7] = {
        0x08,0x04,0x02,0x02,0x02,0x04,0x08,
    };
    static const uint8_t underscore[7] = {
        0x00,0x00,0x00,0x00,0x00,0x00,0x1f,
    };
    static const uint8_t apostrophe[7] = {
        0x04,0x04,0x02,0x00,0x00,0x00,0x00,
    };
    static const uint8_t quote[7] = {
        0x0a,0x0a,0x05,0x00,0x00,0x00,0x00,
    };
    static const uint8_t at[7] = {
        0x0e,0x11,0x17,0x15,0x17,0x10,0x0e,
    };
    static const uint8_t hash[7] = {
        0x0a,0x1f,0x0a,0x0a,0x1f,0x0a,0x00,
    };
    static const uint8_t ampersand[7] = {
        0x0c,0x12,0x14,0x08,0x15,0x12,0x0d,
    };
    static const uint8_t less_than[7] = {
        0x02,0x04,0x08,0x10,0x08,0x04,0x02,
    };
    static const uint8_t greater_than[7] = {
        0x08,0x04,0x02,0x01,0x02,0x04,0x08,
    };

    switch (character) {
    case '!': return exclamation;
    case '?': return question;
    case '=': return equals;
    case '+': return plus;
    case ',': return comma;
    case ';': return semicolon;
    case '(': return left_parenthesis;
    case ')': return right_parenthesis;
    case '_': return underscore;
    case '\'': return apostrophe;
    case '"': return quote;
    case '@': return at;
    case '#': return hash;
    case '&': return ampersand;
    case '<': return less_than;
    case '>': return greater_than;
    default: break;
    }
    return 0;
}

static uint32_t text_width(const char *text)
{
    uint32_t length = 0u;
    while (text[length] != '\0') {
        ++length;
    }
    return length * 6u;
}

void video_draw_text(int32_t x, int32_t y, const char *text, uint8_t color)
{
    while (*text != '\0') {
        const uint8_t *glyph = glyph_for(*text);
        if (glyph != 0) {
            if (vbe_mode_active) {
                int32_t origin_x = x * 2;
                int32_t origin_y = (y * 12) / 5;
                for (uint32_t row = 0u; row < 16u; ++row) {
                    uint32_t source_row = row * 7u / 16u;
                    for (uint32_t column = 0u; column < 8u; ++column) {
                        uint32_t source_column = column * 5u / 8u;
                        if ((glyph[source_row] &
                             (1u << (4u - source_column))) != 0u) {
                            framebuffer_pixel(
                                origin_x + (int32_t)column,
                                origin_y + (int32_t)row, color);
                        }
                    }
                }
            } else {
                for (uint32_t row = 0; row < 7u; ++row) {
                    for (uint32_t column = 0; column < 5u; ++column) {
                        if ((glyph[row] & (1u << (4u - column))) != 0u) {
                            video_fill_rect(x + (int32_t)column,
                                            y + (int32_t)row, 1u, 1u, color);
                        }
                    }
                }
            }
        } else if (*text == '.') {
            video_fill_rect(x + 2, y + 6, 1u, 1u, color);
        } else if (*text == ':') {
            video_fill_rect(x + 2, y + 2, 1u, 1u, color);
            video_fill_rect(x + 2, y + 5, 1u, 1u, color);
        } else if (*text == '-') {
            video_fill_rect(x + 1, y + 3, 3u, 1u, color);
        } else if (*text == '/') {
            video_fill_rect(x + 3, y + 1, 1u, 1u, color);
            video_fill_rect(x + 2, y + 2, 1u, 1u, color);
            video_fill_rect(x + 2, y + 3, 1u, 1u, color);
            video_fill_rect(x + 1, y + 4, 1u, 1u, color);
            video_fill_rect(x + 1, y + 5, 1u, 1u, color);
        }
        x += 6;
        ++text;
    }
}

void video_draw_button(int32_t x, int32_t y, uint32_t width, uint32_t height,
                       const char *label, uint8_t background,
                       uint8_t foreground)
{
    if (width < 10u || height < 9u) {
        return;
    }
    fill_round_rect(x + 1, y + 2, width, height, 2u, 0u);
    fill_round_rect(x, y, width, height, 2u, 8u);
    fill_round_rect(x + 1, y + 1, width - 2u, height - 2u,
                    2u, background);
    video_fill_rect(x + 4, y + 1, width - 8u, 1u, 11u);
    video_fill_rect(x + 4, y + (int32_t)height - 2,
                    width - 8u, 1u, 3u);

    uint32_t label_width = text_width(label);
    if (label_width <= width - 6u) {
        int32_t label_x = x + (int32_t)(width - label_width) / 2;
        int32_t label_y = y + ((int32_t)height - 7) / 2;
        video_draw_text(label_x, label_y, label, foreground);
    }
}
