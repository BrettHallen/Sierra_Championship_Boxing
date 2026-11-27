/* CGA 320x200x4 graphics: screen and sprite decoding, and a software framebuffer.
 * See docs/ANALYSIS.md section 3 for the formats. */
#ifndef CB_CGA_H
#define CB_CGA_H

#include <stdint.h>
#include <stddef.h>

#define CGA_W 320
#define CGA_H 200
#define CGA_TRANSPARENT 0xFF

typedef struct {
    uint8_t px[CGA_W * CGA_H]; /* one colour index (0-3) per pixel */
} Framebuffer;

/* A decoded sprite: colour index per pixel, CGA_TRANSPARENT where the
 * original AND-mask kept the background. */
typedef struct {
    int w, h;          /* pixels */
    int off;           /* signed CGA even-bank offset from the original header */
    int off2;          /* second offset word (head sprites only), else 0 */
    uint8_t *px;
} Sprite;

typedef enum { SPR_OPAQUE, SPR_HEAD, SPR_MASKED } SpriteKind;

/* Palette: 4 RGB colours. */
typedef struct { uint8_t r, g, b; } Rgb;
void cga_palette(int palette, int high_intensity, Rgb out[4]);

/* Decode an RLE full screen (PIC, PI2..PI5) into the framebuffer. */
void cga_decode_screen(const uint8_t *data, size_t len, Framebuffer *fb);

/* Decode one sprite starting at data[pos]. Returns the position just past it,
 * or 0 if the header is not a plausible sprite. */
size_t cga_decode_sprite(const uint8_t *data, size_t len, size_t pos,
                         SpriteKind kind, Sprite *out);
void sprite_free(Sprite *s);
/* Make a left-right mirrored copy (used when a right-corner file is missing). */
void sprite_mirror(const Sprite *src, Sprite *dst);

/* Draw a sprite with its top-left corner at (x, y). */
void fb_blit(Framebuffer *fb, const Sprite *s, int x, int y);
void fb_fill(Framebuffer *fb, int x, int y, int w, int h, uint8_t c);

/* Convert a signed even-bank offset (80 bytes per line pair) to pixels. */
void cga_offset_to_xy(int off, int *x, int *y);

#endif
