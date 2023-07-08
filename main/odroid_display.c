#pragma GCC optimize ("O3")

#include "odroid_display.h"

#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/rtc_io.h"

#include <string.h>
#include <esp_log.h>

const int DUTY_MAX = 0x1fff;

const gpio_num_t SPI_PIN_NUM_MISO = GPIO_NUM_19;
const gpio_num_t SPI_PIN_NUM_MOSI = GPIO_NUM_23;
const gpio_num_t SPI_PIN_NUM_CLK = GPIO_NUM_18;
const gpio_num_t LCD_PIN_NUM_CS = GPIO_NUM_5;
const gpio_num_t LCD_PIN_NUM_DC = GPIO_NUM_21;
const gpio_num_t LCD_PIN_NUM_BCKL = GPIO_NUM_14;

const int LCD_BACKLIGHT_ON_VALUE = 1;
const int LCD_SPI_CLOCK_RATE = 48000000;

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define LINE_BUFFERS (2)
#define LINE_COUNT (5)
#define LINE_BUFFER_SIZE (SCREEN_WIDTH*LINE_COUNT)

#define SPI_TRANSACTION_COUNT (4)

// The number of pixels that need to be updated to use interrupt-based updates
// instead of polling.
#define POLLING_PIXEL_THRESHOLD (LINE_BUFFER_SIZE)

// Maximum amount of change (percent) in a frame before we trigger a full transfer
// instead of a partial update (faster). This also allows us to stop the diff early!
#define FULL_UPDATE_THRESHOLD (0.4f)

static uint16_t *line[LINE_BUFFERS];
static SemaphoreHandle_t display_mutex;
static QueueHandle_t spi_queue;
static QueueHandle_t line_buffer_queue;
static SemaphoreHandle_t spi_count_semaphore;
static spi_transaction_t global_transaction;
static spi_transaction_t trans[SPI_TRANSACTION_COUNT];
static spi_device_handle_t spi;
static bool use_polling = false;

static int BacklightLevels[] = {10, 25, 50, 75, 100};
static int BacklightLevel = ODROID_BACKLIGHT_LEVEL2;

/*
 The ILI9341 needs a bunch of command/argument values to be initialized. They are stored in this struct.
*/
typedef struct {
    uint8_t cmd;
    uint8_t data[128];
    uint8_t databytes; //No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} ili_init_cmd_t;


static inline uint16_t *line_buffer_get() {
    uint16_t *buffer;
    if (use_polling) {
        return line[0];
    }

    if (xQueueReceive(line_buffer_queue, &buffer, 1000 / portTICK_RATE_MS) != pdTRUE) {
        abort();
    }

    return buffer;
}

static void send_continue_line(ILI9341 *device, uint16_t *line, int width, int lineCount) {
    spi_transaction_t transaction = {
            .length = width * 2 * lineCount * 8,  // transaction length is in bits
            .tx_buffer = line,
            .user = (void *) device,
    };
    if (device->spi_semaphore != NULL) xSemaphoreTake(device->spi_semaphore, portMAX_DELAY);
    spi_device_transmit(device->spi_device, &transaction);
    if (device->spi_semaphore != NULL) xSemaphoreGive(device->spi_semaphore);
}

static inline void
write_rect(ILI9341 *device, void *buffer, uint16_t *palette,
           short origin_x, short origin_y, short left, short top,
           short width, short height, short stride, short pixel_width,
           uint8_t pixel_mask, short x_inc, short y_inc) {
    short actual_left = ((SCREEN_WIDTH * left) + (x_inc - 1)) / x_inc;
    short actual_top = ((SCREEN_HEIGHT * top) + (y_inc - 1)) / y_inc;
    short actual_right = ((SCREEN_WIDTH * (left + width)) + (x_inc - 1)) / x_inc;
    short actual_bottom = ((SCREEN_HEIGHT * (top + height)) + (y_inc - 1)) / y_inc;
    short actual_width = actual_right - actual_left;
    short actual_height = actual_bottom - actual_top;
    short ix_acc = (x_inc * actual_left) % SCREEN_WIDTH;
    short iy_acc = (y_inc * actual_top) % SCREEN_HEIGHT;

    if (actual_width == 0 || actual_height == 0) {
        return;
    }

    short line_count = LINE_BUFFER_SIZE / actual_width;
    for (short y = 0, y_acc = iy_acc; y < height;) {
        uint16_t *line_buffer = line_buffer_get();

        short line_buffer_index = 0;
        short lines_to_copy = 0;

        for (; (lines_to_copy < line_count) && (y < height); ++lines_to_copy) {
            for (short x = 0, x_acc = ix_acc; x < width;) {
                if (palette == NULL) {
                    uint16_t sample = ((uint16_t *) buffer)[x];
                    line_buffer[line_buffer_index++] = sample << 8 | sample >> 8;
                } else {
                    line_buffer[line_buffer_index++] = palette[((uint8_t *) buffer)[x] & pixel_mask];
                }

                x_acc += x_inc;
                while (x_acc >= SCREEN_WIDTH) {
                    ++x;
                    x_acc -= SCREEN_WIDTH;
                }
            }

            y_acc += y_inc;
            while (y_acc >= SCREEN_HEIGHT) {
                ++y;
                buffer += stride;
                y_acc -= SCREEN_HEIGHT;
            }
        }

        send_continue_line(device, line_buffer, actual_width, lines_to_copy);
    }
}

static short x_inc = SCREEN_WIDTH;
static short y_inc = SCREEN_HEIGHT;
static short x_origin = 0;
static short y_origin = 0;
static float x_scale = 1.f;
static float y_scale = 1.f;

void odroid_display_reset_scale(short width, short height) {
    x_inc = SCREEN_WIDTH;
    y_inc = SCREEN_HEIGHT;
    x_origin = (SCREEN_WIDTH - width) / 2;
    y_origin = (SCREEN_HEIGHT - height) / 2;
    x_scale = y_scale = 1.f;
}

void odroid_display_set_scale(short width, short height, float aspect) {
    float buffer_aspect = ((width * aspect) / (float) height);
    float screen_aspect = SCREEN_WIDTH / (float) SCREEN_HEIGHT;

    if (buffer_aspect < screen_aspect) {
        y_scale = SCREEN_HEIGHT / (float) height;
        x_scale = y_scale * aspect;
    } else {
        x_scale = SCREEN_WIDTH / (float) width;
        y_scale = x_scale / aspect;
    }

    x_inc = SCREEN_WIDTH / x_scale;
    y_inc = SCREEN_HEIGHT / y_scale;
    x_origin = (SCREEN_WIDTH - (width * x_scale)) / 2.f;
    y_origin = (SCREEN_HEIGHT - (height * y_scale)) / 2.f;

    printf("%dx%d@%.3f x_inc:%d y_inc:%d x_scale:%.3f y_scale:%.3f x_origin:%d y_origin:%d\n",
           width, height, aspect, x_inc, y_inc, x_scale, y_scale, x_origin, y_origin);
}

void odroid_display_lock(ILI9341 *device) {
    if (device->mutex != NULL) xSemaphoreTake(device->mutex, portMAX_DELAY);
}

void odroid_display_unlock(ILI9341 *device) {
    if (device->mutex != NULL) xSemaphoreGive(device->mutex);
}

void odroid_display_init() {
    // Line buffers
    const size_t lineSize = SCREEN_WIDTH * LINE_COUNT * sizeof(uint16_t);

    for (short x = 0; x < LINE_BUFFERS; x++) {
        line[x] = heap_caps_malloc(lineSize, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!line[x]) abort();
    }
}

void ili9341_write_frame_scaled(ILI9341 *device, void *buffer, odroid_scanline *diff,
                                short width, short height, short stride,
                                short pixel_width, uint8_t pixel_mask,
                                uint16_t *palette) {
    if (!buffer) {
        return;
    }

    odroid_display_lock(device);

    // Interrupt/async updates
    odroid_scanline int_updates[SCREEN_HEIGHT / LINE_COUNT];
    odroid_scanline *int_ptr = &int_updates[0];
    odroid_scanline full_update = {0, height, 0, width};

    if (diff) {
        use_polling = true; // Do polling updates first
        for (short y = 0; y < height;) {
            odroid_scanline *update = &diff[y];

            if (update->width > 0) {
                int n_pixels = (x_scale * update->width) * (y_scale * update->repeat);
                if (n_pixels < POLLING_PIXEL_THRESHOLD) {
                    write_rect(device, buffer + (y * stride) + (update->left * pixel_width), palette,
                               x_origin, y_origin, update->left, y, update->width, update->repeat,
                               stride, pixel_width, pixel_mask, x_inc, y_inc);
                } else {
                    (*int_ptr++) = (*update);
                }
            }
            y += update->repeat;
        }
    } else {
        (*int_ptr++) = full_update;
    }

    use_polling = true; // Use interrupt updates for larger areas
    while (--int_ptr >= &int_updates) {
        write_rect(device, buffer + (int_ptr->top * stride) + (int_ptr->left * pixel_width), palette,
                   x_origin, y_origin, int_ptr->left, int_ptr->top, int_ptr->width, int_ptr->repeat,
                   stride, pixel_width, pixel_mask, x_inc, y_inc);
    }

    odroid_display_unlock(device);
}

static inline bool
pixel_diff(uint8_t *buffer1, uint8_t *buffer2,
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

void IRAM_ATTR
odroid_buffer_diff(void *buffer, void *old_buffer,
                   uint16_t *palette, uint16_t *old_palette,
                   short width, short height, short stride, short pixel_width,
                   uint8_t pixel_mask, uint8_t palette_shift_mask,
                   odroid_scanline *out_diff) {
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
    odroid_scanline *diff = out_diff;
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