#include "util.h"

static const char *TAG = "util";

char rom_filename[512] = {0};

const pax_font_t *font = pax_font_saira_condensed;


//bool wait_for_button() {
//    while (1) {
//        keyboard_input_message_t message = {0};
//        if (xQueueReceive(button_queue, &message, portMAX_DELAY) == pdTRUE) {
//            if (message.state) {
//                switch (message.input) {
//                    case BUTTON_BACK:
//                        //case BUTTON_HOME:
//                    case KEY_SHIELD:
//                        return false;
//                    case BUTTON_ACCEPT:
//                        return true;
//                    default:
//                        break;
//                }
//            }
//        }
//    }
//}

char* create_savefile_path(const char* rom_filename, const char* ext) {
    if (!rom_filename) return NULL;
    if (!ext) return NULL;

    int path_end = -1;
    int ext_pos = -1;

    for (int i = strlen(rom_filename) - 1; i >= 0; i--) {
        if (ext_pos == -1) {
            if (rom_filename[i] == '.') {
                ext_pos = i;
            }
        }
        if (rom_filename[i] == '/') {
            path_end = i;
            break;
        }
    }

    if ((path_end < 0) || (ext_pos < 0)) {
        ESP_LOGE(TAG, "Can't find savefile path");
        return NULL;
    }

    char* path = strdup(rom_filename);
    if (path == NULL) return NULL;
    path[path_end] = '\0';
    char* filename = malloc(strlen(&rom_filename[path_end + 1]) + 1);
    if (filename == NULL) {
        free(path);
        return NULL;
    }
    memcpy(filename, &rom_filename[path_end + 1], strlen(&rom_filename[path_end + 1]));
    filename[strlen(&rom_filename[path_end + 1])] = '\0';
    char* filename_without_type = strdup(filename);
    if (filename_without_type == NULL) {
        free(path);
        free(filename);
        return NULL;
    }
    filename_without_type[ext_pos - strlen(path) - 1] = '\0';

    size_t result_len = strlen(path) + 1 + strlen(filename_without_type) + 1 + strlen(ext) + 1;
    char* result = malloc(result_len);
    memset(result, 0, result_len);
    if (result != NULL) {
        strcat(result, path);
        strcat(result, "/");
        strcat(result, filename_without_type);
        strcat(result, ".");
        strcat(result, ext);
        printf("%s\n", result);
    }

    free(path);
    free(filename);
    free(filename_without_type);

    return result;
}

bool load_state() {
    printf("LOADING STATE\n");
    if (rom_filename[0] == '\0') {
        show_error("No ROM loaded", 100);
        return false;
    }
    char* pathName = create_savefile_path(rom_filename, "sta");
    if (!pathName) abort();
    FILE* f = fopen(pathName, "r");
    if (f == NULL) {
        printf("load_state: fopen load failed\n");
        return false;
    } else {
        loadstate(f);
        fclose(f);
        vram_dirty();
        pal_dirty();
        sound_dirty();
        mem_updatemap();
        printf("load_state: loadstate OK.\n");
    }
    free(pathName);
    display_state("State loaded", 50);
    return true;
}

void save_state() {
    printf("SAVING STATE\n");
    if (rom_filename[0] == '\0') {
        show_error("No ROM loaded", 100);
        return;
    }
    char* pathName = create_savefile_path(rom_filename, "sta");
    if (!pathName) abort();
    FILE* f = fopen(pathName, "w");
    if (f == NULL) {
        printf("%s: fopen save failed (%s)\n", __func__, pathName);
        free(pathName);
        return;
    }
    savestate(f);
    fclose(f);
    printf("%s: savestate OK.\n", __func__);
    free(pathName);
    show_message("Game state saved", 50);
}

void load_sram() {
    printf("LOADING SRAM\n");
    if (rom_filename[0] == '\0') {
        show_error("No ROM loaded", 100);
        return;
    }
    char* pathName = create_savefile_path(rom_filename, "srm");
    if (!pathName) abort();
    FILE* f = fopen(pathName, "r");
    if (f == NULL) {
        display_state("Failed to load SRAM", 100);
    } else {
        sram_load(f);
        fclose(f);
        vram_dirty();
        pal_dirty();
        sound_dirty();
        mem_updatemap();
        printf("SRAM loaded.\n");
        display_state("SRAM loaded", 50);
    }
    free(pathName);
}

void save_sram() {
    printf("SAVING SRAM\n");
    if (rom_filename[0] == '\0') {
        show_error("No ROM loaded", 100);
        return;
    }
    char* pathName = create_savefile_path(rom_filename, "srm");
    if (!pathName) abort();
    FILE* f = fopen(pathName, "w");
    if (f == NULL) {
        printf("SRAM save failed\n");
        free(pathName);
        return;
    }
    sram_save(f);
    fclose(f);
    printf("SRAM saved.");
    free(pathName);
    display_state("SRAM saved", 100);
}

void display_state(const char* text, uint16_t delay) {
    pax_draw_image(pax_buffer, &border, 0, 0);
    pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, pax_buffer->height - 18, text);
    disp_flush();
    vTaskDelay(pdMS_TO_TICKS(delay));
}

void display_fatal_error(const char* line0, const char* line1, const char* line2, const char* line3) {
    const pax_font_t* font = pax_font_saira_regular;
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xa85a32);
    if (line0 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 23, 0, 20 * 0, line0);
    if (line1 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 1, line1);
    if (line2 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 2, line2);
    if (line3 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 3, line3);
    disp_flush();
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void show_error(const char* message, uint16_t delay) {
    ESP_LOGE(TAG, "%s", message);
    pax_background(pax_buffer, 0xa85a32);
    render_message(pax_buffer, message);
    disp_flush();
    vTaskDelay(pdMS_TO_TICKS(delay));
}

void show_message(const char* message, uint16_t delay) {
    ESP_LOGI(TAG, "%s", message);
    pax_background(pax_buffer, 0xFFFFFF);
    render_message(pax_buffer, message);
    disp_flush();
    vTaskDelay(pdMS_TO_TICKS(delay));
}