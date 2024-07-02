#pragma once

#include <stdint.h>
#include <memory.h>
#include <stdbool.h>
#include "esp_attr.h"
#include "st77xx.h"


#define LINE_BUFFERS (2)
#define LINE_COUNT (5)
#define LINE_BUFFER_SIZE (ILI9341_WIDTH*LINE_COUNT)

// The number of pixels that need to be updated to use interrupt-based updates
// instead of polling.
#define POLLING_PIXEL_THRESHOLD (LINE_BUFFER_SIZE)

// Maximum amount of change (percent) in a frame before we trigger a full transfer
// instead of a partial update (faster). This also allows us to stop the diff early!
#define FULL_UPDATE_THRESHOLD (0.4f)

typedef struct __attribute__((__packed__)) {
    uint8_t top;
    uint8_t repeat;
    short left;
    short width;
} scanline;

void display_set_scale(short width, short height, float aspect);

void buffer_diff(void *buffer, void *old_buffer,
                 uint16_t *palette, uint16_t *old_palette,
                 short width, short height, short stride, short pixel_width,
                 uint8_t pixel_mask, uint8_t palette_shift_mask,
                 scanline *out_diff);

void display_write_gameboy_frame(ST77XX *device,
                                 void *buffer, scanline *diff,
                                 short width, short height,
                                 short stride,short pixel_width,
                                 uint16_t x_origin, uint16_t y_origin,
                                 uint16_t scaled_width, uint16_t scaled_height);