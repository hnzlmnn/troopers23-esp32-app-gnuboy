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
#include <rtc_gnuboy.h>
#include <gnuboy.h>
#include <sdcard.h>
#include "audio.h"
#include "main.h"
#include "filesystems.h"
#include "ws2812.h"
#include "menu.h"
#include "graphics_wrapper.h"

extern char rom_filename[512];
extern pax_buf_t* pax_buffer;
extern pax_buf_t border;

char* create_savefile_path(const char* rom_filename, const char* ext);
bool load_state();
void save_state();
void load_sram();
void save_sram();
void display_state(const char* text, uint16_t delay);
void display_fatal_error(const char* line0, const char* line1, const char* line2, const char* line3);
void show_error(const char* message, uint16_t delay);
void show_message(const char* message, uint16_t delay);