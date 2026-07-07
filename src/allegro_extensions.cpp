/*
 *  allegro_extensions.cpp
 *
 *  Copyright (C) 2003-2011 - Niko Ritari
 *  Copyright (C) 2003-2009 - Jani Rivinoja
 *
 *  This file is part of Outgun.
 *
 *  Outgun is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Outgun is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Outgun; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <cstdlib>

#include "incalleg.h"
#include "utility.h"

using std::max;
using std::min;

#if ALLEGRO_VERSION == 4 && ALLEGRO_SUB_VERSION == 0

void textprintf_ex(struct BITMAP* bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char* format, ...) throw () {
    text_mode(bg);
    va_list argptr;
    char xbuf[16384];
    va_start(argptr, format);
    platVsnprintf(xbuf, 16384, format, argptr);
    va_end(argptr);
    textout(bmp, f, xbuf, x, y, color);
}

void textprintf_centre_ex(struct BITMAP* bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char* format, ...) throw () {
    text_mode(bg);
    va_list argptr;
    char xbuf[16384];
    va_start(argptr, format);
    platVsnprintf(xbuf, 16384, format, argptr);
    va_end(argptr);
    textout_centre(bmp, f, xbuf, x, y, color);
}

void textprintf_right_ex(struct BITMAP* bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char* format, ...) throw () {
    text_mode(bg);
    va_list argptr;
    char xbuf[16384];
    va_start(argptr, format);
    platVsnprintf(xbuf, 16384, format, argptr);
    va_end(argptr);
    textout_right(bmp, f, xbuf, x, y, color);
}

void textout_ex(struct BITMAP* bmp, AL_CONST FONT *f, AL_CONST char* text, int x, int y, int color, int bg) throw () {
    text_mode(bg);
    textout(bmp, f, text, x, y, color);
}

void textout_centre_ex(struct BITMAP* bmp, AL_CONST FONT *f, AL_CONST char* text, int x, int y, int color, int bg) throw () {
    text_mode(bg);
    textout_centre(bmp, f, text, x, y, color);
}

void textout_right_ex(struct BITMAP* bmp, AL_CONST FONT *f, AL_CONST char* text, int x, int y, int color, int bg) throw () {
    text_mode(bg);
    textout_right(bmp, f, text, x, y, color);
}

#endif // ALLEGRO_VERSION == 4 && ALLEGRO_SUB_VERSION == 0

void set_trans_mode(int alpha) throw () {
    nAssert(alpha >= 0 && alpha <= 255);
    if (alpha != 255) {
        set_trans_blender(0, 0, 0, alpha);
        drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    }
    else
        solid_mode();
}

int makecolBounded(int r, int g, int b) throw () {
    return makecol(bound(r, 0, 255), bound(g, 0, 255), bound(b, 0, 255));
}

int set_gfx_mode_if_new(int depth, int card, int w, int h, int v_w, int v_h) throw () {
    static int oldDepth = -1, oldCard = -1, oldW = -1, oldH = -1, oldVw = -1, oldVh = -1;
    if (depth == oldDepth && card == oldCard && w == oldW && h == oldH && v_w == oldVw && v_h == oldVh)
        return 0;
    set_color_depth(depth);
    const int ret = set_gfx_mode(card, w, h, v_w, v_h);
    if (ret == 0) {
        oldDepth = depth;
        oldCard = card;
        oldW = w; oldH = h;
        oldVw = v_w; oldVh = v_h;
    }
    return ret;
}

void set_alpha_channel(BITMAP* bitmap, BITMAP* alpha) throw () {
    set_write_alpha_blender();
    drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    if (alpha)
        draw_trans_sprite(bitmap, alpha, 0, 0);
    else    // maximum alpha level
        rectfill(bitmap, 0, 0, bitmap->w - 1, bitmap->h - 1, 255);
    solid_mode();
}

void rotate_trans_sprite(BITMAP* bmp, BITMAP* sprite, int x, int y, fixed angle, int alpha) throw () { // x,y are destination coords of the sprite center
    nAssert(sprite);
    nAssert(sprite->w == sprite->h);    // if otherwise, would have to use max(sprite->w, sprite->h) below, and use more complex coords in rotate
    // make room so that rotating won't clip the corners off
    const int size = sprite->h + sprite->h / 2;
    Bitmap buffer = create_bitmap(size, size);
    nAssert(buffer);
    clear_to_color(buffer, bitmap_mask_color(buffer));
    rotate_sprite(buffer, sprite, sprite->h / 4, sprite->h / 4, angle);
    set_trans_mode(alpha);
    draw_trans_sprite(bmp, buffer, x - buffer->w / 2, y - buffer->h / 2);
    solid_mode();
}

void rotate_alpha_sprite(BITMAP* bmp, BITMAP* sprite, int x, int y, fixed angle) throw () {    // x,y are destination coords of the sprite center
    nAssert(bitmap_color_depth(sprite) == 32);
    nAssert(sprite->w == sprite->h);    // if otherwise, would have to use max(sprite->w, sprite->h) below, and use more complex coords in rotate
    // make room so that rotating won't clip the corners off
    const int size = sprite->h + sprite->h / 2;
    Bitmap buffer = create_bitmap_ex(32, size, size);
    nAssert(buffer);
    clear_to_color(buffer, bitmap_mask_color(buffer));
    rotate_sprite(buffer, sprite, sprite->h / 4, sprite->h / 4, angle);
    drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    set_alpha_blender();
    draw_trans_sprite(bmp, buffer, x - buffer->w / 2, y - buffer->h / 2);
    solid_mode();
}

int colorTo32(int color) throw () {
    return makecol32(getr(color), getg(color), getb(color));
}

void tileBlit(BITMAP* target, int x1, int y1, int x2, int y2, BITMAP* tex) throw () {
    ++x2; ++y2; // makes calculating widths easier
    const int txs = x1 % tex->w; int ws = tex->w - txs, we = x2 % tex->w, xe = x2 - we;
    const int tys = y1 % tex->h; int hs = tex->h - tys, he = y2 % tex->h, ye = y2 - he;
    if (x1 + ws >= x2) {
        ws = x2 - x1;
        xe = x1;
    }
    if (y1 + hs >= y2) {
        hs = y2 - y1;
        ye = y1;
    }
    for (int y = y1;;) {
        const int h = y == y1 ? hs : y == ye ? he : tex->h;
        const int ty = y % tex->h;
        blit(tex, target, txs, ty, x1, y, ws, h);
        for (int x = x1 + ws; x < xe; x += tex->w)
            blit(tex, target, 0, ty, x, y, tex->w, h);
        if (we)
            blit(tex, target, 0, ty, xe, y, we, h);
        if (y == ye)
            break;
        y += h;
    }
}

void dcircle(BITMAP* buf, int xc, int yc, double r, int col, bool inSolidMode) throw () {
    const bool debug = false;

    int cx0, cy0, cx1, cy1; // clipping limits relative to (xc,yc)
    get_clip_rect(buf, &cx0, &cy0, &cx1, &cy1);
    cx0 -= xc; cy0 -= yc;
    cx1 -= xc; cy1 -= yc;

    nAssert(r >= 0);
    const int r2 = iround(sqr(r + .5));
    int x = 0, y = iround(r);
    int t = y * y - r2; // generally, t = x² + y² - r²; the (filled) circle is where t <= 0

    #define CONST_PUTPIXEL_AND_CLIP(putpixelFn, clipping)               \
        do {                                                            \
            const int miny = clipping ? max(max(cx0, -cx1), max(cy0, -cy1)) : 0; \
            nAssert(!debug || !clipping || (y < miny) == (y < cx0 || -y > cx1 || y < cy0 || -y > cy1)); \
            if (clipping && y < miny)                                   \
                return; /* y only decreases, so it will never reach the proper range */ \
                                                                        \
            for (; x <= y; ) {                                          \
                if (debug) {                                            \
                    nAssert(t <= 0);                                    \
                    nAssert(t == x * x + y * y - r2);                   \
                    nAssert(y >= miny);                                 \
                    nAssert(-y <= -x && -x <= 0 && 0 <= x && x <= y);   \
                    nAssert(cx0 <= y && cy0 <= y && -y <= cx1 && -y <= cy1); /* c0 <= y  &&  -y <= c1 */ \
                }                                                       \
                if (!clipping) {                                        \
                    putpixelFn(buf, xc - x, yc - y, col);               \
                    putpixelFn(buf, xc + x, yc - y, col);               \
                    putpixelFn(buf, xc - y, yc - x, col);               \
                    putpixelFn(buf, xc + y, yc - x, col);               \
                    putpixelFn(buf, xc - y, yc + x, col);               \
                    putpixelFn(buf, xc + y, yc + x, col);               \
                    putpixelFn(buf, xc - x, yc + y, col);               \
                    putpixelFn(buf, xc + x, yc + y, col);               \
                }                                                       \
                else {                                                  \
                    if (x <= cx1) {                                     \
                        if (x >= cx0) {                                 \
                            if (y <= cy1)                               \
                                putpixelFn(buf, xc + x, yc + y, col); /*  cx0 <=   x  <= cx1  (cy0 <=)  y  <= cy1 */ \
                            if (-y >= cy0)                              \
                                putpixelFn(buf, xc + x, yc - y, col); /*  cx0 <=   x  <= cx1   cy0 <=  -y (<= cy1) */ \
                        }                                               \
                        if (y <= cx1 && x >= cy0 && -x <= cy1) {        \
                            if (x <= cy1)                               \
                                putpixelFn(buf, xc + y, yc + x, col); /* (cx0 <=)  y  <= cx1   cy0 <=   x  <= cy1 */ \
                            if (-x >= cy0)                              \
                                putpixelFn(buf, xc + y, yc - x, col); /* (cx0 <=)  y  <= cx1   cy0 <=  -x  <= cy1 */ \
                        }                                               \
                    }                                                   \
                    if (-x >= cx0) {                                    \
                        if (-x <= cx1) {                                \
                            if (y <= cy1)                               \
                                putpixelFn(buf, xc - x, yc + y, col); /*  cx0 <=  -x  <= cx1  (cy0 <=)  y  <= cy1 */ \
                            if (-y >= cy0)                              \
                                putpixelFn(buf, xc - x, yc - y, col); /*  cx0 <=  -x  <= cx1   cy0 <=  -y (<= cy1) */ \
                        }                                               \
                        if (-y >= cx0 && x >= cy0 && -x <= cy1) {       \
                            if (x <= cy1)                               \
                                putpixelFn(buf, xc - y, yc + x, col); /*  cx0 <=  -y (<= cx1)  cy0 <=   x  <= cy1 */ \
                            if (-x >= cy0)                              \
                                putpixelFn(buf, xc - y, yc - x, col); /*  cx0 <=  -y (<= cx1)  cy0 <=  -x  <= cy1 */ \
                        }                                               \
                    }                                                   \
                }                                                       \
                t += 2 * x + 1; /* (x + 1)² - x² */                     \
                ++x;                                                    \
                if (t > 0) {                                            \
                    t += 1 - 2 * y; /* (y - 1)² - y² */                 \
                    --y;                                                \
                    nAssert(!debug || !clipping || (y < miny) == (y < cx0 || -y > cx1 || y < cy0 || -y > cy1)); \
                    if (clipping && y < miny)                           \
                        break;                                          \
                }                                                       \
            }                                                           \
        } while (0)

    const int ir = iround(r);
    const bool clip = !(-ir >= cx0 && ir <= cx1 && -ir >= cy0 && ir <= cy1);

    #define CONST_PUTPIXEL(putpixelFn)                                  \
        if (clip)                                                       \
            CONST_PUTPIXEL_AND_CLIP(putpixelFn, true);                  \
        else                                                            \
            CONST_PUTPIXEL_AND_CLIP(putpixelFn, false);

    switch (!inSolidMode || !is_linear_bitmap(buf) ? 0 : bitmap_color_depth(buf)) {
    /*break;*/ case 15: case 16: CONST_PUTPIXEL(_putpixel16);
        break; case 24:          CONST_PUTPIXEL(_putpixel24);
        break; case 32:          CONST_PUTPIXEL(_putpixel32);
        break; default:          CONST_PUTPIXEL( putpixel  );
    }

    #undef CONST_PUTPIXEL
    #undef CONST_PUTPIXEL_AND_CLIP
}

void dcirclefill(BITMAP* buf, int xc, int yc, double r, int col) throw () {
    const bool debug = false;

    nAssert(r >= 0);
    const int r2 = iround(sqr(r + .5));
    int x = 0, y = iround(r);
    int t = y * y - r2; // generally, t = x² + y² - r²; the (filled) circle is where t <= 0
    for (;;) {
        if (debug) {
            nAssert(t <= 0);
            nAssert(t == x * x + y * y - r2);
        }

        hline(buf, xc - y, yc + x, xc + y, col);
        if (x)
            hline(buf, xc - y, yc - x, xc + y, col);

        if (x == y)
            break;

        t += 2 * x + 1; // (x + 1)² - x²
        if (t > 0) {
            // about to move to next y (as well as x): draw the lines at +/- y here (at this point x is at the max so the whole line is covered in one draw)
            hline(buf, xc - x, yc + y, xc + x, col);
            hline(buf, xc - x, yc - y, xc + x, col);

            t += 1 - 2 * y; // (y - 1)² - y²
            --y;
            nAssert(x <= y);
            if (x >= y)
                break;
        }
        ++x;
    }
}

static inline void directPutpixel16(BITMAP*, unsigned long lineAddressBase, int x, int, int col) { bmp_write16(lineAddressBase + 2 * x, col); }
static inline void directPutpixel24(BITMAP*, unsigned long lineAddressBase, int x, int, int col) { bmp_write24(lineAddressBase + 3 * x, col); }
static inline void directPutpixel32(BITMAP*, unsigned long lineAddressBase, int x, int, int col) { bmp_write32(lineAddressBase + 4 * x, col); }
static inline void plainPutpixel(BITMAP* buf, unsigned long, int x, int y, int col) { putpixel(buf, x, y, col); }

typedef void (*RCCF_hlineFnT)(BITMAP*, int, int, int, int, int, int, int, const RadiusColorizer&);

#define DEFINE_HLINE_FN(name, putpixelFn, putpixelIsDirect) \
    static void name(BITMAP* buf, int xc, int y, int dx, int cx0, int cx1, int r2_0, int min_r2, const RadiusColorizer& col) throw () { \
        const bool debug = false;                                       \
                                                                        \
        unsigned long lineAddressBase = 0;                              \
        if (putpixelIsDirect) {                                         \
            bmp_select(buf);                                            \
            lineAddressBase = bmp_write_line(buf, y);                   \
        }                                                               \
                                                                        \
        int r2 = r2_0;                                                  \
        int ri = static_cast<int>(sqrt(r2));                            \
        int next_ri_r2 = ri * ri;                                       \
        int x = dx;                                                     \
        int riColor = col(ri);                                          \
        const int minx = max(0, max(cx0, -cx1));                        \
        nAssert(!debug || (x < minx) == (x < cx0 || -x > cx1));         \
        while (x >= minx && r2 >= min_r2) {                             \
            nAssert(!debug || x >= cx0 && -x <= cx1);                   \
            if (x <= cx1)                                               \
                putpixelFn(buf, lineAddressBase, xc + x, y, riColor);   \
            if (-x >= cx0)                                              \
                putpixelFn(buf, lineAddressBase, xc - x, y, riColor);   \
            r2 += 1 - 2 * x; /* (x - 1)² - x² */                        \
            if (r2 < next_ri_r2) {                                      \
                next_ri_r2 += 1 - 2 * ri;                               \
                --ri;                                                   \
                riColor = col(ri);                                      \
                if (debug) {                                            \
                    nAssert(next_ri_r2 == ri * ri);                     \
                    nAssert(r2 >= next_ri_r2);                          \
                }                                                       \
            }                                                           \
            --x;                                                        \
            nAssert(!debug || x == -1 || (x < minx) == (x < cx0 || -x > cx1)); \
        }                                                               \
                                                                        \
        if (putpixelIsDirect)                                           \
            bmp_unwrite_line(buf);                                      \
    }

DEFINE_HLINE_FN(RCCF_hline16,      directPutpixel16, true )
DEFINE_HLINE_FN(RCCF_hline24,      directPutpixel24, true )
DEFINE_HLINE_FN(RCCF_hline32,      directPutpixel32, true )
DEFINE_HLINE_FN(RCCF_hlineGeneric, plainPutpixel,    false)

#undef DEFINE_HLINE_FN

static void RCCF_hlinePair(BITMAP* buf, int xc, int yc, int dx, int dy, int r2_0, int min_r2, const RadiusColorizer& col, RCCF_hlineFnT hlineFn) throw () {
    int cx0, cy0, cx1, cy1; // clipping limits; x coordinates relative to xc
    get_clip_rect(buf, &cx0, &cy0, &cx1, &cy1);
    cx0 -= xc; cx1 -= xc;

    const int y1 = yc - dy, y2 = yc + dy;
    if (y2 < cy0 || y1 > cy1)
        return;
    if (y1 >= cy0)
        hlineFn(buf, xc, y1, dx, cx0, cx1, r2_0, min_r2, col);
    if (y2 <= cy1 && y2 != y1)
        hlineFn(buf, xc, y2, dx, cx0, cx1, r2_0, min_r2, col);
}

void radiusColorizedCircleFill(BITMAP* buf, int xc, int yc, double outRad, double inRad, const RadiusColorizer& col, bool inSolidMode) throw () {
    const bool debug = false;

    RCCF_hlineFnT hlineFn;
    switch (!inSolidMode || !is_linear_bitmap(buf) ? 0 : bitmap_color_depth(buf)) {
    /*break;*/ case 15: case 16: hlineFn = RCCF_hline16;
        break; case 24:          hlineFn = RCCF_hline24;
        break; case 32:          hlineFn = RCCF_hline32;
        break; default:          hlineFn = RCCF_hlineGeneric;
    }

    nAssert(outRad >= 0);
    const int r2 = iround(sqr(outRad + .5));
    const int inR2 = iround(sqr(inRad + .5));
    int x = 0, y = iround(outRad);
    int t = y * y - r2; // generally, t = x² + y² - r²; the (filled) circle is where t <= 0
    for (;;) {
        if (debug) {
            nAssert(t <= 0);
            nAssert(t == x * x + y * y - r2);
        }

        const int r2_0 = t + r2;

        RCCF_hlinePair(buf, xc, yc, y, x, r2_0, inR2, col, hlineFn);

        if (x == y)
            break;

        t += 2 * x + 1; // (x + 1)² - x²
        if (t > 0) {
            // about to move to next y (as well as x): draw the lines at +/- y here (at this point x is at the max so the whole line is covered in one draw)
            RCCF_hlinePair(buf, xc, yc, x, y, r2_0, inR2, col, hlineFn);

            t += 1 - 2 * y; // (y - 1)² - y²
            --y;
            nAssert(x <= y);
            if (x >= y)
                break;
        }
        ++x;
    }
}

TemporaryClipRect::TemporaryClipRect(BITMAP* bitmap, int x1_, int y1_, int x2_, int y2_, bool respectOld) throw () : b(bitmap) {
    get_clip_rect(b, &x1, &y1, &x2, &y2);
    if (respectOld) {
        x1_ = max(x1_, x1);
        y1_ = max(y1_, y1);
        x2_ = min(x2_, x2);
        y2_ = min(y2_, y2);
    }
    set_clip_rect(b, x1_, y1_, x2_, y2_);
}

TemporaryClipRect::~TemporaryClipRect() throw () {
    set_clip_rect(b, x1, y1, x2, y2);
}
