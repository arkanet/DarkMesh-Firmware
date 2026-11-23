#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
struct lv_img_dsc_t {
    const uint8_t *data;
};
struct lv_obj_t {
    const void *src = nullptr;
};
inline void lv_image_set_src(lv_obj_t *image, const void *src)
{
    image->src = src;
}
inline const void *lv_image_get_src(lv_obj_t *image)
{
    return image->src;
}
inline void lv_free(void *ptr)
{
    free(ptr);
}
