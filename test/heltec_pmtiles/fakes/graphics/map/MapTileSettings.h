#pragma once
#include <cstddef>
struct MapTileSettings {
    static constexpr size_t TILE_STYLE_SIZE = 20;
    static inline const char *style = "darkmesh/";
    static inline const char *prefix = "/maps";
    static inline bool colored = false;
    static inline unsigned size = 256;
    static const char *getTileStyle() { return style; }
    static const char *getPrefix() { return prefix; }
    static unsigned getTileSize() { return size; }
    static bool color() { return colored; }
};
