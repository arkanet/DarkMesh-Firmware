// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/HeltecV4R2PMTiles.h"
#include "graphics/common/SdCard.h"
#include "graphics/map/MapTileSettings.h"
#include "lvgl.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

SdFs SDFs;
ISdCard card;
ISdCard *sdCard = &card;
uint32_t generation = 1;
uint32_t heltecSdGeneration()
{
    return generation;
}
bool colorDecoded = false;
std::vector<uint8_t> decodedBytes;

static bool decode(const void *data, size_t size, lv_img_dsc_t **img, bool color)
{
    assert(openHandles == 0);
    assert(size >= 33 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8));
    colorDecoded = color;
    const auto *start = static_cast<const uint8_t *>(data);
    decodedBytes.assign(start, start + size);
    *img = static_cast<lv_img_dsc_t *>(malloc(sizeof(lv_img_dsc_t)));
    (*img)->data = static_cast<uint8_t *>(malloc(1));
    return true;
}
extern "C" bool decodeImgGrey(const void *data, size_t size, lv_img_dsc_t **img)
{
    return decode(data, size, img, false);
}
extern "C" bool decodeImgColor(const void *data, size_t size, lv_img_dsc_t **img)
{
    return decode(data, size, img, true);
}

static bool load(const char *path)
{
    lv_obj_t image;
    const bool ok = heltecLoadPMTile(path, &image);
    if (image.src) {
        auto *d = static_cast<const lv_img_dsc_t *>(image.src);
        free(const_cast<uint8_t *>(d->data));
        free(const_cast<lv_img_dsc_t *>(d));
    }
    assert(openHandles == 0);
    return ok;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    namespace fs = std::filesystem;
    const fs::path fixtures = argv[1];
    SDFs.root = (fixtures / "card").string();
    fs::create_directories(SDFs.root + "/maps/darkmesh");
    const fs::path archive = SDFs.root + "/maps/darkmesh/darkmesh.pmtiles";
    auto install = [&](const char *name) {
        fs::copy_file(fixtures / (std::string(name) + ".pmtiles"), archive, fs::copy_options::overwrite_existing);
        ++generation;
    };
    install("raster_root");
    assert(load("/maps/darkmesh/0/0/0.png"));
    const auto rootBytes = decodedBytes;
    assert(lv_malloc_calls > 0);
    ++generation;
    lv_fail_allocations = true;
    assert(!load("/maps/darkmesh/0/0/0.png"));
    lv_fail_allocations = false;
    assert(load("/maps/darkmesh/0/0/0.png"));
    assert(!colorDecoded);
    MapTileSettings::colored = true;
    assert(load("/maps/darkmesh/0/0/0.png") && colorDecoded);
    MapTileSettings::colored = false;
    assert(!load("/maps/darkmesh/0/1/0.png"));
    assert(!load("/maps/darkmesh/0/0/0.png.bak"));
    assert(!load("/maps/other/0/0/0.png"));
    assert(!load("/maps/darkmesh/32/0/0.png"));
    assert(!load("/maps/darkmesh/0/0/0.jpg"));
    MapTileSettings::style = "../";
    assert(!load("/maps/darkmesh/0/0/0.png"));
    MapTileSettings::style = "darkmesh/";
    MapTileSettings::size = 512;
    assert(!load("/maps/darkmesh/0/0/0.png"));
    MapTileSettings::size = 256;
    card.ready = false;
    assert(!load("/maps/darkmesh/0/0/0.png"));
    card.ready = true;
    failRead = true;
    assert(!load("/maps/darkmesh/0/0/0.png"));
    failRead = false;
    assert(load("/maps/darkmesh/0/0/0.png"));
    assert(decodedBytes == rootBytes);
    for (auto name : {"vector_mvt", "raster_jpeg", "raster_gzip_tiles", "brotli_dirs", "not_v3", "truncated", "bad_crc"}) {
        install(name);
        assert(!load("/maps/darkmesh/0/0/0.png"));
    }
    install("raster_plain_dirs");
    assert(load("/maps/darkmesh/0/0/0.png"));
    install("raster_leaf");
    assert(load("/maps/darkmesh/0/0/0.png"));
    fs::rename(archive, archive.string() + ".bak");
    assert(!load("/maps/darkmesh/0/0/0.png"));
    fs::rename(archive.string() + ".bak", archive);
    assert(load("/maps/darkmesh/0/0/0.png"));
    MapTileSettings::style = "missing/";
    assert(!load("/maps/missing/0/0/0.png"));
    puts("DarkMesh PMTiles adapter: PASS (styles, formats, failures, remount, "
         "grey/color, file lifetime)");
}
