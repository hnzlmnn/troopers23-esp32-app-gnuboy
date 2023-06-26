#include "audio.h"
#include "hardware.h"

#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "driver/i2s.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "hal/gpio_hal.h"

static bool active = false;

static float Volume = 1.0f;
static volume_level volumeLevel = VOLUME_LEVEL1;
static int volumeLevels[] = {0, 125, 250, 500, 1000};

volume_level audio_volume_get() {
    return volumeLevel;
}

void audio_volume_set(volume_level value) {
    if (value >= VOLUME_LEVEL_COUNT) {
        printf("audio_volume_set: value out of range (%d)\n", value);
        abort();
    }

    volumeLevel = value;
    Volume = (float)volumeLevels[value] * 0.001f;
}

int audio_volume_change() {
    int level = (volumeLevel + 1) % VOLUME_LEVEL_COUNT;
    audio_volume_set(level);
    return level;
}

int audio_volume_increase() {
    int level = volumeLevel + 1;
    if (level >= VOLUME_LEVEL_COUNT - 1) {
        level = VOLUME_LEVEL_COUNT - 1;
    }
    audio_volume_set(level);
    return level;
}

int audio_volume_decrease() {
    int level = (volumeLevel - 1) % VOLUME_LEVEL_COUNT;
    audio_volume_set(level);
    return level;
}

void audio_init(int i2s_num, int sample_rate) {
    i2s_config_t i2s_config = {.mode                 = I2S_MODE_MASTER | I2S_MODE_TX,
                               .sample_rate          = sample_rate,
                               .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
                               .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
                               .communication_format = I2S_COMM_FORMAT_STAND_I2S,
                               .dma_buf_count        = 8,
                               .dma_buf_len          = 256,
                               .intr_alloc_flags     = 0,
                               .use_apll             = false,
                               .bits_per_chan        = I2S_BITS_PER_SAMPLE_16BIT};

    i2s_driver_install(i2s_num, &i2s_config, 0, NULL);

    i2s_pin_config_t pin_config = {.bck_io_num = GPIO_I2S_BCLK, .ws_io_num = GPIO_I2S_WS, .data_out_num = GPIO_I2S_DATA, .data_in_num = I2S_PIN_NO_CHANGE};

    i2s_set_pin(i2s_num, &pin_config);
}

void audio_submit(short* stereoAudioBuffer, int frameCount) {
    if (!active) return;
    short currentAudioSampleCount = frameCount*2;
    for (short i = 0; i < currentAudioSampleCount; ++i) {
        int sample = stereoAudioBuffer[i] * Volume;
        if (sample > 32767)
            sample = 32767;
        else if (sample < -32767)
            sample = -32767;

        stereoAudioBuffer[i] = ((short)sample);// * 0.5;
    }
    int len = currentAudioSampleCount * sizeof(int16_t);    
    size_t count;
    i2s_write(I2S_NUM, (const char *)stereoAudioBuffer, len, &count, portMAX_DELAY);
    if (count != len) {
        printf("i2s_write_bytes: count (%d) != len (%d)\n", count, len);
        abort();
    }
}

void audio_stop() {
    if (!active) return;
    i2s_zero_dma_buffer(I2S_NUM);
    i2s_stop(I2S_NUM);
    active = false;
}

void audio_resume() {
    if (active) return;
    i2s_start(I2S_NUM);
    active = true;
}
