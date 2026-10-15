#include "common/Canvas.hpp"

#include <algorithm>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
#include "font8x8_basic.h"
#pragma GCC diagnostic pop

namespace cc {

void Canvas::resize(int w, int h) {
    w_ = std::max(1, w);
    h_ = std::max(1, h);
    px_.assign(static_cast<size_t>(w_) * static_cast<size_t>(h_) * 4, 0);
}

void Canvas::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (size_t i = 0; i + 3 < px_.size(); i += 4) {
        px_[i] = r;
        px_[i + 1] = g;
        px_[i + 2] = b;
        px_[i + 3] = a;
    }
}

void Canvas::pset(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_ || a == 0) return;
    uint8_t* d = px_.data() + (static_cast<size_t>(y) * w_ + x) * 4;
    if (a >= 255 || d[3] == 0) {
        d[0] = r;
        d[1] = g;
        d[2] = b;
        d[3] = 255;
        return;
    }
    const int ia = a;
    const int da = 255 - ia;
    d[0] = static_cast<uint8_t>((r * ia + d[0] * da) / 255);
    d[1] = static_cast<uint8_t>((g * ia + d[1] * da) / 255);
    d[2] = static_cast<uint8_t>((b * ia + d[2] * da) / 255);
    d[3] = 255;
}

void Canvas::fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) pset(xx, yy, r, g, b, a);
}

void Canvas::shade(uint8_t alpha) {
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) pset(x, y, 0, 0, 0, alpha);
}

void Canvas::blit(const uint8_t* src, int srcW, int srcH, int sx, int sy, int sw, int sh, int dx, int dy,
                  bool flip) {
    if (!src || sw <= 0 || sh <= 0 || srcW <= 0 || srcH <= 0) return;
    for (int y = 0; y < sh; ++y) {
        const int dy0 = dy + y;
        if (dy0 < 0 || dy0 >= h_) continue;
        const int sy0 = sy + y;
        if (sy0 < 0 || sy0 >= srcH) continue;
        for (int x = 0; x < sw; ++x) {
            const int dx0 = dx + x;
            if (dx0 < 0 || dx0 >= w_) continue;
            const int sx0 = flip ? (sx + sw - 1 - x) : (sx + x);
            if (sx0 < 0 || sx0 >= srcW) continue;
            const uint8_t* s = src + (static_cast<size_t>(sy0) * srcW + sx0) * 4;
            pset(dx0, dy0, s[0], s[1], s[2], s[3]);
        }
    }
}

void Canvas::text(int x, int y, const std::string& str, uint8_t r, uint8_t g, uint8_t b, int scale) {
    if (scale < 1) scale = 1;
    int cx = x;
    for (unsigned char ch : str) {
        if (ch > 127) ch = '?';
        const unsigned char* glyph = reinterpret_cast<const unsigned char*>(font8x8_basic[ch]);
        for (int row = 0; row < 8; ++row) {
            const unsigned char bits = glyph[row];
            for (int col = 0; col < 8; ++col) {
                if (bits & (1u << col)) {
                    fillRect(cx + col * scale, y + row * scale, scale, scale, r, g, b, 255);
                }
            }
        }
        cx += 8 * scale;
    }
}

int Canvas::textWidth(const std::string& str, int scale) const {
    if (scale < 1) scale = 1;
    return static_cast<int>(str.size()) * 8 * scale;
}

}  // namespace cc
