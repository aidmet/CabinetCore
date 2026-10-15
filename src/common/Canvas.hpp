#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cc {

class Canvas {
public:
    void resize(int w, int h);
    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<uint8_t>& pixels() const { return px_; }
    std::vector<uint8_t>& pixels() { return px_; }

    void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void pset(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void shade(uint8_t alpha);
    void blit(const uint8_t* src, int srcW, int srcH, int sx, int sy, int sw, int sh, int dx, int dy,
              bool flip = false);
    void text(int x, int y, const std::string& str, uint8_t r, uint8_t g, uint8_t b, int scale = 1);
    int textWidth(const std::string& str, int scale = 1) const;

private:
    int w_ = 0;
    int h_ = 0;
    std::vector<uint8_t> px_;
};

}  // namespace cc
