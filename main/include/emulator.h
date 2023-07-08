#pragma once

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
#include <rtc.h>
#include <gnuboy.h>
#include <sdcard.h>
#include "audio.h"
#include "main.h"
#include "filesystems.h"
#include "ws2812.h"
#include "menu.h"
#include "graphics_wrapper.h"
#include "util.h"
#include "scanline.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "odroid_display.h"

#define AUDIO_SAMPLE_RATE (32000)
// Note: Magic number obtained by adjusting until audio buffer overflows stop.
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 10 + 1)

#define GAMEBOY_WIDTH (160)
#define GAMEBOY_HEIGHT (144)
#define AUDIO_SAMPLE_RATE (32000)

//#define AVERAGE(a, b) ( ((((a) ^ (b)) & 0xf7deU) >> 1) + ((a) & (b)) )
//#define LINE_BUFFERS (4)
//#define LINE_COUNT   (19)
//#define LINE_COUNT_UNSCALED   (4)


extern void cpu_reset();
extern int cpu_emulate(int cycles);
extern xQueueHandle button_queue;

void system_init();
void reset_and_init();
void game_loop();