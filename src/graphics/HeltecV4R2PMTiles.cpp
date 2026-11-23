// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(HELTEC_V4_R2_P04)
#include "graphics/HeltecV4R2PMTiles.h"
#include "graphics/common/SdCard.h"
#include "graphics/map/MapTileSettings.h"
#include "graphics/pmtiles/PmTiles.h"
#include "lvgl.h"
#include <cstdio>
#include <cstring>

extern "C" {
bool decodeImgGrey(const void *data, size_t size, lv_img_dsc_t **img);
bool decodeImgColor(const void *data, size_t size, lv_img_dsc_t **img);
}

namespace
{
PmTiles::DirCache cache;
char cachedPath[128] = {};
uint8_t cachedHeader[PmTiles::kHeaderBytes] = {};
uint32_t cachedGeneration = 0;

bool readArchive(void *ctx, uint64_t offset, uint8_t *dst, size_t length)
{
    auto &file = *static_cast<FsFile *>(ctx);
    return file.seekSet(offset) && file.read(dst, length) == static_cast<int>(length);
}

uint32_t readBE32(const uint8_t *p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
} // namespace

bool heltecLoadPMTile(const char *name, void *img)
{
    if (!name || !img || !sdCard || !sdCard->init())
        return false;

    const char *style = MapTileSettings::getTileStyle();
    size_t length = strlen(style);
    if (length && style[length - 1] == '/')
        --length;
    if (!length || length >= MapTileSettings::TILE_STYLE_SIZE)
        return false;
    for (size_t i = 0; i < length; ++i) {
        if (style[i] == '/' || style[i] == '\\' || style[i] == '.')
            return false;
    }

    char prefix[96];
    int prefixLength = snprintf(prefix, sizeof(prefix), "%s/%.*s/", MapTileSettings::getPrefix(), int(length), style);
    if (prefixLength < 0 || prefixLength >= int(sizeof(prefix)) || strncmp(name, prefix, prefixLength) != 0)
        return false;
    unsigned z, x, y;
    int consumed = 0;
    if (sscanf(name + prefixLength, "%u/%u/%u.png%n", &z, &x, &y, &consumed) != 3 || !consumed || name[prefixLength + consumed] ||
        z > 31 || x >= (uint64_t{1} << z) || y >= (uint64_t{1} << z))
        return false;

    char path[128];
    int pathLength = snprintf(path, sizeof(path), "%s%.*s.pmtiles", prefix, int(length), style);
    if (pathLength < 0 || pathLength >= int(sizeof(path)))
        return false;
    FsFile archive = SDFs.open(path, O_RDONLY);
    if (!archive)
        return false;
    uint8_t raw[PmTiles::kHeaderBytes];
    PmTiles::Header header{};
    if (!readArchive(&archive, 0, raw, sizeof(raw)) ||
        PmTiles::checkArchive(raw, sizeof(raw), archive.fileSize(), header) != PmTiles::kCheckOk) {
        cache.clear();
        return false;
    }
    const uint32_t generation = heltecSdGeneration();
    if (generation != cachedGeneration || strcmp(path, cachedPath) || memcmp(raw, cachedHeader, sizeof(raw))) {
        cache.clear();
        strcpy(cachedPath, path);
        memcpy(cachedHeader, raw, sizeof(raw));
        cachedGeneration = generation;
    }

    uint64_t offset = 0;
    uint32_t bytes = 0;
    const auto result = PmTiles::locateTile(header, readArchive, &archive, cache, uint8_t(z), x, y, offset, bytes);
    if (result != PmTiles::kFound || bytes < 33 || bytes > 256u * 1024u) {
        if (result == PmTiles::kReadError || result == PmTiles::kCorrupt)
            cache.clear();
        return false;
    }
    auto *png = static_cast<uint8_t *>(PmTiles::allocBuffer(bytes));
    if (!png)
        return false;
    const bool read = readArchive(&archive, offset, png, bytes);
    archive.close();
    const uint32_t tileSize = MapTileSettings::getTileSize();
    const bool validPNG = read && !memcmp(png, "\x89PNG\r\n\x1a\n", 8) && readBE32(png + 8) == 13 &&
                          !memcmp(png + 12, "IHDR", 4) && readBE32(png + 16) == tileSize && readBE32(png + 20) == tileSize;
    lv_img_dsc_t *decoded = nullptr;
    const bool ok =
        validPNG && (MapTileSettings::color() ? decodeImgColor(png, bytes, &decoded) : decodeImgGrey(png, bytes, &decoded));
    PmTiles::freeBuffer(png);
    if (!read)
        cache.clear();
    if (!ok || !decoded)
        return false;
    lv_image_set_src(static_cast<lv_obj_t *>(img), decoded);
    if (lv_image_get_src(static_cast<lv_obj_t *>(img)) == decoded)
        return true;
    lv_free(const_cast<uint8_t *>(decoded->data));
    lv_free(decoded);
    return false;
}
#endif
