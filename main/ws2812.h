#ifndef _WS2812_H_
#define _WS2812_H_

#include <driver/gpio.h>
#include "esp_err.h"

// LED 颜色结构
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_color_t;

// 预设颜色
#define LED_COLOR_RED       {255, 0, 0}
#define LED_COLOR_GREEN     {0, 255, 0}
#define LED_COLOR_BLUE      {0, 0, 255}
#define LED_COLOR_YELLOW    {255, 255, 0}
#define LED_COLOR_CYAN      {0, 255, 255}
#define LED_COLOR_MAGENTA   {255, 0, 255}
#define LED_COLOR_WHITE     {255, 255, 255}
#define LED_COLOR_OFF       {0, 0, 0}

// WS2812 LED 控制类
// 使用 ESP-IDF 5.x 新版 RMT TX API（driver/rmt_tx.h）
// 静态 RMT 符号缓冲区，消除每帧 malloc
class WS2812 {
public:
    static bool initialized;
    static int led_count;
    
public:
    // 初始化 WS2812 LED
    static bool Init(gpio_num_t pin, int count);
    
    // 设置单个 LED 颜色
    static void SetPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);
    
    // 设置单个 LED 颜色 (led_color_t)
    static void SetPixel(uint16_t index, led_color_t color);
    
    // 填充所有 LED
    static void Fill(uint8_t r, uint8_t g, uint8_t b);
    
    // 填充所有 LED (led_color_t)
    static void Fill(led_color_t color);
    
    // 清除所有 LED
    static void Clear();
    
    // 显示/刷新
    static void Show();
    
    // 彩虹效果
    static void Rainbow(uint16_t delay_ms = 20);
    
    // 跑马灯效果
    static void KnightRider(uint16_t delay_ms = 100);
    
    // 呼吸灯效果
    static void Breathing(uint16_t delay_ms = 10);
};

#endif // _WS2812_H_

