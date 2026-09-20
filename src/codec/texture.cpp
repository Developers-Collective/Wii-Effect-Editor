#include "texture.h"
#include <algorithm>
#include <array>
#include <map>

namespace breff::codec {
namespace {
using Color = std::array<uint8_t, 4>;
Color rgb565(unsigned v) {
    unsigned r = v >> 11, g = (v >> 5) & 63, b = v & 31;
    return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 2) | (g >> 4)), uint8_t((b << 3) | (b >> 2)), 255};
}
Color rgb5a3(unsigned v) {
    if (v & 32768) {
        unsigned r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
        return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 3) | (g >> 2)), uint8_t((b << 3) | (b >> 2)), 255};
    }
    unsigned a = v >> 12;
    return {uint8_t(((v >> 8) & 15) * 17), uint8_t(((v >> 4) & 15) * 17), uint8_t((v & 15) * 17),
            uint8_t((a << 5) | (a << 2) | (a >> 1))};
}
Color ia8(unsigned v) {
    return {uint8_t(v), uint8_t(v), uint8_t(v), uint8_t(v >> 8)};
}
}
TextureImage decodeTexture(std::span<const uint8_t> record) {
    Reader header(record);
    header.check(0, 32);
    TextureImage result{unsigned(header.at(4, 2)), unsigned(header.at(6, 2)), unsigned(header.at(12, 1)), {}};
    auto [w, h, format, unused] = result;
    if (!w || !h || w > 1024 || h > 1024)
        throw std::runtime_error("Invalid GX texture dimensions");
    static const std::map<unsigned, std::array<unsigned, 3>> blocks = {
        {0, {8, 8, 32}}, {1, {8, 4, 32}}, {2, {8, 4, 32}}, {3, {4, 4, 32}},  {4, {4, 4, 32}}, {5, {4, 4, 32}},
        {6, {4, 4, 64}}, {8, {8, 8, 32}}, {9, {8, 4, 32}}, {10, {4, 4, 32}}, {14, {8, 8, 32}}};
    if (!blocks.contains(format))
        throw std::runtime_error("Unsupported GX texture format " + std::to_string(format));
    const auto [bw, bh, bs] = blocks.at(format);
    const unsigned nx = (w + bw - 1) / bw;
    Reader data(header.slice(32, header.at(8, 4)));
    data.check(0, nx * ((h + bh - 1) / bh) * bs);
    Reader palette(header.slice(32 + header.at(8, 4), header.at(16, 4)));
    const unsigned paletteFormat = unsigned(header.at(13, 1)), paletteCount = unsigned(header.at(14, 2));
    palette.check(0, 2 * paletteCount);
    std::vector<Color> colors;
    for (unsigned i = 0; i < paletteCount; ++i) {
        unsigned v = unsigned(palette.integer(2));
        if (paletteFormat > 2)
            throw std::runtime_error("Invalid texture palette format");
        colors.push_back(paletteFormat == 0 ? ia8(v) : paletteFormat == 1 ? rgb565(v) : rgb5a3(v));
    }
    auto indexed = [&](unsigned index) {
        if (index >= colors.size())
            throw std::runtime_error("Texture palette index out of bounds");
        return colors[index];
    };
    result.rgba.resize(w * h * 4);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            unsigned base = ((y / bh) * nx + x / bw) * bs, i = (y % bh) * bw + x % bw, v;
            Color color{};
            if (format == 0 || format == 8) {
                v = (unsigned(data.at(base + i / 2, 1)) >> (i % 2 ? 0 : 4)) & 15;
                color = format == 0 ? Color{uint8_t(v * 17), uint8_t(v * 17), uint8_t(v * 17), uint8_t(v * 17)}
                                    : indexed(v);
            } else if (format == 1 || format == 2 || format == 9) {
                v = unsigned(data.at(base + i, 1));
                color = format == 1   ? Color{uint8_t(v), uint8_t(v), uint8_t(v), uint8_t(v)}
                        : format == 2 ? Color{uint8_t((v & 15) * 17), uint8_t((v & 15) * 17), uint8_t((v & 15) * 17),
                                              uint8_t((v >> 4) * 17)}
                                      : indexed(v);
            } else if (format == 3 || format == 4 || format == 5 || format == 10) {
                v = unsigned(data.at(base + i * 2, 2));
                color = format == 3 ? ia8(v) : format == 4 ? rgb565(v) : format == 5 ? rgb5a3(v) : indexed(v & 16383);
            } else if (format == 6)
                color = {uint8_t(data.at(base + i * 2 + 1, 1)), uint8_t(data.at(base + 32 + i * 2, 1)),
                         uint8_t(data.at(base + 33 + i * 2, 1)), uint8_t(data.at(base + i * 2, 1))};
            else {
                base += ((y % 8) / 4 * 2 + (x % 8) / 4) * 8;
                const unsigned c0 = unsigned(data.at(base, 2)), c1 = unsigned(data.at(base + 2, 2));
                std::array<Color, 4> c{rgb565(c0), rgb565(c1), Color{}, Color{}};
                for (unsigned j = 0; j < 3; ++j) {
                    c[2][j] = uint8_t(c0 > c1 ? (5 * c[0][j] + 3 * c[1][j]) / 8 : (c[0][j] + c[1][j]) / 2);
                    c[3][j] = uint8_t(c0 > c1 ? (3 * c[0][j] + 5 * c[1][j]) / 8 : (c[0][j] + c[1][j]) / 2);
                }
                c[2][3] = 255;
                c[3][3] = c0 > c1 ? 255 : 0;
                color = c[(data.at(base + 4 + y % 4, 1) >> (6 - 2 * (x % 4))) & 3];
            }
            std::copy(color.begin(), color.end(), result.rgba.begin() + (y * w + x) * 4);
        }
    return result;
}
Bytes encodeTexture(unsigned w, unsigned h, std::span<const uint8_t> rgba) {
    if (!w || !h || w > 1024 || h > 1024 || rgba.size() != w * h * 4)
        throw std::runtime_error("Invalid RGBA texture dimensions");
    Writer result;
    result.zeros(32);
    result.patch(4, w, 2);
    result.patch(6, h, 2);
    result.patch(12, 6, 1);
    result.patch(20, 1, 1);
    result.patch(21, 1, 1);
    result.patch(22, 1, 1);
    for (unsigned by = 0; by < h; by += 4)
        for (unsigned bx = 0; bx < w; bx += 4) {
            std::array<uint8_t, 64> block{};
            for (unsigned y = 0; y < 4; ++y)
                for (unsigned x = 0; x < 4; ++x) {
                    const auto offset = (std::min(by + y, h - 1) * w + std::min(bx + x, w - 1)) * 4,
                               i = (y * 4 + x) * 2;
                    block[i] = rgba[offset + 3];
                    block[i + 1] = rgba[offset];
                    block[32 + i] = rgba[offset + 1];
                    block[33 + i] = rgba[offset + 2];
                }
            result.append(block);
        }
    result.patch(8, result.bytes.size() - 32, 4);
    return std::move(result.bytes);
}
}
