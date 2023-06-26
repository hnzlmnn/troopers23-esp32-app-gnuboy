#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "hardware.h"
#include "sdcard.h"

static const char* TAG = "fs";

static bool sdcard_mounted = false;

esp_err_t mount_internal_filesystem() {
    const esp_partition_t* fs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "locfd");
    if (fs_partition == NULL) {
        ESP_LOGE(TAG, "failed to mount locfd: partition not found");
        return ESP_FAIL;
    }

    wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files              = 5,
        .allocation_unit_size   = CONFIG_WL_SECTOR_SIZE,
    };

    esp_err_t res = esp_vfs_fat_spiflash_mount("/internal", "locfd", &mount_config, &s_wl_handle);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "failed to mount locfd (%d)", res);
        return res;
    }

    return ESP_OK;
}

esp_err_t mount_sdcard_filesystem() {
    esp_err_t res  = mount_sd(GPIO_SD_CMD, GPIO_SD_CLK, GPIO_SD_D0, 0, "/sd", false, 5);
    sdcard_mounted = (res == ESP_OK);
    return res;
}

bool get_sdcard_mounted() { return sdcard_mounted; }

void get_internal_filesystem_size_and_available(uint64_t* fs_size, uint64_t* fs_free) {
    FATFS* fs;
    DWORD  fre_clust, fre_sect, tot_sect;

    /* Get volume information and free clusters of drive 0 */
    FRESULT res = f_getfree("0:", &fre_clust, &fs);
    /* Get total sectors and free sectors */
    tot_sect = (fs->n_fatent - 2) * fs->csize;
    fre_sect = fre_clust * fs->csize;

    if (fs_size != NULL) *fs_size = tot_sect * CONFIG_WL_SECTOR_SIZE;
    if (fs_free != NULL) *fs_free = fre_sect * CONFIG_WL_SECTOR_SIZE;
}

void get_sdcard_filesystem_size_and_available(uint64_t* fs_size, uint64_t* fs_free) {
    if (!sdcard_mounted) {
        if (fs_size != NULL) *fs_size = 0;
        if (fs_free != NULL) *fs_free = 0;
        return;
    }
    FATFS* fs;
    DWORD  fre_clust, fre_sect, tot_sect;

    /* Get volume information and free clusters of drive 1 */
    FRESULT res = f_getfree("1:", &fre_clust, &fs);
    /* Get total sectors and free sectors */
    tot_sect = (fs->n_fatent - 2) * fs->csize;
    fre_sect = fre_clust * fs->csize;

    if (fs_size != NULL) *fs_size = tot_sect * 512;
    if (fs_free != NULL) *fs_free = fre_sect * 512;
}
