#include "cga.h"
#include <stdlib.h>
#include <string.h>

void cga_palette(int palette, int high, Rgb out[4])
{
    static const Rgb p1h[4] = {{0,0,0},{85,255,255},{255,85,255},{255,255,255}};
    static const Rgb p1l[4] = {{0,0,0},{0,170,170},{170,0,170},{170,170,170}};
    static const Rgb p0h[4] = {{0,0,0},{85,255,85},{255,85,85},{255,255,85}};
    static const Rgb p0l[4] = {{0,0,0},{0,170,0},{170,0,0},{170,85,0}};
    const Rgb *src = palette ? (high ? p1h : p1l) : (high ? p0h : p0l);
    memcpy(out, src, sizeof(Rgb) * 4);
}

/* Write a CGA video-memory byte (4 pixels) at a B800 offset. */
static void put_vram_byte(Framebuffer *fb, unsigned addr, uint8_t v)
{
    unsigned bank = addr >= 0x2000;
    unsigned o = addr - bank * 0x2000;
    if (o >= 8000) return;
    unsigned y = (o / 80) * 2 + bank;
    unsigned x = (o % 80) * 4;
    uint8_t *p = &fb->px[y * CGA_W + x];
    p[0] = (v >> 6) & 3; p[1] = (v >> 4) & 3; p[2] = (v >> 2) & 3; p[3] = v & 3;
}

void cga_decode_screen(const uint8_t *data, size_t len, Framebuffer *fb)
{
    /* (count, value) pairs alternating between the even (0000h) and odd
     * (2000h) banks; each bank holds 8000 bytes; a count of 0 means 1. */
    unsigned even = 0x0000, odd = 0x2000;
    size_t p = 0;
    memset(fb->px, 0, sizeof fb->px);
    while ((even < 0x1F40 || odd < 0x3F40) && p + 1 < len) {
        if (even < 0x1F40) {
            unsigned n = data[p] ? data[p] : 1; uint8_t v = data[p + 1]; p += 2;
            while (n-- && even < 0x1F40) put_vram_byte(fb, even++, v);
        }
        if (odd < 0x3F40 && p + 1 < len) {
            unsigned n = data[p] ? data[p] : 1; uint8_t v = data[p + 1]; p += 2;
            while (n-- && odd < 0x3F40) put_vram_byte(fb, odd++, v);
        }
    }
}

static int rd16(const uint8_t *d) { return d[0] | (d[1] << 8); }

size_t cga_decode_sprite(const uint8_t *d, size_t len, size_t pos,
                         SpriteKind kind, Sprite *out)
{
    memset(out, 0, sizeof *out);
    if (pos + 8 > len) return 0;
    int ww = rd16(d + pos), hh = rd16(d + pos + 2);
    if (ww <= 0 || ww > 40 || hh <= 0 || hh > 60) return 0;
    int off = (int16_t)rd16(d + pos + 4);
    size_t p = pos + (kind == SPR_HEAD ? 8 : 6);
    size_t need = kind == SPR_MASKED ? (size_t)hh * (4 * ww + 8) : (size_t)4 * ww * hh;
    if (p + need > len) return 0;

    out->w = ww * 8; out->h = hh * 2; out->off = off;
    out->off2 = kind == SPR_HEAD ? (int16_t)rd16(d + pos + 6) : 0;
    out->px = malloc((size_t)out->w * out->h);
    if (!out->px) return 0;

    for (int r = 0; r < hh; r++) {
        for (int c = 0; c < ww; c++) {
            int edge = kind == SPR_MASKED && (c == 0 || c == ww - 1);
            for (int line = 0; line < 2; line++) {
                uint8_t m[2] = {0, 0}, v[2];
                if (edge) { m[0] = d[p]; m[1] = d[p + 1]; v[0] = d[p + 2]; v[1] = d[p + 3]; p += 4; }
                else      { v[0] = d[p]; v[1] = d[p + 1]; p += 2; }
                for (int half = 0; half < 2; half++)
                    for (int k = 0; k < 4; k++) {
                        int sh = 6 - 2 * k;
                        int mm = (m[half] >> sh) & 3, vv = (v[half] >> sh) & 3;
                        out->px[(r * 2 + line) * out->w + c * 8 + half * 4 + k] =
                            mm == 3 ? CGA_TRANSPARENT : (uint8_t)vv;
                    }
            }
        }
    }
    if (kind == SPR_OPAQUE) p += 2; /* padding word */
    return p;
}

void sprite_free(Sprite *s)
{
    free(s->px);
    s->px = NULL;
}

void sprite_mirror(const Sprite *src, Sprite *dst)
{
    *dst = *src;
    dst->px = malloc((size_t)src->w * src->h);
    if (!dst->px) { dst->w = dst->h = 0; return; }
    for (int y = 0; y < src->h; y++)
        for (int x = 0; x < src->w; x++)
            dst->px[y * src->w + x] = src->px[y * src->w + (src->w - 1 - x)];
}

void fb_blit(Framebuffer *fb, const Sprite *s, int x0, int y0)
{
    if (!s->px) return;
    for (int y = 0; y < s->h; y++) {
        int sy = y0 + y;
        if (sy < 0 || sy >= CGA_H) continue;
        for (int x = 0; x < s->w; x++) {
            int sx = x0 + x;
            uint8_t c = s->px[y * s->w + x];
            if (sx < 0 || sx >= CGA_W || c == CGA_TRANSPARENT) continue;
            fb->px[sy * CGA_W + sx] = c;
        }
    }
}

void fb_fill(Framebuffer *fb, int x0, int y0, int w, int h, uint8_t c)
{
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            if (x >= 0 && x < CGA_W && y >= 0 && y < CGA_H) fb->px[y * CGA_W + x] = c;
}

void cga_offset_to_xy(int off, int *x, int *y)
{
    /* Pick the line pair so the byte column stays within -40..39: an offset
     * of -2642 means "33 line pairs up, 2 bytes left", not "34 up, 78 right". */
    int r = off / 80;
    int c = off - r * 80;
    if (c >= 40) { c -= 80; r++; }
    if (c < -40) { c += 80; r--; }
    *x = c * 4;
    *y = r * 2;
}
