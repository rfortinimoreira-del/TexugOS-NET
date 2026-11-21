#include <stdint.h>

#include "bios_video.h"
#include "font_dejavu.h"
#include "gfx_assets.h"
#include "frame_allocator.h"
#include "cursor_hires.h"
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

/* A paleta de 256 cores (0..15 = cores do desktop, 16..39 = cinzas,
 * 40..254 = cores dos icones e das misturas do anti-aliasing) vem de
 * gfx_assets.h. O indice 255 nunca e usado como cor de desenho. */
#define PALETTE_USABLE 255u
#define TEXT_BLEND_LEVELS (FONT_LEVELS - 1u)
#define CACHE_EMPTY 0xffu
#define ICON_HOVER_LIGHTEN 26u

static uint8_t text_blend_cache[16][256][TEXT_BLEND_LEVELS];
static uint8_t cursor_grey_index[256];
static uint8_t lighten_index[256];
static uint8_t icon_hover_cache[GFX_ICON_COUNT]
                               [GFX_ICON_HOVER_SIZE * GFX_ICON_HOVER_SIZE];
static uint8_t icon_hover_ready[GFX_ICON_COUNT];
static uint8_t icon_scratch[GFX_ICON_SIZE * GFX_ICON_SIZE];
static uint8_t blend_tables_ready;

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
    for (uint32_t color = 0u; color < 256u; ++color) {
        for (uint32_t channel = 0u; channel < 3u; ++channel) {
            out8(VGA_DAC_DATA, gfx_palette[color][channel]);
        }
    }
}

/* Cor da paleta mais proxima de (r, g, b), em escala 0..252 (6 bits * 4). */
static uint8_t palette_nearest(int32_t r, int32_t g, int32_t b)
{
    uint32_t best = 0xffffffffu;
    uint8_t best_index = 0u;
    for (uint32_t index = 0u; index < PALETTE_USABLE; ++index) {
        int32_t dr = r - (int32_t)gfx_palette[index][0] * 4;
        int32_t dg = g - (int32_t)gfx_palette[index][1] * 4;
        int32_t db = b - (int32_t)gfx_palette[index][2] * 4;
        uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
        if (distance < best) {
            best = distance;
            best_index = (uint8_t)index;
            if (distance == 0u) {
                break;
            }
        }
    }
    return best_index;
}

static void prepare_blend_tables(void)
{
    if (blend_tables_ready) {
        return;
    }
    for (uint32_t fg = 0u; fg < 16u; ++fg) {
        for (uint32_t bg = 0u; bg < 256u; ++bg) {
            for (uint32_t level = 0u; level < TEXT_BLEND_LEVELS; ++level) {
                text_blend_cache[fg][bg][level] = CACHE_EMPTY;
            }
        }
    }
    for (uint32_t grey = 0u; grey < 256u; ++grey) {
        int32_t value = (int32_t)(grey * 252u / 255u);
        cursor_grey_index[grey] = palette_nearest(value, value, value);
    }
    for (uint32_t index = 0u; index < 256u; ++index) {
        int32_t channel[3];
        for (uint32_t c = 0u; c < 3u; ++c) {
            int32_t value = (int32_t)gfx_palette[index][c] * 4;
            channel[c] = value + ((252 - value) * (int32_t)ICON_HOVER_LIGHTEN) / 256;
        }
        lighten_index[index] =
            palette_nearest(channel[0], channel[1], channel[2]);
    }
    for (uint32_t icon = 0u; icon < GFX_ICON_COUNT; ++icon) {
        icon_hover_ready[icon] = 0u;
    }
    blend_tables_ready = 1u;
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
    prepare_blend_tables();
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

static uint8_t framebuffer_pixel_read(int32_t x, int32_t y)
{
    uint32_t width = video_display_width();
    uint32_t height = video_display_height();
    if (x < 0 || x >= (int32_t)width || y < 0 || y >= (int32_t)height) {
        return 0u;
    }
    return framebuffer_read(render_frames,
                            (uint32_t)y * width + (uint32_t)x);
}

/* Mistura a cor `fg` (0..15) sobre o pixel atual com cobertura 1..7 de 8. */
static uint8_t blend_text_color(uint8_t fg, uint8_t background,
                                uint32_t level)
{
    uint8_t *slot = &text_blend_cache[fg][background][level - 1u];
    if (*slot == CACHE_EMPTY) {
        int32_t channel[3];
        for (uint32_t c = 0u; c < 3u; ++c) {
            int32_t mixed =
                (int32_t)gfx_palette[fg][c] * (int32_t)level +
                (int32_t)gfx_palette[background][c] *
                    (int32_t)(FONT_LEVELS - level);
            channel[c] = (mixed * 4 + (int32_t)FONT_LEVELS / 2) /
                         (int32_t)FONT_LEVELS;
        }
        *slot = palette_nearest(channel[0], channel[1], channel[2]);
    }
    return *slot;
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

/* Cursor com anti-aliasing: cinza + opacidade por pixel (0..255). Pixels
 * opacos usam a cor mais proxima da paleta; os de borda sao misturados com
 * o que ja esta na tela. */
static void draw_cursor_bitmap(int32_t x, int32_t y, const uint8_t *grey,
                               const uint8_t *alpha, uint32_t width,
                               uint32_t height)
{
    int32_t origin_x = x;
    int32_t origin_y = y;
    if (vbe_mode_active) {
        origin_x *= 2;
        origin_y = (origin_y * 12) / 5;
    }
    for (uint32_t row = 0u; row < height; ++row) {
        for (uint32_t column = 0u; column < width; ++column) {
            uint32_t offset = row * width + column;
            uint32_t opacity = alpha[offset];
            if (opacity == 0u) {
                continue;
            }
            int32_t px = origin_x + (int32_t)column;
            int32_t py = origin_y + (int32_t)row;
            if (opacity >= 255u) {
                framebuffer_pixel(px, py, cursor_grey_index[grey[offset]]);
                continue;
            }
            uint8_t below = framebuffer_pixel_read(px, py);
            int32_t value = (int32_t)(grey[offset] * 252u / 255u);
            int32_t channel[3];
            for (uint32_t c = 0u; c < 3u; ++c) {
                channel[c] = (value * (int32_t)opacity +
                              (int32_t)gfx_palette[below][c] * 4 *
                                  (int32_t)(255u - opacity) + 127) / 255;
            }
            framebuffer_pixel(px, py,
                              palette_nearest(channel[0], channel[1],
                                              channel[2]));
        }
    }
}

void video_draw_mouse_cursor(int32_t x, int32_t y)
{
    draw_cursor_bitmap(x, y, mouse_cursor_grey, mouse_cursor_alpha,
                       MOUSE_CURSOR_WIDTH, MOUSE_CURSOR_HEIGHT);
}

/* ---- Icones dos aplicativos ------------------------------------------
 * Bitmaps de 8 bits (RLE) ja compostos sobre a cor de fundo do desktop.
 * Ao passar o mouse o icone e ampliado por interpolacao bilinear e
 * guardado em cache (so e calculado uma vez por icone).
 */
static void icon_decode(const struct gfx_icon *icon, uint8_t *output)
{
    uint32_t position = 0u;
    uint32_t total = (uint32_t)icon->width * icon->height;
    for (uint32_t offset = 0u; offset + 1u < icon->size; offset += 2u) {
        uint32_t count = icon->data[offset];
        uint8_t value = icon->data[offset + 1u];
        while (count != 0u && position < total) {
            output[position++] = value;
            --count;
        }
    }
    while (position < total) {
        output[position++] = GFX_BACKGROUND_INDEX;
    }
}

static void icon_build_hover(uint32_t index)
{
    const uint32_t source = GFX_ICON_SIZE;
    const uint32_t target = GFX_ICON_HOVER_SIZE;
    uint8_t *output = icon_hover_cache[index];
    icon_decode(&gfx_icons[index], icon_scratch);

    for (uint32_t dy = 0u; dy < target; ++dy) {
        int32_t fy = (((int32_t)(2u * dy + 1u) * (int32_t)source -
                       (int32_t)target) * 128) / (int32_t)target;
        if (fy < 0) {
            fy = 0;
        }
        if (fy > (int32_t)(source - 1u) * 256) {
            fy = (int32_t)(source - 1u) * 256;
        }
        uint32_t y0 = (uint32_t)fy >> 8;
        uint32_t y1 = y0 + 1u < source ? y0 + 1u : y0;
        uint32_t wy = (uint32_t)fy & 255u;
        for (uint32_t dx = 0u; dx < target; ++dx) {
            int32_t fx = (((int32_t)(2u * dx + 1u) * (int32_t)source -
                           (int32_t)target) * 128) / (int32_t)target;
            if (fx < 0) {
                fx = 0;
            }
            if (fx > (int32_t)(source - 1u) * 256) {
                fx = (int32_t)(source - 1u) * 256;
            }
            uint32_t x0 = (uint32_t)fx >> 8;
            uint32_t x1 = x0 + 1u < source ? x0 + 1u : x0;
            uint32_t wx = (uint32_t)fx & 255u;
            uint8_t p00 = icon_scratch[y0 * source + x0];
            uint8_t p10 = icon_scratch[y0 * source + x1];
            uint8_t p01 = icon_scratch[y1 * source + x0];
            uint8_t p11 = icon_scratch[y1 * source + x1];
            uint8_t result;
            if (p00 == p10 && p00 == p01 && p00 == p11) {
                result = p00 == GFX_BACKGROUND_INDEX ?
                         p00 : lighten_index[p00];
            } else {
                uint32_t w00 = (256u - wx) * (256u - wy);
                uint32_t w10 = wx * (256u - wy);
                uint32_t w01 = (256u - wx) * wy;
                uint32_t w11 = wx * wy;
                int32_t channel[3];
                for (uint32_t c = 0u; c < 3u; ++c) {
                    uint32_t mixed =
                        w00 * gfx_palette[p00][c] + w10 * gfx_palette[p10][c] +
                        w01 * gfx_palette[p01][c] + w11 * gfx_palette[p11][c];
                    int32_t value = (int32_t)((mixed * 4u + 32768u) >> 16);
                    channel[c] = value +
                        ((252 - value) * (int32_t)ICON_HOVER_LIGHTEN) / 256;
                }
                result = palette_nearest(channel[0], channel[1], channel[2]);
            }
            output[dy * target + dx] = result;
        }
    }
    icon_hover_ready[index] = 1u;
}

int video_draw_app_icon(uint32_t icon, int32_t center_x, int32_t center_y,
                        int hover)
{
    if (!vbe_mode_active || icon >= GFX_ICON_COUNT) {
        return 0;
    }
    int32_t device_center_x = center_x * 2;
    int32_t device_center_y = (center_y * 12) / 5;
    const uint8_t *pixels;
    uint32_t size;
    if (hover) {
        if (!icon_hover_ready[icon]) {
            icon_build_hover(icon);
        }
        pixels = icon_hover_cache[icon];
        size = GFX_ICON_HOVER_SIZE;
    } else {
        icon_decode(&gfx_icons[icon], icon_scratch);
        pixels = icon_scratch;
        size = GFX_ICON_SIZE;
    }
    int32_t left = device_center_x - (int32_t)size / 2;
    int32_t top = device_center_y - (int32_t)size / 2;
    for (uint32_t row = 0u; row < size; ++row) {
        for (uint32_t column = 0u; column < size; ++column) {
            framebuffer_pixel(left + (int32_t)column, top + (int32_t)row,
                              pixels[row * size + column]);
        }
    }
    return 1;
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
    const uint8_t *data = face->bitmap + glyph->offset;
    uint32_t index = 0u;
    for (uint32_t row = 0u; row < glyph->height; ++row) {
        for (uint32_t column = 0u; column < glyph->width; ++column) {
            uint8_t packed = data[index >> 1];
            uint32_t level = (index & 1u) != 0u ?
                             (uint32_t)(packed & 0x0fu) : (uint32_t)(packed >> 4);
            ++index;
            if (level == 0u) {
                continue;
            }
            int32_t px = x + glyph->left + (int32_t)column;
            int32_t py = baseline - glyph->top + (int32_t)row;
            if (level >= FONT_LEVELS || color >= 16u) {
                framebuffer_pixel(px, py, color);
            } else {
                uint8_t below = framebuffer_pixel_read(px, py);
                framebuffer_pixel(px, py,
                                  blend_text_color(color, below, level));
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