/*
 * Copyright (c) 2022 Renze Nicolai
 * Copyright (c) 2019 Fuji Pebri
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_spi_flash.h>
#include <esp_err.h>
#include <esp_log.h>
#include <soc/rtc.h>
#include <soc/rtc_cntl_reg.h>
#include <hardware.h>
#include <loader.h>
#include <hw.h>
#include <lcd.h>
#include <fb.h>
#include <cpu.h>
#include <mem.h>
#include <sound.h>
#include <pcm.h>
#include <regs.h>
#include <rtc_gnuboy.h>
#include <gnuboy.h>
#include <sdcard.h>
#include "audio.h"
#include "main.h"
#include "filesystems.h"
#include "ws2812.h"
#include "menu.h"
#include "graphics_wrapper.h"
#include "util.h"
#include "emulator.h"


static const char *TAG = "main";

extern const uint8_t border_png_start[] asm("_binary_border_png_start");
extern const uint8_t border_png_end[] asm("_binary_border_png_end");

extern const uint8_t load_png_start[] asm("_binary_load_png_start");
extern const uint8_t load_png_end[] asm("_binary_load_png_end");

extern const uint8_t save_png_start[] asm("_binary_save_png_start");
extern const uint8_t save_png_end[] asm("_binary_save_png_end");

extern const uint8_t rom_png_start[] asm("_binary_rom_png_start");
extern const uint8_t rom_png_end[] asm("_binary_rom_png_end");

extern const uint8_t state_load_png_start[] asm("_binary_state_load_png_start");
extern const uint8_t state_load_png_end[] asm("_binary_state_load_png_end");

extern const uint8_t state_save_png_start[] asm("_binary_state_save_png_start");
extern const uint8_t state_save_png_end[] asm("_binary_state_save_png_end");

extern const uint8_t play_png_start[] asm("_binary_play_png_start");
extern const uint8_t play_png_end[] asm("_binary_play_png_end");

extern const uint8_t joystick_png_start[] asm("_binary_joystick_png_start");
extern const uint8_t joystick_png_end[] asm("_binary_joystick_png_end");

extern const uint8_t volume_up_png_start[] asm("_binary_volume_up_png_start");
extern const uint8_t volume_up_png_end[] asm("_binary_volume_up_png_end");

extern const uint8_t volume_down_png_start[] asm("_binary_volume_down_png_start");
extern const uint8_t volume_down_png_end[] asm("_binary_volume_down_png_end");


static nvs_handle_t nvs_handle_gnuboy;

pax_buf_t pax_buffer;
xQueueHandle button_queue;
uint8_t* rom_data = NULL;
pax_buf_t border;


esp_err_t nvs_get_str_fixed(nvs_handle_t handle, const char* key, char* target, size_t target_size, size_t* size) {
    esp_err_t    res;

    size_t required_size;
    res = nvs_get_str(handle, key, NULL, &required_size);
    if (res != ESP_OK) {
        return res;
    }

    if (required_size > target_size) {
        return ESP_FAIL;
    }

    res = nvs_get_str(handle, key, target, &required_size);

    if (size != NULL) *size = required_size;

    return res;
}

void disp_flush() {
    ili9341_write(get_ili9341(), pax_buffer.buf);
}

void exit_to_launcher() {
    nvs_close(nvs_handle_gnuboy);
    REG_WRITE(RTC_CNTL_STORE0_REG, 0);
    esp_restart();
}

uint8_t* load_file_to_ram(FILE* fd, size_t* fsize) {
    fseek(fd, 0, SEEK_END);
    *fsize = ftell(fd);
    fseek(fd, 0, SEEK_SET);
    uint8_t* file = heap_caps_malloc(*fsize, MALLOC_CAP_SPIRAM);
    if (file == NULL) return NULL;
    fread(file, *fsize, 1, fd);
    return file;
}

typedef enum action {
    ACTION_NONE,
    ACTION_LOAD_ROM,
    ACTION_LOAD_ROM_INT,
    ACTION_LOAD_STATE,
    ACTION_STORE_STATE,
    ACTION_RUN,
    ACTION_EXIT,
    ACTION_VOLUME_DOWN,
    ACTION_VOLUME_UP,
    ACTION_RESET
} menu_action_t;

static size_t menu_pos = 0;

menu_action_t show_menu() {
    printf("show_menu()\n");
    menu_t* menu = menu_alloc("GNUBOY Gameboy emulator", 34, 18);
    menu->fgColor           = 0xFF000000;
    menu->bgColor           = 0xFFFFFFFF;
    menu->bgTextColor       = 0xFF000000;
    menu->selectedItemColor = 0xFFfec859;
    menu->borderColor       = 0xFF491d88;
    menu->titleColor        = 0xFF491d88;
    menu->titleBgColor      = 0xFFfec859;
    menu->scrollbarBgColor  = 0xFFCCCCCC;
    menu->scrollbarFgColor  = 0xFF555555;
    menu->grid_entry_count_y = 3;

    pax_buf_t icon_rom;
    pax_decode_png_buf(&icon_rom, (void*) rom_png_start, rom_png_end - rom_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_save;
    pax_decode_png_buf(&icon_save, (void*) save_png_start, save_png_end - save_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_load;
    pax_decode_png_buf(&icon_load, (void*) load_png_start, load_png_end - load_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_play;
    pax_decode_png_buf(&icon_play, (void*) play_png_start, play_png_end - play_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_state_save;
    pax_decode_png_buf(&icon_state_save, (void*) state_save_png_start, state_save_png_end - state_save_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_state_load;
    pax_decode_png_buf(&icon_state_load, (void*) state_load_png_start, state_load_png_end - state_load_png_start, PAX_BUF_32_8888ARGB, 0);    
    pax_buf_t icon_joystick;
    pax_decode_png_buf(&icon_joystick, (void*) joystick_png_start, joystick_png_end - joystick_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_volume_up;
    pax_decode_png_buf(&icon_volume_up, (void*) volume_up_png_start, volume_up_png_end - volume_up_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_volume_down;
    pax_decode_png_buf(&icon_volume_down, (void*) volume_down_png_start, volume_down_png_end - volume_down_png_start, PAX_BUF_32_8888ARGB, 0);
    
    menu_set_icon(menu, &icon_joystick);
    
    menu_insert_item_icon(menu, "Browse SD", NULL, (void*) ACTION_LOAD_ROM, -1, &icon_rom);
    menu_insert_item_icon(menu, "Browse int.", NULL, (void*) ACTION_LOAD_ROM_INT, -1, &icon_rom);
    menu_insert_item(menu, "", NULL, (void*) ACTION_NONE, -1);
    menu_insert_item_icon(menu, "Play!", NULL, (void*) ACTION_RUN, -1, &icon_play);
    menu_insert_item_icon(menu, "Vol. down", NULL, (void*) ACTION_VOLUME_DOWN, -1, &icon_volume_down);
    menu_insert_item_icon(menu, "Vol. up", NULL, (void*) ACTION_VOLUME_UP, -1, &icon_volume_up);
    menu_insert_item_icon(menu, "Load state", NULL, (void*) ACTION_LOAD_STATE, -1, &icon_state_load);
    menu_insert_item_icon(menu, "Save state", NULL, (void*) ACTION_STORE_STATE, -1, &icon_state_save);
    menu_insert_item_icon(menu, "Reset state", NULL, (void*) ACTION_RESET, -1, &icon_play);
    
    bool render = true;
    bool quit = false;
    menu_action_t action = ACTION_NONE;
    pax_noclip(&pax_buffer);
    
    menu_set_position(menu, menu_pos);

    keyboard_input_message_t message = {0};
    
    while (!quit) {
        if (render) {
            const pax_font_t* font = pax_font_saira_regular;
            pax_background(&pax_buffer, 0xFFFFFF);
            menu_render_grid(&pax_buffer, menu, 0, 0, 320, 220);//160);
            pax_draw_text(&pax_buffer, 0xFF491d88, font, 18, 5, 240 - 18, "🅰 select  🅱 exit");
            disp_flush();
            render = false;
        }
        clear_keyboard_queue();
        if (xQueueReceive(button_queue, &message, portMAX_DELAY) == pdTRUE) {
            printf("show_menu() keypress (state=%d): %d\n", message.state, message.input);
            if (message.state) {
                switch (message.input) {
                    case JOYSTICK_DOWN:
                        menu_navigate_next_row(menu);
                        render = true;
                        break;
                    case JOYSTICK_UP:
                        menu_navigate_previous_row(menu);
                        render = true;
                        break;
                    case JOYSTICK_LEFT:
                        menu_navigate_previous(menu);
                        render = true;
                        break;
                    case JOYSTICK_RIGHT:
                        menu_navigate_next(menu);
                        render = true;
                        break;
                    //case BUTTON_HOME:
                    case BUTTON_BACK:
                        action = ACTION_EXIT;
                        quit = true;
                        break;
                    case BUTTON_ACCEPT:
                    case BUTTON_SELECT:
                    case BUTTON_START:
                        action = (menu_action_t) menu_get_callback_args(menu, menu_get_position(menu));
                        quit = true;
                        break;
                    //case BUTTON_MENU:
                    case KEY_M:
                        action = ACTION_RUN;
                        quit = true;
                        break;
                    default:
                        break;
                }
            }
        }
    }
    
    menu_pos = menu_get_position(menu);
    
    menu_free(menu);
    pax_buf_destroy(&icon_joystick);
    pax_buf_destroy(&icon_load);
    pax_buf_destroy(&icon_play);
    pax_buf_destroy(&icon_rom);
    pax_buf_destroy(&icon_save);
    pax_buf_destroy(&icon_state_load);
    pax_buf_destroy(&icon_state_save);
    pax_buf_destroy(&icon_volume_up);
    pax_buf_destroy(&icon_volume_down);
    return action;
}

typedef struct _file_browser_menu_args {
    char type;
    char path[512];
    char label[512];
} file_browser_menu_args_t;

void find_parent_dir(char* path, char* parent) {
    size_t last_separator = 0;
    for (size_t index = 0; index < strlen(path); index++) {
        if (path[index] == '/') last_separator = index;
    }

    strcpy(parent, path);
    parent[last_separator] = '\0';
}

bool ends_with(const char* input, const char* end) {
    size_t input_length = strlen(input);
    size_t end_length = strlen(end);
    for (size_t position = 0; position < end_length; position++) {
        if (input[input_length - position - 1] != end[end_length - position - 1]) {
            return false;
        }
    }
    return true;
}

bool file_browser(const char* initial_path, char* selected_file) {
    bool result = false;
    char path[512] = {0};
    strncpy(path, initial_path, sizeof(path));
    bool selected = false;
    while (!selected) {
        menu_t* menu = menu_alloc(path, 20, 18);
        DIR*    dir  = opendir(path);
        if (dir == NULL) {
            if (path[0] != 0) {
                ESP_LOGE(TAG, "Failed to open directory %s", path);
                display_fatal_error("Failed to open directory", NULL, NULL, NULL);
                vTaskDelay(200 / portTICK_PERIOD_MS);
            }
            return false;
        }
        struct dirent*            ent;
        file_browser_menu_args_t* pd_args = malloc(sizeof(file_browser_menu_args_t));
        pd_args->type                     = 'd';
        find_parent_dir(path, pd_args->path);
        menu_insert_item(menu, "../", NULL, pd_args, -1);

        while ((ent = readdir(dir)) != NULL) {
            file_browser_menu_args_t* args = malloc(sizeof(file_browser_menu_args_t));
            sprintf(args->path, path);
            if (path[strlen(path) - 1] != '/') {
                strcat(args->path, "/");
            }
            strcat(args->path, ent->d_name);

            if (ent->d_type == DT_REG) {
                args->type = 'f';
            } else {
                args->type = 'd';
            }

            snprintf(args->label, sizeof(args->label), "%s%s", ent->d_name, (args->type == 'd') ? "/" : "");
            
            if ((args->type == 'f') && (!(ends_with(ent->d_name, ".gb") || ends_with(ent->d_name, ".gbc")))) {
                free(args);
            } else {
                menu_insert_item(menu, args->label, NULL, args, -1);
            }
        }
        closedir(dir);

        bool                      render   = true;
        bool                      renderbg = true;
        bool                      exit     = false;
        file_browser_menu_args_t* menuArgs = NULL;

        keyboard_input_message_t message = {0};
        while (!exit) {
            clear_keyboard_queue();
            if (xQueueReceive(button_queue, &message, 16 / portTICK_PERIOD_MS) == pdTRUE) {
                if (message.state) {
                    switch (message.input) {
                        case JOYSTICK_DOWN:
                            menu_navigate_next(menu);
                            render = true;
                            break;
                        case JOYSTICK_UP:
                            menu_navigate_previous(menu);
                            render = true;
                            break;
                        case BUTTON_BACK:
                            menuArgs = pd_args;
                            break;
                        case BUTTON_ACCEPT:
                        //case JOYSTICK_PRESS:
                        case BUTTON_SELECT:
                            menuArgs = menu_get_callback_args(menu, menu_get_position(menu));
                            break;
                        //case BUTTON_HOME:
                        case KEY_SHIELD:
                            exit = true;
                            break;
                        default:
                            break;
                    }
                }
            }

            if (renderbg) {
                pax_background(&pax_buffer, 0xFFFFFF);
                pax_noclip(&pax_buffer);
                pax_draw_text(&pax_buffer, 0xFF000000, pax_font_saira_regular, 18, 5, 240 - 19, "🅰 select  🅱 back");
                renderbg = false;
            }

            if (render) {
                menu_render(&pax_buffer, menu, 0, 0, 320, 220);
                disp_flush();
                render = false;
            }

            if (menuArgs != NULL) {
                if (menuArgs->type == 'd') {
                    strcpy(path, menuArgs->path);
                    break;
                } else {
                    printf("File selected: %s\n", menuArgs->path);
                    strcpy(selected_file, menuArgs->path);
                    result = true;
                    exit = true;
                    selected = true;
                }
                menuArgs = NULL;
                render   = true;
                renderbg = true;
            }

            if (exit) {
                break;
            }
        }

        for (size_t index = 0; index < menu_get_length(menu); index++) {
            free(menu_get_callback_args(menu, index));
        }

        menu_free(menu);
    }
    return result;
}

bool load_rom(bool browser, bool sd_card) {
    if (browser) {
        if (!file_browser(sd_card ? "/sd" : "/internal", rom_filename)) return false;
    }
    
    display_state("Loading ROM...", 0);
    
    if (rom_data != NULL) {
        free(rom_data);
        rom_data = NULL;
    }

    FILE* rom_fd = fopen(rom_filename, "rb");
    if (rom_fd == NULL) {
        memset(rom_filename, 0, sizeof(rom_filename));
        nvs_set_str(nvs_handle_gnuboy, "rom", rom_filename);
        show_error("Failed to open ROM file", 100);
        return false;
    }

    size_t rom_length;
    rom_data = load_file_to_ram(rom_fd, &rom_length);
    fclose(rom_fd);

    if (rom_data == NULL) {
        memset(rom_filename, 0, sizeof(rom_filename));
        nvs_set_str(nvs_handle_gnuboy, "rom", rom_filename);
        show_error("Failed to load ROM file", 100);
        return false;
    }

    loader_init(rom_data);
    reset_and_init();

    lcd_begin();
    sound_reset();
    
    display_state("ROM loaded", 100);
    
    nvs_set_str(nvs_handle_gnuboy, "rom", rom_filename);
    printf("ROM filename stored: '%s'\n", rom_filename);
    return true;
}

void app_main(void) {
    system_init();
    esp_err_t res;
    //audio_init();

    if (bsp_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize basic board support functions");
        esp_restart();
    }

    controller_enable_leds(get_controller(), &controller_led_callback);

    /* Initialize the LEDs */
    ws2812_init(GPIO_LED_DATA, 150);
    const uint8_t led_off[15] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    ws2812_send_data(led_off, sizeof(led_off));

    /* Turning the backlight on */
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1LL << GPIO_LCD_BL,
        .pull_down_en = 0,
        .pull_up_en   = 0,
    };
    res = gpio_config(&io_conf);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "LCD Backlight set_direction failed: %d", res);
        esp_restart();
    }
    res = gpio_set_level(GPIO_LCD_BL, true);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "LCD Backlight set_level failed: %d", res);
        esp_restart();
    }

    pax_buf_init(&pax_buffer, NULL, 320, 240, PAX_BUF_16_565RGB);

    pax_decode_png_buf(&border, (void*) border_png_start, border_png_end - border_png_start, PAX_BUF_16_565RGB, 0);

    display_state("Initializing...", 1000);

    /* Enable the amplifier */
    PCA9555* io_expander = get_io_expander();
    if (io_expander == NULL) {
        ESP_LOGE(TAG, "Failed to retrieve the IO expander");
        esp_restart();
    }
    pca9555_set_gpio_direction(io_expander, IO_AMP_ENABLE, PCA_OUTPUT);
    pca9555_set_gpio_direction(io_expander, IO_AMP_GAIN0, PCA_OUTPUT);
    pca9555_set_gpio_direction(io_expander, IO_AMP_GAIN1, PCA_OUTPUT);

    pca9555_set_gpio_value(io_expander, IO_AMP_ENABLE, 1);
    pca9555_set_gpio_value(io_expander, IO_AMP_GAIN0, 1);
    pca9555_set_gpio_value(io_expander, IO_AMP_GAIN1, 0);

    /* Enable the controller */
    Controller *controller = get_controller();
    controller_enable(controller);

    button_queue = get_keyboard()->queue;

    nvs_flash_init();
    
    /* Start internal filesystem */
    if (mount_internal_filesystem() != ESP_OK) {
        display_fatal_error("An error occured", "Failed to initialize flash FS", NULL, NULL);
        exit_to_launcher();
    }

    /* Start SD card filesystem */
    bool sdcard_mounted = (mount_sdcard_filesystem() == ESP_OK);
    if (sdcard_mounted) {
        ESP_LOGI(TAG, "SD card filesystem mounted");
        ///* LED power is on: start LED driver and turn LEDs off */
        //ws2812_init(GPIO_LED_DATA);
        //const uint8_t led_off[15] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        //ws2812_send_data(led_off, sizeof(led_off));
    } else {
        ESP_LOGE(TAG, "SD card filesystem not mounted!");
        gpio_set_level(0, 0);  // Disable power to LEDs and SD card
    }
    
    audio_init(0, AUDIO_SAMPLE_RATE);

    reset_and_init();

    lcd_begin();

    res = nvs_open("gnuboy", NVS_READWRITE, &nvs_handle_gnuboy);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %d", res);
    }

    // Default value
    audio_volume_set(2);
    int volume;
    if (nvs_get_i32(nvs_handle_gnuboy, "volume", &volume) == ESP_OK) {
        printf("Volume set to %u\r\n", volume);
        audio_volume_set(volume);
    } else {
        printf("Failed to read volume level from NVS\r\n");

    }

    nvs_get_str_fixed(nvs_handle_gnuboy, "rom", rom_filename, sizeof(rom_filename) - 1, NULL);

    printf("ROM filename: '%s'\n", rom_filename);

    if (rom_filename[0] != '\0') {
        printf("Loading Rom...\n");
        load_rom(false, false);
        if (!load_state()) {
            load_sram();
        }
        printf("Starting Game Loop ...\n");
        audio_resume();
        game_loop();
    }

    while(true) {
        audio_stop();
        menu_action_t action = show_menu();
        printf("while loop, action = %d\n", action);
        switch(action) {
            case ACTION_EXIT:
                if (rom_filename[0] != '\0') {
                    audio_stop();
                    save_sram();
                    save_state();
                }
                exit_to_launcher();
                break;
            case ACTION_LOAD_ROM:
                if (rom_filename[0] != '\0') {
                    audio_stop();
                    save_sram();
                    save_state();
                }
                if (load_rom(true, true)) {
                    if (!load_state()) {
                        load_sram();
                    }
                    audio_resume();
                    game_loop();
                }
                break;
            case ACTION_LOAD_ROM_INT:
                if (rom_filename[0] != '\0') {
                    audio_stop();
                    save_sram();
                    save_state();
                }
                if (load_rom(true, false)) {
                    if (!load_state()) {
                        load_sram();
                    }
                    audio_resume();
                    game_loop();
                }
                break;
            case ACTION_LOAD_STATE:
                load_state();
                audio_resume();
                game_loop();
                break;
            case ACTION_STORE_STATE:
                save_state();
                save_sram();
                audio_resume();
                game_loop();
                break;
            case ACTION_RUN:
                audio_resume();
                game_loop();
                break;
            case ACTION_RESET:
                if (rom_filename[0] == '\0') {
                    show_error("No ROM loaded", 100);
                } else {
                    load_rom(false, false);
                    save_state();
                    load_sram();
                    audio_resume();
                    game_loop();
                }
                break;
            case ACTION_VOLUME_DOWN: {
                int volume = audio_volume_decrease();
                nvs_set_i32(nvs_handle_gnuboy, "volume", volume);
                char message[32];
                message[31] = '\0';
                uint8_t volume_percent = 0;
                if (volume == 1) volume_percent = 25;
                if (volume == 2) volume_percent = 50;
                if (volume == 3) volume_percent = 75;
                if (volume == 4) volume_percent = 100;
                snprintf(message, sizeof(message) - 1, "Volume set to %u%%\n", volume_percent);
                show_message(message, 50);
                break;
            }
            case ACTION_VOLUME_UP: {
                int volume = audio_volume_increase();
                nvs_set_i32(nvs_handle_gnuboy, "volume", volume);
                char message[32];
                message[31] = '\0';
                uint8_t volume_percent = 0;
                if (volume == 1) volume_percent = 25;
                if (volume == 2) volume_percent = 50;
                if (volume == 3) volume_percent = 75;
                if (volume == 4) volume_percent = 100;
                snprintf(message, sizeof(message) - 1, "Volume set to %u%%\n", volume_percent);
                show_message(message, 50);
                break;
            }
            default:
                ESP_LOGW(TAG, "Action %u", (uint8_t) action);
        }
    }
    
    load_sram();
}
