#include "emulator.h"

static const char *TAG = "emulator";

static const int frameTime = (CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ * 1000000 / 60);

#define SCALING_ENABLED 1

struct fb fb;
struct pcm pcm;

int16_t *audioBuffer;

uint16_t *displayBuffer[2]; //= { fb0, fb1 }; //[160 * 144];
uint16_t* framebuffer = NULL;


struct video_update {
    scanline diff[GAMEBOY_HEIGHT];
    uint16_t *buffer;
    int stride;
};
static struct video_update update1 = {0,};
static struct video_update update2 = {0,};
static struct video_update *currentUpdate = &update1;

static uint emulatedFrames = 0;
static uint skippedFrames = 0;
static uint droppedFrames = 0;
static uint fullFrames = 0;

bool exited = false;
bool skipFrame = false;
bool forceRedraw = true;
int8_t speedupEnabled = 0;
int frame = 0;

uint16_t* line[LINE_BUFFERS];

static SemaphoreHandle_t videoReady;
volatile QueueHandle_t videoTaskQueue;


uint get_elapsed_time_since(uint start_time) {
    return xthal_get_ccount() - start_time;
}

int pcm_submit() {
    if (!speedupEnabled) {
        audio_submit(pcm.buf, pcm.pos >> 1);
    }
    pcm.pos = 0;
    return 1;
}


void run_to_vblank() {
    uint startTime = xthal_get_ccount();

    /* FIXME: djudging by the time specified this was intended
    to emulate through vblank phase which is handled at the
    end of the loop. */
    cpu_emulate(2280);

    /* FIXME: R_LY >= 0; comparsion to zero can also be removed
    altogether, R_LY is always 0 at this point */
    while (R_LY > 0 && R_LY < 144) {
        /* Step through visible line scanning phase */
        emu_step();
    }

    /* VBLANK BEGIN */
//    vid_end();
    if (!skipFrame) {
        struct video_update *previousUpdate = (currentUpdate == &update1) ? &update2 : &update1;

//        ESP_LOGI(TAG, "%d %d", currentUpdate->buffer[0], currentUpdate->buffer[1]);

        if (xSemaphoreTake(videoReady, 0) == pdTRUE) {
            xSemaphoreGive(videoReady);

            buffer_diff(currentUpdate->buffer, previousUpdate->buffer,
                        NULL, NULL,
                        GAMEBOY_WIDTH, GAMEBOY_HEIGHT,
                        GAMEBOY_WIDTH * 2, 2, 0xFF, 0,
                        currentUpdate->diff);

            if (currentUpdate->diff[0].width && currentUpdate->diff[0].repeat == GAMEBOY_HEIGHT) {
                ++fullFrames;
            }

            xQueueSend(videoTaskQueue, &currentUpdate, portMAX_DELAY);
        } else {
            ++droppedFrames;
        }

        // swap buffers
        currentUpdate = previousUpdate;
        fb.ptr = (uint8_t *) currentUpdate->buffer;
    }

    rtc_tick();

    sound_mix();

    // pcm_submit();

    if (!(R_LCDC & 0x80)) {
        /* LCDC operation stopped */
        /* FIXME: djudging by the time specified, this is
        intended to emulate through visible line scanning
        phase, even though we are already at vblank here */
        cpu_emulate(32832);
    }

    while (R_LY > 0) {
        /* Step through vblank phase */
        emu_step();
    }

    skipFrame = !skipFrame && get_elapsed_time_since(startTime) > frameTime;

    pcm_submit();
}


void videoTask(void *arg) {
    struct video_update *update;

    odroid_display_set_scale(GAMEBOY_WIDTH, GAMEBOY_HEIGHT, 1.3f);

    while (1) {
        xQueuePeek(videoTaskQueue, &update, portMAX_DELAY);

        if (!update) break;

        xSemaphoreTake(videoReady, portMAX_DELAY);

        if (SCALING_ENABLED) {
//        write_gb_frame(update->buffer);
            uint16_t xpos = 58;
            uint16_t ypos = 26;
//        ili9341_write_partial_direct(get_ili9341(), (void *) update->buffer, xpos, ypos, GAMEBOY_WIDTH, GAMEBOY_HEIGHT);
//        ili9341_write_frame_scaled(get_ili9341(), update->buffer, forceRedraw ? NULL : update->diff, // NULL, //
////                                    xpos, ypos,
//                                   GAMEBOY_WIDTH, GAMEBOY_HEIGHT,
//                                   GAMEBOY_WIDTH * 2, 2, 0xFF, NULL);
            display_write_gameboy_frame(get_st77xx(), update->buffer, forceRedraw ? NULL : update->diff,
                                        GAMEBOY_WIDTH, GAMEBOY_HEIGHT,
                                        GAMEBOY_WIDTH * 2, 2,
                                        xpos, ypos, 205, 189);
        } else {
            uint16_t xpos = (ST77XX_WIDTH - GAMEBOY_WIDTH) / 2;
            uint16_t ypos = (ST77XX_HEIGHT - GAMEBOY_HEIGHT) / 2;
            display_write_gameboy_frame(get_st77xx(), update->buffer, forceRedraw ? NULL : update->diff,
                                        GAMEBOY_WIDTH, GAMEBOY_HEIGHT,
                                        GAMEBOY_WIDTH * 2, 2,
                                        xpos, ypos, GAMEBOY_WIDTH, GAMEBOY_HEIGHT);

        }

        xSemaphoreGive(videoReady);

        xQueueReceive(videoTaskQueue, &update, portMAX_DELAY);
    }

    ESP_LOGI(TAG, "Exiting video task");

    exited = true;
    vTaskDelete(NULL);

    while (1) {}
}

void system_init() {
    // Do before odroid_system_init to make sure we get the caps requested
    displayBuffer[0] = heap_caps_calloc(GAMEBOY_WIDTH * GAMEBOY_HEIGHT, 2, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    displayBuffer[1] = heap_caps_calloc(GAMEBOY_WIDTH * GAMEBOY_HEIGHT, 2, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    audioBuffer = heap_caps_calloc(AUDIO_BUFFER_LENGTH * 2, 2, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);

    videoTaskQueue = xQueueCreate(1, sizeof(void *));

    videoReady = xSemaphoreCreateBinary();
    xSemaphoreGive(videoReady);

    update1.buffer = displayBuffer[0];
    update2.buffer = displayBuffer[1];

    odroid_display_init();
    const size_t lineSize = ST77XX_WIDTH * LINE_COUNT * sizeof(uint16_t);
    for (int x = 0; x < LINE_BUFFERS; x++)
    {
        line[x] = heap_caps_malloc(lineSize, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (line[x] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate line %u!", x);
            display_fatal_error("Failed to allocate line!", NULL, NULL, NULL);
            exit_to_launcher();
        }
        memset(line[x], 0, lineSize);
    }
}

bool update_input() {
    keyboard_input_message_t buttonMessage = {0};
    BaseType_t queueResult;
    do {
        queueResult = xQueueReceive(button_queue, &buttonMessage, 0);
        if (queueResult == pdTRUE) {
            uint8_t button = buttonMessage.input;
            bool value = buttonMessage.state;
            switch (button) {
                case JOYSTICK_DOWN:
                    pad_set(PAD_DOWN, value);
                    break;
                case JOYSTICK_UP:
                    pad_set(PAD_UP, value);
                    break;
                case JOYSTICK_LEFT:
                    pad_set(PAD_LEFT, value);
                    break;
                case JOYSTICK_RIGHT:
                    pad_set(PAD_RIGHT, value);
                    break;
                case BUTTON_ACCEPT:
                    pad_set(PAD_A, value);
                    break;
                case BUTTON_BACK:
                    pad_set(PAD_B, value);
                    break;
                case BUTTON_START:
                    pad_set(PAD_START, value);
                    break;
                case BUTTON_SELECT:
                    pad_set(PAD_SELECT, value);
                    break;
                    //case BUTTON_HOME:
//                case KEY_SHIELD:
//                    if (value) {
//                        audio_stop();
//                        save_sram();
//                        save_state();
//                        exit_to_launcher();
//                    }
//                    break;
                    //case BUTTON_MENU:
                case JOYSTICK_PUSH:
                    if (value) {
                        return true;
                    }
                default:
                    break;
            }
        }
    } while (queueResult == pdTRUE);
    return false;
}

void game_loop() {
    if (rom_filename[0] == '\0') {
        show_error("No ROM loaded", 100);
        return;
    }
    pax_draw_image(pax_buffer, &border, 0, 0);
    disp_flush();

    uint startTime;
    uint totalElapsedTime = 0;

    ESP_LOGI(TAG, "Starting game loop");

    exited = false;
    xQueueReset(videoTaskQueue);
    xTaskCreatePinnedToCore(&videoTask, "videoTask", 4096, NULL, 5, NULL, 1);

    clear_keyboard_queue();

    forceRedraw = true;

    lcd_begin();

    while (1) {
        if (update_input()) {
            break;
        }

        startTime = xthal_get_ccount();

        if (skipFrame) {
            ++skippedFrames;
        }

        run_to_vblank();

        if (speedupEnabled) {
            skipFrame = emulatedFrames % (speedupEnabled * 4);
        } else {
            skipFrame = emulatedFrames % 2;
        }

        totalElapsedTime += get_elapsed_time_since(startTime);

        ++emulatedFrames;
        ++frame;

        if (emulatedFrames == 60) {
            float seconds = totalElapsedTime / (CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ * 1000000.0f);
            float fps = emulatedFrames / seconds;

            printf("HEAP:%d, FPS:%f, SKIP:%d, FULL:%d, DROPPED:%d\n", esp_get_free_heap_size() / 1024, fps, skippedFrames,
                   fullFrames, droppedFrames);

            emulatedFrames = 0;
            skippedFrames = 0;
            fullFrames = 0;
            totalElapsedTime = 0;
            droppedFrames = 0;
        }
    }

    void *param = NULL;
    xQueueSend(videoTaskQueue, &param, portMAX_DELAY);
    while (!exited) vTaskDelay(1);
}

void reset_and_init() {
    // RTC
    memset(&rtc, 0, sizeof(rtc));

    // Video
    memset(&fb, 0, sizeof(fb));
    fb.w = 160;
    fb.h = 144;
    fb.pelsize = 2;
    fb.pitch = fb.w * fb.pelsize;
    fb.indexed = 0;
    fb.ptr = currentUpdate->buffer;
    fb.enabled = 1;
    fb.dirty = 0;

    // Audio
    memset(&pcm, 0, sizeof(pcm));
    pcm.hz = AUDIO_SAMPLE_RATE;
    pcm.stereo = 1;
    pcm.len = AUDIO_BUFFER_LENGTH; // count of 16bit samples (x2 for stereo)
    pcm.buf = audioBuffer;
    pcm.pos = 0;

    emu_reset();
}