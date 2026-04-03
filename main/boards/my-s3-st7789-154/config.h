
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// INMP441 I2S 麦克风
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6

// MAX98357A I2S 功放
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

// SSD1306 0.96" 128x64 OLED (I2C接口)
#define DISPLAY_SDA_PIN GPIO_NUM_41   // SDA
#define DISPLAY_SCL_PIN GPIO_NUM_42   // SCL
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

// ==================== 电机驱动 (L298N) ====================
#define MOTOR_LEFT_IN1  GPIO_NUM_12  // 左电机方向A
#define MOTOR_LEFT_IN2  GPIO_NUM_13  // 左电机方向B
#define MOTOR_RIGHT_IN3 GPIO_NUM_14  // 右电机方向A
#define MOTOR_RIGHT_IN4 GPIO_NUM_21  // 右电机方向B

// ==================== TOF0200C 红外测距传感器 ====================
// 使用 I2C 接口 (复用 OLED 的 I2C 总线)
#define TOF_I2C_PORT    I2C_NUM_0
#define TOF_SDA_PIN     GPIO_NUM_41   // 复用 OLED SDA
#define TOF_SCL_PIN     GPIO_NUM_42   // 复用 OLED SCL
#define TOF_XSHUT_PIN   GPIO_NUM_4   // 复位引脚 (可选)

// 避障参数
#define TOF_OBSTACLE_THRESHOLD_MM   150   // 障碍物阈值 (毫米)
#define TOF_FOLLOW_DISTANCE_MM      300   // 跟随距离 (毫米)
#define TOF_EDGE_THRESHOLD_MM       100   // 边缘检测阈值 (毫米)

// ==================== WS2812 RGB LED (8颗) ====================
#define WS2812_LED_PIN   GPIO_NUM_38   // 数据引脚
#define WS2812_LED_COUNT 8             // LED 数量

#endif // _BOARD_CONFIG_H_
