/*
 * WS2812 RGB LED 驱动
 * 使用 ESP-IDF 5.x 新版 RMT TX API（driver/rmt_tx.h），替代已废弃的 driver/rmt.h
 *
 * 关键优化：
 *   - 静态 rmt_symbol_word_t 缓冲区，消除旧版每帧 malloc/free 带来的堆碎片
 *   - 使用 rmt_new_tx_channel / rmt_new_bytes_encoder 标准编码器流程
 *   - WS2812 时序：T0H=350ns, T1H=900ns @ 10 MHz RMT 时钟 (100ns/tick)
 *       T0H =  4 ticks (400ns), T0L =  8 ticks (800ns)
 *       T1H =  8 ticks (800ns), T1L =  4 ticks (400ns)
 *       Reset >= 50us → 500 ticks (用 RMT 结束符 + 足够的空闲时间满足)
 */

#include "ws2812.h"
#include "boards/bread-compact-wifi/config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

// 使用新版 RMT TX API
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#define TAG "WS2812"

// -------------------------------------------------------
// RMT 时序常数（10 MHz 时钟，1 tick = 100 ns）
// WS2812B 数据手册：T0H 400ns, T0L 800ns, T1H 800ns, T1L 400ns
// -------------------------------------------------------
#define WS2812_RMT_CLK_RESOLUTION_HZ  (10 * 1000 * 1000)  // 10 MHz
#define WS2812_T0H_TICKS   4   // 400 ns
#define WS2812_T0L_TICKS   8   // 800 ns
#define WS2812_T1H_TICKS   8   // 800 ns
#define WS2812_T1L_TICKS   4   // 400 ns
#define WS2812_RESET_US    80  // >50µs，留裕量

bool WS2812::initialized = false;
int  WS2812::led_count   = 0;

// -------------------------------------------------------
// 模块级静态资源
// -------------------------------------------------------

// LED 颜色缓冲区（GRB 顺序已在 SetPixel 中转换）
static led_color_t* s_led_buffer = nullptr;

// RMT 句柄
static rmt_channel_handle_t s_rmt_channel = nullptr;
static rmt_encoder_handle_t s_bytes_encoder = nullptr;

// 静态 RMT 符号缓冲区：每颗 LED 24 bit，每 bit 1 个 symbol
// 最大支持 WS2812_LED_COUNT（8）颗 LED
#define WS2812_MAX_LEDS   WS2812_LED_COUNT
static rmt_symbol_word_t s_rmt_symbols[WS2812_MAX_LEDS * 24];

// -------------------------------------------------------
// HSV 转 RGB
// -------------------------------------------------------
static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v,
                        uint8_t* r, uint8_t* g, uint8_t* b) {
    uint8_t region    = h / 43;
    uint8_t remainder = (uint8_t)((h % 43) * 6);
    uint8_t p = (uint8_t)((v * (255 - s)) / 255);
    uint8_t q = (uint8_t)((v * (255 - (s * remainder) / 255)) / 255);
    uint8_t t = (uint8_t)((v * (255 - (s * (255 - remainder)) / 255)) / 255);

    switch (region) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

// -------------------------------------------------------
// 初始化 WS2812 LED
// -------------------------------------------------------
bool WS2812::Init(gpio_num_t pin, int count) {
    if (initialized) {
        return true;
    }

    if (count > WS2812_MAX_LEDS) {
        ESP_LOGE(TAG, "count %d exceeds WS2812_MAX_LEDS %d", count, WS2812_MAX_LEDS);
        return false;
    }

    led_count = count;

    // 分配 LED 颜色缓冲区（仅在初始化时 malloc 一次）
    s_led_buffer = (led_color_t*)malloc(sizeof(led_color_t) * count);
    if (s_led_buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate LED buffer");
        return false;
    }
    memset(s_led_buffer, 0, sizeof(led_color_t) * count);

    // ---- 配置 RMT TX 通道 ----
    rmt_tx_channel_config_t chan_config = {
        .gpio_num          = pin,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = WS2812_RMT_CLK_RESOLUTION_HZ,
        .mem_block_symbols = 64,   // 最小块，8 颗 LED × 24 symbols = 192，分批发送
        .trans_queue_depth = 4,
        .flags = {
            .invert_out        = 0,
            .with_dma          = 0,
            .io_loop_back      = 0,
            .io_od_mode        = 0,
        },
    };
    esp_err_t ret = rmt_new_tx_channel(&chan_config, &s_rmt_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(ret));
        free(s_led_buffer);
        s_led_buffer = nullptr;
        return false;
    }

    // ---- 配置 bytes encoder ----
    // WS2812 每 bit 使用 RMT symbol 编码，需要自定义 bit0/bit1 时序
    rmt_bytes_encoder_config_t enc_config = {
        .bit0 = {
            .duration0 = WS2812_T0H_TICKS,
            .level0    = 1,
            .duration1 = WS2812_T0L_TICKS,
            .level1    = 0,
        },
        .bit1 = {
            .duration0 = WS2812_T1H_TICKS,
            .level0    = 1,
            .duration1 = WS2812_T1L_TICKS,
            .level1    = 0,
        },
        .flags = {
            .msb_first = 1,  // WS2812 高位先发
        },
    };
    ret = rmt_new_bytes_encoder(&enc_config, &s_bytes_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_bytes_encoder failed: %s", esp_err_to_name(ret));
        rmt_del_channel(s_rmt_channel);
        s_rmt_channel = nullptr;
        free(s_led_buffer);
        s_led_buffer = nullptr;
        return false;
    }

    // 使能 TX 通道
    ret = rmt_enable(s_rmt_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(ret));
        rmt_del_encoder(s_bytes_encoder);
        rmt_del_channel(s_rmt_channel);
        s_bytes_encoder = nullptr;
        s_rmt_channel   = nullptr;
        free(s_led_buffer);
        s_led_buffer = nullptr;
        return false;
    }

    initialized = true;
    ESP_LOGI(TAG, "WS2812 initialized: GPIO %d, %d LEDs, RMT 10 MHz", pin, count);

    Clear();
    Show();

    return true;
}

// -------------------------------------------------------
// 设置单个 LED 颜色（内部转 GRB 顺序）
// -------------------------------------------------------
void WS2812::SetPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (!initialized || index >= (uint16_t)led_count || s_led_buffer == nullptr) {
        return;
    }
    // WS2812 物理顺序：G R B
    s_led_buffer[index].r = g;
    s_led_buffer[index].g = r;
    s_led_buffer[index].b = b;
}

void WS2812::SetPixel(uint16_t index, led_color_t color) {
    SetPixel(index, color.r, color.g, color.b);
}

// -------------------------------------------------------
// 填充所有 LED
// -------------------------------------------------------
void WS2812::Fill(uint8_t r, uint8_t g, uint8_t b) {
    if (!initialized || s_led_buffer == nullptr) return;
    for (int i = 0; i < led_count; i++) {
        // GRB 顺序
        s_led_buffer[i].r = g;
        s_led_buffer[i].g = r;
        s_led_buffer[i].b = b;
    }
}

void WS2812::Fill(led_color_t color) {
    Fill(color.r, color.g, color.b);
}

void WS2812::Clear() {
    Fill(0, 0, 0);
}

// -------------------------------------------------------
// 显示/刷新：将 s_led_buffer 通过 RMT 发送到 LED 链
// 使用静态 s_rmt_symbols 缓冲区，无 malloc
// -------------------------------------------------------
void WS2812::Show() {
    if (!initialized || s_rmt_channel == nullptr || s_bytes_encoder == nullptr ||
        s_led_buffer == nullptr) {
        return;
    }

    rmt_transmit_config_t tx_config = {
        .loop_count   = 0,              // 不循环
        .flags = {
            .eot_level = 0,             // 帧结束后输出低电平（WS2812 RESET）
        },
    };

    // 等待上一帧完成，再发送新帧
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(s_rmt_channel, pdMS_TO_TICKS(10)));

    // 发送 GRB 数据（bytes encoder 负责 bit 展开）
    esp_err_t ret = rmt_transmit(s_rmt_channel, s_bytes_encoder,
                                  s_led_buffer,
                                  (size_t)led_count * sizeof(led_color_t),
                                  &tx_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(ret));
    }

    // 等待本帧完成后再加 reset delay（≥50µs）
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(s_rmt_channel, pdMS_TO_TICKS(10)));
    esp_rom_delay_us(WS2812_RESET_US);
}

// -------------------------------------------------------
// 彩虹效果（256 帧 × delay_ms，建议在独立任务中调用）
// -------------------------------------------------------
void WS2812::Rainbow(uint16_t delay_ms) {
    if (!initialized) return;

    for (int j = 0; j < 256; j++) {
        for (int i = 0; i < led_count; i++) {
            uint16_t hue = (uint16_t)((i * 256 / led_count + j) % 256);
            uint8_t r, g, b;
            hsv_to_rgb(hue, 255, 200, &r, &g, &b);
            SetPixel((uint16_t)i, r, g, b);
        }
        Show();
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// -------------------------------------------------------
// 跑马灯效果
// -------------------------------------------------------
void WS2812::KnightRider(uint16_t delay_ms) {
    if (!initialized) return;

    led_color_t red = LED_COLOR_RED;
    int position  = 0;
    int direction = 1;

    for (int cycle = 0; cycle < 10; cycle++) {
        Clear();
        SetPixel((uint16_t)position, red);
        Show();
        vTaskDelay(pdMS_TO_TICKS(delay_ms));

        position += direction;
        if (position >= led_count - 1 || position <= 0) {
            direction *= -1;
        }
    }
    Clear();
    Show();
}

// -------------------------------------------------------
// 呼吸灯效果（2 个周期）
// -------------------------------------------------------
void WS2812::Breathing(uint16_t delay_ms) {
    if (!initialized) return;

    led_color_t color = LED_COLOR_BLUE;

    for (int cycle = 0; cycle < 2; cycle++) {
        for (int brightness = 0; brightness <= 255; brightness += 5) {
            Fill((uint8_t)((color.r * brightness) / 255),
                 (uint8_t)((color.g * brightness) / 255),
                 (uint8_t)((color.b * brightness) / 255));
            Show();
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
        for (int brightness = 255; brightness >= 0; brightness -= 5) {
            Fill((uint8_t)((color.r * brightness) / 255),
                 (uint8_t)((color.g * brightness) / 255),
                 (uint8_t)((color.b * brightness) / 255));
            Show();
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }
}
