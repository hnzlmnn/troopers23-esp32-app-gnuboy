#pragma once

#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdint.h>

#include "menu.h"
#include "pax_gfx.h"

void render_header(pax_buf_t* pax_buffer, float position_x, float position_y, float width, float height, float text_height, pax_col_t text_color,
                   pax_col_t bg_color, pax_buf_t* icon, const char* label);
void render_outline(pax_buf_t* pax_buffer, float position_x, float position_y, float width, float height, pax_col_t border_color, pax_col_t background_color);
void render_message(pax_buf_t* pax_buffer, const char* message);
