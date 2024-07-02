#include "scanline.h"

static bool use_polling = false;
static uint16_t *line[LINE_BUFFERS];


static inline bool pixel_diff(uint8_t *buffer1, uint8_t *buffer2,
                              uint16_t *palette1, uint16_t *palette2,
                              uint8_t pixel_mask, uint8_t palette_shift_mask,
                              int idx) {
    uint8_t p1 = (buffer1[idx] & pixel_mask);
    uint8_t p2 = (buffer2[idx] & pixel_mask);
    if (!palette1)
        return p1 != p2;

    if (palette_shift_mask) {
        if (buffer1[idx] & palette_shift_mask) p1 += (pixel_mask + 1);
        if (buffer2[idx] & palette_shift_mask) p2 += (pixel_mask + 1);
    }

    return palette1[p1] != palette2[p2];
}

void IRAM_ATTR buffer_diff(void *buffer, void *old_buffer,
                           uint16_t *palette, uint16_t *old_palette,
                           short width, short height, short stride, short pixel_width,
                           uint8_t pixel_mask, uint8_t palette_shift_mask,
                           scanline *out_diff) {
    if (!old_buffer) {
        goto _full_update;
    }

    // If the palette didn't change we can speed up things by avoiding pixel_diff()
    if (palette == old_palette || memcmp(palette, old_palette, (pixel_mask + 1) * 2) == 0) {
        pixel_mask |= palette_shift_mask;
        palette_shift_mask = 0;
        palette = NULL;
    }

    int partial_update_remaining = width * height * FULL_UPDATE_THRESHOLD;

    uint32_t u32_pixel_mask = (pixel_mask << 24) | (pixel_mask << 16) | (pixel_mask << 8) | pixel_mask;
    uint16_t u32_blocks = (width * pixel_width / 4);
    uint16_t u32_pixels = 4 / pixel_width;

    for (int y = 0, i = 0; y < height; ++y, i += stride) {
        out_diff[y].top = y;
        out_diff[y].left = width;
        out_diff[y].width = 0;
        out_diff[y].repeat = 1;

        if (!palette) {
            // This is only accurate to 4 pixels of course, but much faster
            uint32_t *buffer32 = buffer + i;
            uint32_t *old_buffer32 = old_buffer + i;
            for (short x = 0; x < u32_blocks; ++x) {
                if ((buffer32[x] & u32_pixel_mask) != (old_buffer32[x] & u32_pixel_mask)) {
                    out_diff[y].left = x * u32_pixels;
                    for (x = u32_blocks - 1; x >= 0; --x) {
                        if ((buffer32[x] & u32_pixel_mask) != (old_buffer32[x] & u32_pixel_mask)) {
                            out_diff[y].width = (((x + 1) * u32_pixels) - out_diff[y].left);
                            break;
                        }
                    }
                }
            }
        } else {
            for (int x = 0, idx = i; x < width; ++x, ++idx) {
                if (!pixel_diff(buffer, old_buffer, palette, old_palette,
                                pixel_mask, palette_shift_mask, idx)) {
                    continue;
                }
                out_diff[y].left = x;

                for (x = width - 1, idx = i + (width - 1); x >= 0; --x, --idx) {
                    if (!pixel_diff(buffer, old_buffer, palette, old_palette,
                                    pixel_mask, palette_shift_mask, idx)) {
                        continue;
                    }
                    out_diff[y].width = (x - out_diff[y].left) + 1;
                    break;
                }
                break;
            }
        }

        partial_update_remaining -= out_diff[y].width;

        if (partial_update_remaining <= 0) {
            goto _full_update;
        }
    }

    // Combine consecutive lines with similar changes location to optimize the SPI transfer
    scanline *diff = out_diff;
    for (short y = height - 1; y > 0; --y) {
        short left_diff = abs(diff[y].left - diff[y - 1].left);
        if (left_diff > 8) continue;

        short right = diff[y].left + diff[y].width;
        short right_prev = diff[y - 1].left + diff[y - 1].width;
        short right_diff = abs(right - right_prev);
        if (right_diff > 8) continue;

        if (diff[y].left < diff[y - 1].left)
            diff[y - 1].left = diff[y].left;
        diff[y - 1].width = (right > right_prev) ?
                            right - diff[y - 1].left : right_prev - diff[y - 1].left;
        diff[y - 1].repeat = diff[y].repeat + 1;
    }
    return;

    _full_update:
    out_diff[0].top = 0;
    out_diff[0].left = 0;
    out_diff[0].width = width;
    out_diff[0].repeat = height;
}


static inline uint16_t *
scale_buffer(uint16_t *buffer, short width, scanline *update, short pixel_width, uint16_t target_width,
             uint16_t target_height, float x_scale, float y_scale) {
    uint16_t size = target_width * target_height;
    uint16_t *scaled = malloc(size * pixel_width);


    for (uint16_t y = 0; y < target_height; y++) {
        uint16_t unscaled_y = (uint16_t) ((float) y / y_scale);
        uint16_t base = ((update->top + unscaled_y) * width) + (update->left);
        for (uint16_t x = 0; x < target_width; x++) {
            uint16_t unscaled_x = (uint16_t) ((float) x / x_scale);
            uint16_t sample = ((uint16_t *) buffer)[base + unscaled_x];
            scaled[y * target_width + x] = sample << 8 | sample >> 8;
        }
    }

    return scaled;
}


void display_write_gameboy_frame(ST77XX *device,
                                 void *buffer, scanline *diff,
                                 short width, short height,
                                 short stride, short pixel_width,
                                 uint16_t x_origin, uint16_t y_origin,
                                 uint16_t scaled_width, uint16_t scaled_height) {

    // Interrupt/async updates
    scanline full_update = {0, height, 0, width};

    float x_scale = (float) scaled_width / (float) width;
    float y_scale = (float) scaled_height / (float) height;

    if (diff) {
        for (short y = 0; y < height;) {
            scanline *update = &diff[y];
            if (update->width > 0) {
                if (x_scale == 1.) {
                    uint16_t base = (y * update->width) + (update->left);
                    for (int i = 0; i < update->width * update->repeat; i++) {
                        uint16_t sample = ((uint16_t *) buffer)[base + i];
                        ((uint16_t *) buffer)[base + i] = sample << 8 | sample >> 8;
                    }
                    st77xx_write_partial_direct(device, buffer + (y * stride) + (update->left * pixel_width),
                                                 x_origin + update->left,
                                                 y_origin + y,
                                                 width, height);
                } else {
                    uint16_t *scaled = scale_buffer(buffer, width, update, pixel_width, scaled_width, scaled_height,
                                                    x_scale, y_scale);
                    st77xx_write_partial_direct(device, (void *) scaled,
                                                 x_origin + (uint16_t) ((float) update->left * x_scale),
                                                 y_origin + (uint16_t) ((float) y * y_scale), scaled_width,
                                                 scaled_height);
                    free(scaled);
                }
            }
            y += update->repeat;
        }
    } else {
        if (x_scale == 1.) {
            for (int i = 0; i < full_update.width * full_update.repeat; i++) {
                uint16_t sample = ((uint16_t *) buffer)[i];
                ((uint16_t *) buffer)[i] = sample << 8 | sample >> 8;
            }
            st77xx_write_partial_direct(device, buffer,
                                         x_origin + full_update.left,
                                         y_origin + full_update.top,
                                         full_update.width, full_update.repeat);
        } else {
            uint16_t *scaled = scale_buffer(buffer, width, &full_update, pixel_width, scaled_width, scaled_height,
                                            x_scale,
                                            y_scale);
            st77xx_write_partial_direct(device, (void *) scaled, x_origin, y_origin, scaled_width, scaled_height);
            free(scaled);
        }
    }
}