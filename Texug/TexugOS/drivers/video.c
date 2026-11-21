#include <stdint.h>

#include "bios_video.h"
#include "font_dejavu.h"
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
    {5, 6, 7}, /*  0 contorno */
    {11, 13, 13}, /*  1 texto escuro / barras */
    {15, 19, 25}, /*  2 azul aco (botoes do painel) */
    {11, 20, 32}, /*  3 fundo do desktop */
    {52, 18, 17}, /*  4 vermelho */
    {35, 31, 52}, /*  5 roxo */
    {53, 36, 10}, /*  6 ambar */
    {55, 55, 55}, /*  7 cinza claro (widgets) */
    {34, 36, 37}, /*  8 cinza de borda */
    {14, 32, 53}, /*  9 azul de destaque */
    {14, 38, 22}, /* 10 verde */
    {31, 50, 57}, /* 11 ciano */
    {53, 16, 23}, /* 12 vermelho/erro */
    {57, 35, 45}, /* 13 rosa */
    {59, 50, 15}, /* 14 amarelo */
    {63, 63, 63}, /* 15 branco */
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

/* ---- Geometria -------------------------------------------------------
 * Os aplicativos desenham em coordenadas logicas de 320x200. No modo VBE
 * 640x480 isso vira x * 2 e y * 12 / 5. Bordas e texto sao desenhados em
 * pixels reais do dispositivo, para ficarem sempre nitidos (1 px).
 */
struct device_rect {
    int32_t left;
    int32_t top;
    int32_t right;   /* exclusivo */
    int32_t bottom;  /* exclusivo */
};

static void logical_to_device(int32_t x, int32_t y, uint32_t width,
                              uint32_t height, struct device_rect *rect)
{
    rect->left = x;
    rect->top = y;
    rect->right = x + (int32_t)width;
    rect->bottom = y + (int32_t)height;
    if (vbe_mode_active) {
        rect->left *= 2;
        rect->right *= 2;
        rect->top = (rect->top * 12) / 5;
        rect->bottom = (rect->bottom * 12) / 5;
    }
}

static void fill_device_rect(int32_t left, int32_t top, int32_t right,
                             int32_t bottom, uint8_t color)
{
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

void video_fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height,
                     uint8_t color)
{
    struct device_rect rect;
    logical_to_device(x, y, width, height, &rect);
    fill_device_rect(rect.left, rect.top, rect.right, rect.bottom, color);
}

/* Contorno de 1 pixel real, sem preenchimento. */
void video_draw_frame(int32_t x, int32_t y, uint32_t width, uint32_t height,
                      uint8_t color)
{
    struct device_rect rect;
    logical_to_device(x, y, width, height, &rect);
    if (rect.right - rect.left < 2 || rect.bottom - rect.top < 2) {
        fill_device_rect(rect.left, rect.top, rect.right, rect.bottom,
                         color);
        return;
    }
    fill_device_rect(rect.left, rect.top, rect.right, rect.top + 1, color);
    fill_device_rect(rect.left, rect.bottom - 1, rect.right, rect.bottom,
                     color);
    fill_device_rect(rect.left, rect.top + 1, rect.left + 1,
                     rect.bottom - 1, color);
    fill_device_rect(rect.right - 1, rect.top + 1, rect.right,
                     rect.bottom - 1, color);
}

/* Linha horizontal de 1 pixel real. */
void video_draw_hline(int32_t x, int32_t y, uint32_t width, uint8_t color)
{
    struct device_rect rect;
    logical_to_device(x, y, width, 1u, &rect);
    fill_device_rect(rect.left, rect.top, rect.right, rect.top + 1, color);
}

/* "X" de diagonais continuas (2 pixels de largura), centrado numa caixa. */
void video_draw_cross(int32_t x, int32_t y, uint32_t width, uint32_t height,
                      uint8_t color)
{
    struct device_rect rect;
    logical_to_device(x, y, width, height, &rect);
    int32_t size = vbe_mode_active ? 9 : 5;
    int32_t left = rect.left + ((rect.right - rect.left) - size) / 2;
    int32_t top = rect.top + ((rect.bottom - rect.top) - size) / 2;
    int32_t thickness = vbe_mode_active ? 2 : 1;
    for (int32_t step = 0; step < size; ++step) {
        fill_device_rect(left + step, top + step,
                         left + step + thickness, top + step + 1, color);
        fill_device_rect(left + size - 1 - step, top + step,
                         left + size - 1 - step + thickness, top + step + 1,
                         color);
    }
}

/* Recorte dos cantos (em pixels) por raio e por linha, de fora para dentro. */
static const uint8_t corner_inset[4][3] = {
    {0, 0, 0}, {0, 0, 0}, {1, 0, 0}, {2, 1, 0},
};

static void fill_device_round(int32_t left, int32_t top, int32_t right,
                              int32_t bottom, uint32_t radius, uint8_t color)
{
    if (radius > 3u) {
        radius = 3u;
    }
    if (radius < 2u || right - left < (int32_t)radius * 2 + 1 ||
        bottom - top < (int32_t)radius * 2 + 1) {
        fill_device_rect(left, top, right, bottom, color);
        return;
    }
    fill_device_rect(left, top + (int32_t)radius, right,
                     bottom - (int32_t)radius, color);
    for (uint32_t row = 0u; row < radius; ++row) {
        int32_t inset = (int32_t)corner_inset[radius][row];
        fill_device_rect(left + inset, top + (int32_t)row,
                         right - inset, top + (int32_t)row + 1, color);
        fill_device_rect(left + inset, bottom - 1 - (int32_t)row,
                         right - inset, bottom - (int32_t)row, color);
    }
}

/* Painel liso: preenchimento chapado, borda de 1 pixel, cantos suaves. */
void video_draw_panel(int32_t x, int32_t y, uint32_t width, uint32_t height,
                      uint8_t fill, uint8_t border, uint32_t radius)
{
    struct device_rect rect;
    logical_to_device(x, y, width, height, &rect);
    if (!vbe_mode_active && radius > 2u) {
        radius = 2u;
    }
    fill_device_round(rect.left, rect.top, rect.right, rect.bottom, radius,
                      border);
    fill_device_round(rect.left + 1, rect.top + 1, rect.right - 1,
                      rect.bottom - 1, radius != 0u ? radius - 1u : 0u,
                      fill);
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

/* ---- Texto (DejaVu Sans, bitmap 1 bpp proporcional) ------------------- */
struct font_face {
    const struct font_glyph *glyphs;
    const uint8_t *bitmap;
    uint32_t cap_height;
    uint32_t baseline;   /* linha de base dentro da caixa de texto padrao */
    uint32_t cell;       /* largura de uma celula de 6 unidades logicas */
};

static const struct font_face face_vbe = {
    dejavu14_glyphs, dejavu14_bitmap, DEJAVU14_CAP_HEIGHT, 13u, 12u,
};
static const struct font_face face_vga = {
    dejavu9_glyphs, dejavu9_bitmap, DEJAVU9_CAP_HEIGHT, 7u, 6u,
};

static const struct font_face *current_face(void)
{
    return vbe_mode_active ? &face_vbe : &face_vga;
}

static const struct font_glyph *face_glyph(const struct font_face *face,
                                           char character)
{
    uint32_t code = (uint8_t)character;
    if (code < FONT_FIRST_CHAR || code > FONT_LAST_CHAR) {
        code = ' ';
    }
    return &face->glyphs[code - FONT_FIRST_CHAR];
}

static uint32_t face_text_width(const struct font_face *face,
                                const char *text)
{
    uint32_t width = 0u;
    while (*text != '\0') {
        width += face_glyph(face, *text)->advance;
        ++text;
    }
    return width;
}

static void draw_glyph(const struct font_face *face, int32_t x,
                       int32_t baseline, const struct font_glyph *glyph,
                       uint8_t color)
{
    uint32_t stride = ((uint32_t)glyph->width + 7u) / 8u;
    const uint8_t *rows = face->bitmap + glyph->offset;
    for (uint32_t row = 0u; row < glyph->height; ++row) {
        for (uint32_t column = 0u; column < glyph->width; ++column) {
            if ((rows[row * stride + column / 8u] &
                 (0x80u >> (column % 8u))) != 0u) {
                framebuffer_pixel(x + glyph->left + (int32_t)column,
                                  baseline - glyph->top + (int32_t)row,
                                  color);
            }
        }
    }
}

static void draw_string_device(const struct font_face *face, int32_t x,
                               int32_t baseline, const char *text,
                               uint8_t color)
{
    while (*text != '\0') {
        const struct font_glyph *glyph = face_glyph(face, *text);
        draw_glyph(face, x, baseline, glyph, color);
        x += glyph->advance;
        ++text;
    }
}

static int32_t device_x(int32_t x)
{
    return vbe_mode_active ? x * 2 : x;
}

static int32_t device_y(int32_t y)
{
    return vbe_mode_active ? (y * 12) / 5 : y;
}

/* Largura do texto em unidades logicas (as mesmas de x/y dos widgets). */
uint32_t video_text_width(const char *text)
{
    uint32_t pixels = face_text_width(current_face(), text);
    return vbe_mode_active ? (pixels + 1u) / 2u : pixels;
}

/* Canto superior esquerdo da caixa de texto em (x, y). */
void video_draw_text(int32_t x, int32_t y, const char *text, uint8_t color)
{
    const struct font_face *face = current_face();
    draw_string_device(face, device_x(x),
                       device_y(y) + (int32_t)face->baseline, text, color);
}

/* Texto alinhado a esquerda, centrado na vertical numa faixa de altura h. */
void video_draw_text_middle(int32_t x, int32_t y, uint32_t height,
                            const char *text, uint8_t color)
{
    const struct font_face *face = current_face();
    struct device_rect rect;
    logical_to_device(x, y, 0u, height, &rect);
    int32_t box = rect.bottom - rect.top;
    draw_string_device(face, rect.left,
                       rect.top + (box + (int32_t)face->cap_height) / 2,
                       text, color);
}

/* Texto centrado num retangulo. */
void video_draw_text_centered(int32_t x, int32_t y, uint32_t width,
                              uint32_t height, const char *text,
                              uint8_t color)
{
    const struct font_face *face = current_face();
    struct device_rect rect;
    logical_to_device(x, y, width, height, &rect);
    int32_t box_width = rect.right - rect.left;
    int32_t box_height = rect.bottom - rect.top;
    int32_t text_width = (int32_t)face_text_width(face, text);
    draw_string_device(face, rect.left + (box_width - text_width) / 2,
                       rect.top + (box_height + (int32_t)face->cap_height) / 2,
                       text, color);
}

/* Um caractere centrado numa celula de 6 unidades logicas (editor de texto,
 * contadores): mantem o passo fixo mesmo com fonte proporcional. */
void video_draw_char_cell(int32_t x, int32_t y, char character,
                          uint8_t color)
{
    const struct font_face *face = current_face();
    const struct font_glyph *glyph = face_glyph(face, character);
    int32_t left = device_x(x) +
                   ((int32_t)face->cell - (int32_t)glyph->advance) / 2;
    draw_glyph(face, left, device_y(y) + (int32_t)face->baseline, glyph,
               color);
}

/* Botao liso com borda. Papeis de cor mantidos para os apps existentes:
 *   fundo 2 -> botao neutro (cinza claro, texto escuro)
 *   fundo 9 -> botao de destaque (azul)   fundo 4/12 -> perigo/erro */
void video_draw_button(int32_t x, int32_t y, uint32_t width, uint32_t height,
                       const char *label, uint8_t background,
                       uint8_t foreground)
{
    if (width < 10u || height < 9u) {
        return;
    }
    uint8_t border = 8u;
    if (background == 2u) {
        background = 7u;
        foreground = 1u;
    } else if (background == 9u) {
        border = 2u;
    } else {
        border = 0u;
    }
    video_draw_panel(x, y, width, height, background, border, 2u);
    if (label[0] != '\0') {
        const struct font_face *face = current_face();
        uint32_t label_width = face_text_width(face, label);
        uint32_t box_width = (uint32_t)(vbe_mode_active ? width * 2u : width);
        if (label_width + 4u <= box_width) {
            video_draw_text_centered(x, y, width, height, label,
                                     foreground);
        }
    }
}