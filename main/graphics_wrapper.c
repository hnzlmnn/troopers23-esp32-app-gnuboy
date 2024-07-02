#include "graphics_wrapper.h"

#include <string.h>

#include "hardware.h"
#include "pax_keyboard.h"

void render_header(pax_buf_t* pax_buffer, float position_x, float position_y, float width, float height, float text_height, pax_col_t text_color,
                   pax_col_t bg_color, pax_buf_t* icon, const char* label) {
    const pax_font_t* font = pax_font_saira_regular;
    pax_simple_rect(pax_buffer, bg_color, position_x, position_y, width, height);
    pax_clip(pax_buffer, position_x + 1, position_y + ((height - text_height) / 2) + 1, width - 2, text_height);
    pax_draw_text(pax_buffer, text_color, font, text_height, position_x + ((icon != NULL) ? 32 : 0) + 1, position_y + ((height - text_height) / 2) + 1, label);
    if (icon != NULL) {
        pax_clip(pax_buffer, position_x, position_y, 32, 32);
        pax_draw_image(pax_buffer, icon, position_x, position_y);
    }
    pax_noclip(pax_buffer);
}

void render_outline(pax_buf_t* pax_buffer, float position_x, float position_y, float width, float height, pax_col_t border_color, pax_col_t background_color) {
    pax_simple_rect(pax_buffer, background_color, position_x, position_y, width, height);
    pax_outline_rect(pax_buffer, border_color, position_x, position_y, width, height);
}

void render_message(pax_buf_t* pax_buffer, const char* message) {
    const pax_font_t* font    = pax_font_saira_regular;
    pax_vec1_t        size    = pax_text_size(font, 18, message);
    float             margin  = 4;
    float             width   = size.x + (margin * 2);
    float             posX    = (pax_buffer->width - width) / 2;
    float             height  = size.y + (margin * 2);
    float             posY    = (pax_buffer->height - height) / 2;
    pax_col_t         fgColor = 0xFFfa448c;
    pax_col_t         bgColor = 0xFFFFFFFF;
    pax_simple_rect(pax_buffer, bgColor, posX, posY, width, height);
    pax_outline_rect(pax_buffer, fgColor, posX, posY, width, height);
    pax_clip(pax_buffer, posX + 1, posY + 1, width - 2, height - 2);
    pax_center_text(pax_buffer, fgColor, font, 18, pax_buffer->width / 2, ((pax_buffer->height - height) / 2) + margin, message);
    pax_noclip(pax_buffer);
}

