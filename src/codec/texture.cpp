#include "texture.h"
#include <algorithm>
#include <array>
#include <map>
#include <queue>
#include <limits>

namespace breff::codec {
    using enum TextureFormat;
    using enum PaletteFormat;

    namespace {
        using Color = std::array<uint8_t, 4>;

        Color rgb565(unsigned v) {
            unsigned r = v >> 11, g = (v >> 5) & 0x3F, b = v & 0x1F;
            return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 2) | (g >> 4)), uint8_t((b << 3) | (b >> 2)), 255};
        }

        Color rgb5a3(unsigned v) {
            if (v & 0x8000) {
                unsigned r = (v >> 10) & 0x1F, g = (v >> 5) & 0x1F, b = v & 0x1F;
                return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 3) | (g >> 2)), uint8_t((b << 3) | (b >> 2)), 255};
            }
            unsigned a = v >> 12;
            return {uint8_t(((v >> 8) & 0xF) * 17), uint8_t(((v >> 4) & 0xF) * 17), uint8_t((v & 0xF) * 17),
                    uint8_t((a << 5) | (a << 2) | (a >> 1))};
        }

        Color ia8(unsigned v) {
            return {uint8_t(v), uint8_t(v), uint8_t(v), uint8_t(v >> 8)};
        }
    }

    TextureImage decodeTexture(std::span<const uint8_t> record) {
        Reader header(record);
        header.check(0x00, 32);
        TextureImage result{
            unsigned(header.at(0x04, 2)), unsigned(header.at(0x06, 2)), TextureFormat(header.at(0x0C, 1)), {}};
        auto [w, h, format, unused] = result;
        if (!w || !h || w > 1024 || h > 1024)
            throw std::runtime_error("Invalid GX texture dimensions");
        static const std::map<TextureFormat, std::array<unsigned, 3>> blocks = {
            {GX_I4, {8, 8, 32}},     {GX_I8, {8, 4, 32}},     {GX_IA4, {8, 4, 32}},   {GX_IA8, {4, 4, 32}},
            {GX_RGB565, {4, 4, 32}}, {GX_RGB5A3, {4, 4, 32}}, {GX_RGBA8, {4, 4, 64}}, {GX_C4, {8, 8, 32}},
            {GX_C8, {8, 4, 32}},     {GX_C14X2, {4, 4, 32}},  {GX_CMPR, {8, 8, 32}}};
        if (!blocks.contains(format))
            throw std::runtime_error("Unsupported GX texture format " + std::to_string(unsigned(format)));

        const auto [bw, bh, bs] = blocks.at(format);
        const unsigned nx = (w + bw - 1) / bw;
        Reader data(header.slice(0x20, header.at(0x08, 4)));
        data.check(0x00, nx * ((h + bh - 1) / bh) * bs);

        Reader palette(header.slice(0x20 + header.at(0x08, 4), header.at(0x10, 4)));
        const auto paletteFormat = PaletteFormat(header.at(0x0D, 1));
        const unsigned paletteCount = unsigned(header.at(0x0E, 2));
        palette.check(0x00, 2 * paletteCount);
        std::vector<Color> colors;
        for (unsigned i = 0; i < paletteCount; ++i) {
            unsigned v = unsigned(palette.integer(2));
            if (paletteFormat != GX_TL_IA8 && paletteFormat != GX_TL_RGB565 && paletteFormat != GX_TL_RGB5A3)
                throw std::runtime_error("Invalid texture palette format");
            colors.push_back(paletteFormat == GX_TL_IA8      ? ia8(v)
                             : paletteFormat == GX_TL_RGB565 ? rgb565(v)
                                                             : rgb5a3(v));
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
                if (format == GX_I4 || format == GX_C4) {
                    v = (unsigned(data.at(base + i / 2, 1)) >> (i % 2 ? 0 : 4)) & 0xF;
                    color = format == GX_I4 ? Color{uint8_t(v * 17), uint8_t(v * 17), uint8_t(v * 17), uint8_t(v * 17)}
                                            : indexed(v);
                } else if (format == GX_I8 || format == GX_IA4 || format == GX_C8) {
                    v = unsigned(data.at(base + i, 1));
                    color = format == GX_I8    ? Color{uint8_t(v), uint8_t(v), uint8_t(v), uint8_t(v)}
                            : format == GX_IA4 ? Color{uint8_t((v & 0xF) * 17), uint8_t((v & 0xF) * 17),
                                                       uint8_t((v & 0xF) * 17), uint8_t((v >> 4) * 17)}
                                               : indexed(v);
                } else if (format == GX_IA8 || format == GX_RGB565 || format == GX_RGB5A3 || format == GX_C14X2) {
                    v = unsigned(data.at(base + i * 2, 2));
                    color = format == GX_IA8      ? ia8(v)
                            : format == GX_RGB565 ? rgb565(v)
                            : format == GX_RGB5A3 ? rgb5a3(v)
                                                  : indexed(v & 0x3FFF);
                } else if (format == GX_RGBA8)
                    color = {uint8_t(data.at(base + i * 2 + 0x01, 1)), uint8_t(data.at(base + 0x20 + i * 2, 1)),
                             uint8_t(data.at(base + 0x21 + i * 2, 1)), uint8_t(data.at(base + i * 2, 1))};
                else {
                    base += ((y % 8) / 4 * 2 + (x % 8) / 4) * 8;
                    const unsigned c0 = unsigned(data.at(base, 2)), c1 = unsigned(data.at(base + 0x02, 2));
                    std::array<Color, 4> c{rgb565(c0), rgb565(c1), Color{}, Color{}};
                    for (unsigned j = 0; j < 3; ++j) {
                        c[2][j] = uint8_t(c0 > c1 ? (5 * c[0][j] + 3 * c[1][j]) / 8 : (c[0][j] + c[1][j]) / 2);
                        c[3][j] = uint8_t(c0 > c1 ? (3 * c[0][j] + 5 * c[1][j]) / 8 : (c[0][j] + c[1][j]) / 2);
                    }
                    c[2][3] = 255;
                    c[3][3] = c0 > c1 ? 255 : 0;
                    color = c[(data.at(base + 0x04 + y % 4, 1) >> (6 - 2 * (x % 4))) & 0x3];
                }
                std::copy(color.begin(), color.end(), result.rgba.begin() + (y * w + x) * 4);
            }

        return result;
    }

    namespace {
        unsigned quantize(unsigned value, unsigned maximum) {
            return (value * maximum + 127) / 255;
        }

        unsigned intensity(const Color& c) {
            return (77 * unsigned(c[0]) + 150 * unsigned(c[1]) + 29 * unsigned(c[2]) + 128) >> 8;
        }

        unsigned pack565(const Color& c) {
            return (quantize(c[0], 31) << 11) | (quantize(c[1], 63) << 5) | quantize(c[2], 31);
        }

        unsigned pack5a3(const Color& c) {
            if (quantize(c[3], 7) == 7)
                return 0x8000 | (quantize(c[0], 31) << 10) | (quantize(c[1], 31) << 5) | quantize(c[2], 31);
            return (quantize(c[3], 7) << 12) | (quantize(c[0], 15) << 8) | (quantize(c[1], 15) << 4) |
                   quantize(c[2], 15);
        }

        unsigned paletteWord(const Color& c, PaletteFormat format) {
            return format == GX_TL_IA8      ? (unsigned(c[3]) << 8) | intensity(c)
                   : format == GX_TL_RGB565 ? pack565(c)
                                            : pack5a3(c);
        }

        Color paletteColor(unsigned word, PaletteFormat format) {
            return format == GX_TL_IA8 ? ia8(word) : format == GX_TL_RGB565 ? rgb565(word) : rgb5a3(word);
        }

        struct Palette {
            Bytes bytes;
            std::vector<uint16_t> indices;
        };

        Palette makePalette(std::span<const uint8_t> rgba, PaletteFormat format, unsigned maximum) {
            struct Sample {
                unsigned word, count;
                Color color;
            };

            std::vector<unsigned> counts(65536);
            for (size_t i = 0; i < rgba.size(); i += 4)
                ++counts[paletteWord({rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]}, format)];

            std::vector<Sample> samples;
            for (unsigned word = 0; word < counts.size(); ++word)
                if (counts[word])
                    samples.push_back({word, counts[word], paletteColor(word, format)});

            struct Box {
                size_t begin, end;
                unsigned axis, population;
                uint64_t score;
            };

            auto box = [&](size_t begin, size_t end) {
                Color low{255, 255, 255, 255}, high{};
                unsigned population = 0;
                for (size_t i = begin; i < end; ++i) {
                    population += samples[i].count;
                    for (unsigned c = 0; c < 4; ++c) {
                        low[c] = std::min(low[c], samples[i].color[c]);
                        high[c] = std::max(high[c], samples[i].color[c]);
                    }
                }
                unsigned axis = 0;
                for (unsigned c = 1; c < 4; ++c)
                    if (high[c] - low[c] > high[axis] - low[axis])
                        axis = c;
                return Box{begin, end, axis, population, uint64_t(high[axis] - low[axis]) * population};
            };

            std::vector<Box> boxes;
            if (samples.size() <= maximum) {
                for (size_t i = 0; i < samples.size(); ++i)
                    boxes.push_back(box(i, i + 1));
            } else {
                // Weighted median cut bounds palette size without an expensive per-pixel palette search.
                boxes.push_back(box(0, samples.size()));
                std::priority_queue<std::pair<uint64_t, size_t>> pending;
                pending.emplace(boxes[0].score, 0);
                while (boxes.size() < maximum && !pending.empty()) {
                    const auto index = pending.top().second;
                    pending.pop();
                    const auto current = boxes[index];
                    if (current.end - current.begin < 2)
                        continue;
                    std::sort(samples.begin() + current.begin, samples.begin() + current.end,
                              [&](const Sample& a, const Sample& b) {
                                  return a.color[current.axis] != b.color[current.axis]
                                             ? a.color[current.axis] < b.color[current.axis]
                                             : a.word < b.word;
                              });
                    unsigned weight = 0;
                    size_t middle = current.begin;
                    do {
                        weight += samples[middle++].count;
                    } while (middle < current.end - 1 && weight < current.population / 2);
                    boxes[index] = box(current.begin, middle);
                    boxes.push_back(box(middle, current.end));
                    for (size_t i : {index, boxes.size() - 1})
                        if (boxes[i].end - boxes[i].begin > 1)
                            pending.emplace(boxes[i].score, i);
                }
            }

            Palette palette;
            palette.indices.resize(65536);
            Writer data;
            for (size_t i = 0; i < boxes.size(); ++i) {
                const auto& current = boxes[i];
                std::array<uint64_t, 4> sum{};
                for (size_t j = current.begin; j < current.end; ++j) {
                    palette.indices[samples[j].word] = uint16_t(i);
                    for (unsigned c = 0; c < 4; ++c)
                        sum[c] += uint64_t(samples[j].color[c]) * samples[j].count;
                }
                Color average{};
                for (unsigned c = 0; c < 4; ++c)
                    average[c] = uint8_t((sum[c] + current.population / 2) / current.population);
                data.integer(paletteWord(average, format), 2);
            }
            data.align(32);
            palette.bytes = std::move(data.bytes);
            return palette;
        }

        std::array<uint8_t, 8> compressBlock(const std::array<Color, 16>& pixels) {
            bool transparent = false;
            unsigned first = 0, second = 0, farthest = 0;
            for (unsigned i = 0; i < 16; ++i) {
                transparent |= pixels[i][3] < 128;
                if (pixels[i][3] < 128)
                    continue;
                first = second = pack565(pixels[i]);
                break;
            }
            for (unsigned i = 0; i < 16; ++i) {
                transparent |= pixels[i][3] < 128;
                if (pixels[i][3] < 128)
                    continue;
                for (unsigned j = i + 1; j < 16; ++j) {
                    if (pixels[j][3] < 128)
                        continue;
                    unsigned distance = 0;
                    for (unsigned c = 0; c < 3; ++c) {
                        const int difference = int(pixels[i][c]) - pixels[j][c];
                        distance += difference * difference;
                    }
                    if (distance > farthest) {
                        farthest = distance;
                        first = pack565(pixels[i]);
                        second = pack565(pixels[j]);
                    }
                }
            }

            std::array<uint8_t, 8> best{};
            unsigned bestError = std::numeric_limits<unsigned>::max();
            auto evaluate = [&](unsigned a, unsigned b) {
                if (transparent ? a > b : a < b)
                    std::swap(a, b);
                if (!transparent && a == b) {
                    if (a < 65535)
                        ++a;
                    else
                        --b;
                }
                std::array<Color, 4> colors{rgb565(a), rgb565(b), Color{}, Color{}};
                for (unsigned c = 0; c < 3; ++c) {
                    colors[2][c] = uint8_t(transparent ? (colors[0][c] + colors[1][c]) / 2
                                                       : (5 * colors[0][c] + 3 * colors[1][c]) / 8);
                    colors[3][c] = uint8_t((3 * colors[0][c] + 5 * colors[1][c]) / 8);
                }
                unsigned error = 0;
                std::array<uint8_t, 8> encoded{uint8_t(a >> 8), uint8_t(a), uint8_t(b >> 8), uint8_t(b)};
                for (unsigned i = 0; i < 16; ++i) {
                    unsigned selected = 3;
                    if (pixels[i][3] >= 128) {
                        unsigned closest = std::numeric_limits<unsigned>::max();
                        for (unsigned index = 0; index < (transparent ? 3u : 4u); ++index) {
                            unsigned distance = 0;
                            for (unsigned c = 0; c < 3; ++c) {
                                const int difference = int(pixels[i][c]) - colors[index][c];
                                distance += difference * difference;
                            }
                            if (distance < closest) {
                                closest = distance;
                                selected = index;
                            }
                        }
                        error += closest;
                    }
                    encoded[4 + i / 4] |= uint8_t(selected << (6 - 2 * (i % 4)));
                }
                if (error < bestError) {
                    bestError = error;
                    best = encoded;
                }
            };

            evaluate(first, second);
            for (unsigned pass = 0; pass < 2; ++pass) {
                const unsigned a = (unsigned(best[0]) << 8) | best[1], b = (unsigned(best[2]) << 8) | best[3];
                for (unsigned endpoint = 0; endpoint < 2; ++endpoint)
                    for (unsigned channel = 0; channel < 3; ++channel) {
                        const unsigned shift = channel == 0 ? 11 : channel == 1 ? 5 : 0;
                        const unsigned mask = channel == 1 ? 0x3F : 0x1F, word = endpoint ? b : a;
                        const int component = (word >> shift) & mask;
                        for (int change : {-1, 1}) {
                            if (component + change < 0 || component + change > int(mask))
                                continue;
                            const unsigned adjusted =
                                (word & ~(mask << shift)) | (unsigned(component + change) << shift);
                            evaluate(endpoint ? a : adjusted, endpoint ? adjusted : b);
                        }
                    }
            }
            return best;
        }
    }

    Bytes encodeTexture(unsigned w, unsigned h, std::span<const uint8_t> rgba, TextureFormat format,
                        PaletteFormat paletteFormat) {
        if (!w || !h || w > 1024 || h > 1024 || rgba.size() != w * h * 4)
            throw std::runtime_error("Invalid RGBA texture dimensions");
        const bool indexed = format == GX_C4 || format == GX_C8 || format == GX_C14X2;
        if (format != GX_I4 && format != GX_I8 && format != GX_IA4 && format != GX_IA8 && format != GX_RGB565 &&
            format != GX_RGB5A3 && format != GX_RGBA8 && !indexed && format != GX_CMPR)
            throw std::runtime_error("Unsupported GX texture format");
        if (indexed && paletteFormat != GX_TL_IA8 && paletteFormat != GX_TL_RGB565 && paletteFormat != GX_TL_RGB5A3)
            throw std::runtime_error("Unsupported GX palette format");
        const unsigned bw = format == GX_I4 || format == GX_I8 || format == GX_IA4 || format == GX_C4 ||
                                    format == GX_C8 || format == GX_CMPR
                                ? 8
                                : 4;
        const unsigned bh = format == GX_I4 || format == GX_C4 || format == GX_CMPR ? 8 : 4;

        const auto palette = indexed ? makePalette(rgba, paletteFormat,
                                                   format == GX_C4   ? 16
                                                   : format == GX_C8 ? 256
                                                                     : 16384)
                                     : Palette{};

        auto pixel = [&](unsigned x, unsigned y) {
            const auto offset = (std::min(y, h - 1) * w + std::min(x, w - 1)) * 4;
            return Color{rgba[offset], rgba[offset + 1], rgba[offset + 2], rgba[offset + 3]};
        };

        Writer result;
        result.zeros(32);
        result.patch(0x04, w, 2);
        result.patch(0x06, h, 2);
        result.patch(0x0C, unsigned(format), 1);
        result.patch(0x0D, unsigned(indexed ? paletteFormat : GX_TL_IA8), 1);
        result.patch(0x0E, palette.bytes.size() / 2, 2);
        result.patch(0x10, palette.bytes.size(), 4);
        result.patch(0x14, 1, 1);
        result.patch(0x15, 1, 1);
        result.patch(0x16, 1, 1);

        for (unsigned by = 0; by < h; by += bh)
            for (unsigned bx = 0; bx < w; bx += bw) {
                if (format == GX_CMPR) {
                    for (unsigned sy = 0; sy < 8; sy += 4)
                        for (unsigned sx = 0; sx < 8; sx += 4) {
                            std::array<Color, 16> pixels;
                            for (unsigned i = 0; i < 16; ++i)
                                pixels[i] = pixel(bx + sx + i % 4, by + sy + i / 4);
                            result.append(compressBlock(pixels));
                        }
                    continue;
                }
                if (format == GX_RGBA8) {
                    for (unsigned plane = 0; plane < 2; ++plane)
                        for (unsigned i = 0; i < 16; ++i) {
                            const auto c = pixel(bx + i % 4, by + i / 4);
                            result.integer(c[plane ? 1 : 3], 1);
                            result.integer(c[plane ? 2 : 0], 1);
                        }
                    continue;
                }
                unsigned nibble = 0;
                for (unsigned i = 0; i < bw * bh; ++i) {
                    const auto c = pixel(bx + i % bw, by + i / bw);
                    const unsigned v = indexed               ? palette.indices[paletteWord(c, paletteFormat)]
                                       : format == GX_I4     ? quantize(intensity(c), 15)
                                       : format == GX_I8     ? intensity(c)
                                       : format == GX_IA4    ? (quantize(c[3], 15) << 4) | quantize(intensity(c), 15)
                                       : format == GX_IA8    ? (unsigned(c[3]) << 8) | intensity(c)
                                       : format == GX_RGB565 ? pack565(c)
                                                             : pack5a3(c);
                    if (format == GX_I4 || format == GX_C4) {
                        if (i % 2)
                            result.integer(nibble | v, 1);
                        else
                            nibble = v << 4;
                    } else
                        result.integer(v, format == GX_I8 || format == GX_IA4 || format == GX_C8 ? 1 : 2);
                }
            }

        result.patch(0x08, result.bytes.size() - 0x20, 4);
        result.append(palette.bytes);
        return std::move(result.bytes);
    }
}
