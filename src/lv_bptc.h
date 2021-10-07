#ifndef LV_BPTC_H
#define LV_BPTC_H

#include <stddef.h>
#include <stdint.h>

extern "C" {
typedef enum lv_bptc_format_e {
    LV_BPTC_FORMAT_UNKNOWN = 0,
    LV_BPTC_FORMAT_BC6H_SF16 = 1,
    LV_BPTC_FORMAT_BC6H_UF16 = 2,
    LV_BPTC_FORMAT_BC7_UNORM = 3,
} lv_bptc_format;

size_t lv_bptc_output_size(lv_bptc_format format, int width, int height, void const *src_data, size_t src_size);
bool lv_bptc_decode(lv_bptc_format format, int width, int height, void const *src_data, size_t src_size, void *dst_data,
                    size_t dst_size);
}

#endif // LV_BPTC_H