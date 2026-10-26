/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Waveshare ESP32-S3-Touch-LCD-1.54: ESP32-S3R8, 16 MB flash, 8 MB octal
 * PSRAM, 1.54" 240x240 ST7789 TFT over SPI, CST816S touch, ES8311 speaker
 * (NS4150B amp) + ES7210 dual-mic ADC, QMI8658 IMU, microSD, no PMU.
 * Three buttons: KEY_PLUS (GPIO4), KEY_PWR (GPIO5), KEY_MINUS/BOOT (GPIO0).
 * PWR talks, BOOT is aux. On battery the board needs GPIO2 (BAT_EN) held
 * high or its power latch opens and the rail collapses.
 *
 * Pins from waveshareteam/ESP32-S3-Touch-LCD-1.54,
 * examples/ESP32-S3-Touch-LCD-1.54-demo/Arduino-3.2.0/examples/:
 *   09_lvgl_arduino_v9.ino: LCD SPI (DC 45, CS 21, SCK 38, MOSI 39, RST 40,
 *     BL 46), touch I2C (SDA 42, SCL 41, IRQ 48, RST 47)
 *   01_i2s_audio.ino: I2S (MCLK 8, BCLK 9, WS 10, DOUT 12), codec I2C
 *     (SDA 42, SCL 41), ES8311 at 0x18, PA_CTRL 7
 *   10_esp_sr.ino: ES7210 at 0x40, I2S DIN 11 (its DOUT 12 agrees with the
 *     speaker example; daich/bbtalkie-waveshare-lcd154's table has them
 *     swapped, so check on hardware if the mic misbehaves)
 *   02_button_example.ino: buttons on GPIO 0, 5, 4
 *   07_sd_card_test.ino: SDMMC (CLK 16, CMD 15, D0-D3 17/18/13/14)
 *   03_qmi8658_example.ino: IMU on the shared I2C bus
 * Button roles (KEY_PLUS/KEY_PWR/KEY_MINUS) and the battery latch (BAT_EN
 * GPIO2, 2:1 divider on ADC1_CH0, CHG_STAT GPIO3) cross-checked against
 * daich/bbtalkie-waveshare-lcd154 and rbouteiller/airplay-esp32 PR #90,
 * both validated on this hardware.
 *
 * No vendor BSP exists for this board in the ESP Component Registry, so the
 * panel, touch and codecs are driven directly (the AIPI Lite pattern).
 */
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "muse_audio.h"
#include "muse_board.h"
#include "muse_mem.h"

static const char *TAG = "board";

/* ST7789 over SPI. */
#define LCD_RES 240
#define LCD_HOST SPI3_HOST
#define LCD_SCLK GPIO_NUM_38
#define LCD_MOSI GPIO_NUM_39
#define LCD_CS GPIO_NUM_21
#define LCD_DC GPIO_NUM_45
#define LCD_RST GPIO_NUM_40
#define LCD_BL GPIO_NUM_46
/* GRAM is 240x320, but this module's glass is rows 0-239. Waveshare's
 * ESP-IDF demo leaves the gap at 0. A Y gap of 80 left the top 80 rows
 * unwritten, which showed up as static. */
#define DRAW_BUF_LINES 32

/* Shared I2C: touch, codecs, IMU. */
#define I2C_SDA GPIO_NUM_42
#define I2C_SCL GPIO_NUM_41
#define TOUCH_INT GPIO_NUM_48
#define TOUCH_RST GPIO_NUM_47

/* I2S: ES8311 speaker out, ES7210 dual-mic in, one duplex bus. */
#define I2S_MCLK GPIO_NUM_8
#define I2S_BCLK GPIO_NUM_9
#define I2S_WS GPIO_NUM_10
#define I2S_DOUT GPIO_NUM_12
#define I2S_DIN GPIO_NUM_11
#define PA_EN GPIO_NUM_7

#define TALK_GPIO GPIO_NUM_5       /* KEY_PWR */
#define AUX_GPIO GPIO_NUM_0        /* KEY_MINUS / BOOT */

/* Battery: ADC1_CH0 through the divider, enabled by the latch pin. */
#define BAT_EN_GPIO GPIO_NUM_2
#define BATT_ADC ADC_CHANNEL_0     /* GPIO1 */
#define CHG_GPIO GPIO_NUM_3        /* CHG_STAT, active low */

static i2c_master_bus_handle_t s_i2c;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_tp;
static muse_gpio_button_t s_talk, s_aux;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok;

static esp_err_t init(void)
{
    /* Hold the battery power latch: on battery the rail collapses when PWR
     * is released unless this stays high. A prior power-off may have left
     * the hold on, so release it before re-driving. Harmless on USB. */
    gpio_hold_dis(BAT_EN_GPIO);
    gpio_reset_pin(BAT_EN_GPIO);
    gpio_set_direction(BAT_EN_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BAT_EN_GPIO, 1);

    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_i2c), TAG, "i2c");
    ESP_RETURN_ON_ERROR(muse_gpio_button_init(&s_talk, TALK_GPIO), TAG, "talk button");
    ESP_RETURN_ON_ERROR(muse_gpio_button_init(&s_aux, AUX_GPIO), TAG, "aux button");
    /* PWR turned the board on and may still be held; don't count that. */
    s_talk.pressed = gpio_get_level(TALK_GPIO) == 0;

    gpio_config_t chg = {
        .pin_bit_mask = 1ULL << CHG_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&chg), TAG, "charge pin");
    const adc_oneshot_unit_init_cfg_t adc_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&adc_cfg, &s_adc), TAG, "adc");
    const adc_oneshot_chan_cfg_t ch_cfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, BATT_ADC, &ch_cfg), TAG, "adc channel");
    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = BATT_ADC,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK;
    if (!s_cali_ok) {
        ESP_LOGW(TAG, "ADC calibration unavailable; battery voltage disabled");
    }
    return ESP_OK;
}

static lv_display_t *display_start(lv_indev_t **touch)
{
    const ledc_timer_config_t bl_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t bl_ch = {
        .gpio_num = LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    if (ledc_timer_config(&bl_timer) != ESP_OK || ledc_channel_config(&bl_ch) != ESP_OK) {
        return NULL;
    }

    const spi_bus_config_t bus = {
        .sclk_io_num = LCD_SCLK,
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_RES * DRAW_BUF_LINES * 2,
    };
    if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        return NULL;
    }
    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &io) != ESP_OK) {
        return NULL;
    }
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel) != ESP_OK) {
        return NULL;
    }
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, false);
    esp_lcd_panel_disp_on_off(s_panel, true);

    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_core_id = MUSE_UI_CORE;
    adapter_cfg.task_priority = MUSE_UI_PRIORITY;
    if (esp_lv_adapter_init(&adapter_cfg) != ESP_OK) {
        return NULL;
    }
    const esp_lv_adapter_display_config_t disp_cfg = {
        .panel = s_panel,
        .panel_io = io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = LCD_RES,
            .ver_res = LCD_RES,
            .buffer_height = DRAW_BUF_LINES,
            .use_psram = false,
            .require_double_buffer = true,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    if (!disp) {
        return NULL;
    }

    /* CST816S touch on the shared I2C bus. Orientation flags from
     * rbouteiller/airplay-esp32's bring-up of this panel; re-check the
     * axes on hardware. */
    esp_lcd_panel_io_handle_t tp_io;
    const esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    if (esp_lcd_new_panel_io_i2c(s_i2c, &tp_io_cfg, &tp_io) != ESP_OK) {
        return NULL;
    }
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_RES,
        .y_max = LCD_RES,
        .rst_gpio_num = TOUCH_RST,
        .int_gpio_num = TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };
    if (esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &s_tp) != ESP_OK) {
        return NULL;
    }
    const esp_lv_adapter_touch_config_t tp_lv_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, s_tp);
    *touch = esp_lv_adapter_register_touch(&tp_lv_cfg);
    if (!*touch || esp_lv_adapter_start() != ESP_OK) {
        return NULL;
    }
    return disp;
}

static bool display_lock(int timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
}

static void set_brightness(int pct)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, pct * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void panel_sleep(bool sleep)
{
    esp_lcd_panel_disp_sleep(s_panel, sleep);   /* SLPIN/SLPOUT; GRAM is kept */
    vTaskDelay(pdMS_TO_TICKS(120));             /* settle before the next command */
}

static void display_pause(bool pause)
{
    if (pause) {
        esp_lv_adapter_pause(-1);
    } else {
        esp_lv_adapter_resume();
    }
}

/* ES8311 drives the speaker, ES7210 the dual mics, over one duplex I2S bus
 * the ESP32 masters. MCLK is wired, so both codecs take it from the pin. */
static esp_err_t audio_init(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic)
{
    i2s_chan_handle_t tx, rx;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx, &rx), TAG, "i2s channel");
    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_DOUT,
            .din = I2S_DIN,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std_cfg), TAG, "i2s tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std_cfg), TAG, "i2s rx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx), TAG, "i2s tx on");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx), TAG, "i2s rx on");

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .rx_handle = rx, .tx_handle = tx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(data_if && gpio_if, ESP_ERR_NO_MEM, TAG, "codec interfaces");

    audio_codec_i2c_cfg_t spk_i2c = { .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = s_i2c };
    const audio_codec_ctrl_if_t *spk_ctrl = audio_codec_new_i2c_ctrl(&spk_i2c);
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = spk_ctrl,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = PA_EN,
        .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    const audio_codec_if_t *spk_codec = es8311_codec_new(&es8311_cfg);
    ESP_RETURN_ON_FALSE(spk_codec, ESP_FAIL, TAG, "ES8311 not responding");

    audio_codec_i2c_cfg_t mic_i2c = { .port = I2C_NUM_0, .addr = ES7210_CODEC_DEFAULT_ADDR, .bus_handle = s_i2c };
    const audio_codec_ctrl_if_t *mic_ctrl = audio_codec_new_i2c_ctrl(&mic_i2c);
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = mic_ctrl,
        .master_mode = false,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
        .mclk_src = ES7210_MCLK_FROM_PAD,
        .mclk_div = 256,
    };
    const audio_codec_if_t *mic_codec = es7210_codec_new(&es7210_cfg);
    ESP_RETURN_ON_FALSE(mic_codec, ESP_FAIL, TAG, "ES7210 not responding");

    esp_codec_dev_cfg_t out_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = spk_codec, .data_if = data_if };
    esp_codec_dev_cfg_t in_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = mic_codec, .data_if = data_if };
    *spk = esp_codec_dev_new(&out_cfg);
    *mic = esp_codec_dev_new(&in_cfg);
    return *spk && *mic ? ESP_OK : ESP_FAIL;
}

static void set_mic_gain(esp_codec_dev_handle_t mic, int db)
{
    /* ES7210 PGA steps are 3 dB; snap so the UI shows what's applied. */
    db = (db / 3) * 3;
    esp_codec_dev_set_in_gain(mic, (float)db);
}

static unsigned poll_buttons(void)
{
    return muse_gpio_button_poll(&s_talk) | muse_gpio_button_poll(&s_aux) << 2;
}

static void wait_buttons(int timeout_ms)
{
    muse_gpio_buttons_wait((muse_gpio_button_t *const[]){ &s_talk, &s_aux }, 2, timeout_ms);
}

/* Battery through the 2:1 divider on ADC1_CH0; CHG_STAT is active low.
 * Curve and divider follow rbouteiller/airplay-esp32's bring-up. */
static esp_err_t read_power(muse_power_t *out)
{
    static const struct {
        float v;
        int pct;
    } curve[] = {
        { 3.30f, 0 }, { 3.50f, 10 }, { 3.60f, 20 }, { 3.68f, 35 }, { 3.74f, 50 },
        { 3.82f, 65 }, { 3.92f, 80 }, { 4.02f, 90 }, { 4.12f, 98 }, { 4.20f, 100 },
    };
    enum { N = sizeof(curve) / sizeof(curve[0]) };
    int pct = 0, mv = 0;
    if (s_cali_ok) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, BATT_ADC, &raw) == ESP_OK &&
            adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) {
            float v = (float)mv / 1000.0f * 2.0f;
            mv = (int)(v * 1000.0f);
            if (v <= curve[0].v) {
                pct = curve[0].pct;
            } else if (v >= curve[N - 1].v) {
                pct = curve[N - 1].pct;
            } else {
                for (int i = 1; i < N; i++) {
                    if (v < curve[i].v) {
                        float frac = (v - curve[i - 1].v) / (curve[i].v - curve[i - 1].v);
                        pct = curve[i - 1].pct + (int)(frac * (curve[i].pct - curve[i - 1].pct) + 0.5f);
                        break;
                    }
                }
            }
        }
    }
    bool charging = gpio_get_level(CHG_GPIO) == 0;
    out->charging = charging && pct < 100;
    out->usb = charging || usb_serial_jtag_is_connected();
    out->battery_pct = pct;
    out->battery_mv = mv;
    return ESP_OK;
}

/* Opening the latch cuts power once PWR is up; deep sleep covers USB power,
 * where the rail stays up. Hold the pin low so a brownout can't re-latch. */
static esp_err_t power_off(void)
{
    set_brightness(0);
    esp_lcd_panel_disp_on_off(s_panel, false);
    gpio_set_level(BAT_EN_GPIO, 0);
    gpio_hold_en(BAT_EN_GPIO);
    gpio_deep_sleep_hold_en();
    while (gpio_get_level(AUX_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    esp_deep_sleep_start();
    return ESP_FAIL;
}

static const muse_board_t s_board = {
    .name = "Waveshare ESP32-S3-Touch-LCD-1.54",
    .width = LCD_RES,
    .height = LCD_RES,
    .round = false,
    .touch = true,
    .diagonal_in = 1.54f,
    .talk_button = "pwr",
    .aux_button = "boot",
    /* Buttons sit on the left edge: PWR (talk) above BOOT (aux).
     * Positions are approximate; adjust against the hardware. */
    .talk_hint = { LV_ALIGN_LEFT_MID, 14, -30 },
    .aux_hint = { LV_ALIGN_LEFT_MID, 14, 30 },
    .frame_ms = 40,
    .init = init,
    .display_start = display_start,
    .display_lock = display_lock,
    .display_unlock = esp_lv_adapter_unlock,
    .set_brightness = set_brightness,
    .panel_sleep = panel_sleep,
    .display_pause = display_pause,
    .audio_init = audio_init,
    .mic_slot = -1,             /* mix the dual mics */
    .set_mic_gain = set_mic_gain,
    .poll_buttons = poll_buttons,
    .wait_buttons = wait_buttons,
    .read_power = read_power,
    .power_off = power_off,
};

/* Home Link's app_main starts Muse with this board (main/main.c). */
const muse_board_t *muse_board_get(void)
{
    return &s_board;
}
