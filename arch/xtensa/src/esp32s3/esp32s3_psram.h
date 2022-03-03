/****************************************************************************
 * arch/xtensa/src/esp32s3/esp32s3_psram.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#ifndef __ARCH_XTENSA_SRC_ESP32S3_ESP32S3_SPIRAM_H
#define __ARCH_XTENSA_SRC_ESP32S3_ESP32S3_SPIRAM_H

#define PSRAM_SIZE_2MB                  (2 * 1024 * 1024)
#define PSRAM_SIZE_4MB                  (4 * 1024 * 1024)
#define PSRAM_SIZE_8MB                  (8 * 1024 * 1024)
#define PSRAM_SIZE_16MB                 (16 * 1024 * 1024)
#define PSRAM_SIZE_32MB                 (32 * 1024 * 1024)

#define PSRAM_CACHE_S80M                1
#define PSRAM_CACHE_S40M                2
#define PSRAM_CACHE_MAX                 3

#define SPIRAM_WRAP_MODE_16B            0
#define SPIRAM_WRAP_MODE_32B            1
#define SPIRAM_WRAP_MODE_64B            2
#define SPIRAM_WRAP_MODE_DISABLE        3

/* See the TRM, chapter PID/MPU/MMU, header 'External RAM' for the
 * definitions of these modes. Important is that NORMAL works with the app
 * CPU cache disabled, but gives huge cache coherency issues when both app
 * and pro CPU are enabled. LOWHIGH and EVENODD do not have these coherency
 * issues but cannot be used when the app CPU cache is disabled.
 */

#define PSRAM_VADDR_MODE_NORMAL   0  /* App and Pro CPU use their own flash
                                      * cache for external RAM access
                                      */

#define PSRAM_VADDR_MODE_LOWHIGH  1 /* App and Pro CPU share external RAM caches:
                                     * pro CPU has low 2M, app CPU has high 2M
                                     */

#define PSRAM_VADDR_MODE_EVENODD  2 /* App and Pro CPU share external RAM caches:
                                     * pro CPU does even 32yte ranges, app does
                                     * odd ones.
                                     */

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

/**
 * @brief To get the physical psram size in bytes.
 *
 * @param[out] out_size_bytes    physical psram size in bytes.
 */

int psram_get_physical_size(uint32_t *out_size_bytes);

/**
 * @brief To get the available physical psram size in bytes.
 *
 * If ECC is enabled, available PSRAM size will be 15/16 times its
 * physical size. If not, it equals to the physical psram size.
 * @note For now ECC is only enabled on ESP32S3 Octal PSRAM
 *
 * @param[out] out_size_bytes    availabe physical psram size in bytes.
 */

int psram_get_available_size(uint32_t *out_size_bytes);

/**
 * @brief psram cache enable function
 *
 * Esp-idf uses this to initialize cache for psram, mapping it into the
 * main memory address space.
 *
 * @param mode       SPI mode to access psram in
 * @param vaddrmode  Mode the psram cache works in.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE when VSPI peripheral
 * is needed but cannot be claimed.
 */

int psram_enable(int mode, int vaddrmode);

int esp_spiram_wrap_set(int mode);

/**
 * @brief get psram CS IO
 *
 * @return psram CS IO
 */

uint8_t psram_get_cs_io(void);

#endif
