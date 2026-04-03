# xiaozhi-esp32-car v1.0

**版本发布日期**: 2026-04-03  
**基于**: xiaozhi-esp32-2.2.4

## 版本说明

这是基于 xiaozhi-esp32-2.2.4 修改的智能小车项目 v1.0 版本。

## 硬件配置

### 主板
- **芯片**: ESP32-S3
- **开发板**: my-s3-st7789-154
- **Flash**: 16MB

### 音频模块
- **麦克风**: INMP441 (I2S)
  - SCK (BCLK): GPIO 5
  - WS (LRCK): GPIO 4
  - DIN (SD): GPIO 6
- **扬声器功放**: MAX98357A (I2S)
  - DOUT: GPIO 7
  - BCLK: GPIO 15
  - LRCK: GPIO 16

### 显示模块
- **屏幕**: SSD1306 0.96" 128x64 OLED (I2C)
  - SDA: GPIO 41
  - SCL: GPIO 42

### 电机驱动
- **驱动芯片**: L298N
- **左电机**: 
  - IN1: GPIO 12
  - IN2: GPIO 13
- **右电机**:
  - IN3: GPIO 14
  - IN4: GPIO 21

### 传感器
- **TOF0200C 红外测距传感器**:
  - SDA: GPIO 41 (复用 OLED SDA)
  - SCL: GPIO 42 (复用 OLED SCL)
  - XSHUT: GPIO 4

### LED 灯带
- **型号**: WS2812 RGB
- **数量**: 8 颗
- **数据引脚**: GPIO 38

## 软件特性

### 核心功能
- ✅ WiFi 连接（自动重连）
- ✅ MQTT 连接（mqtt.xiaozhi.me）
- ✅ 唤醒词检测（"你好小智"）
- ✅ 语音识别（xiaozhi AI）
- ✅ Web 服务器控制（端口 80）
- ✅ OLED 显示
- ✅ 电机控制
- ✅ 避障功能
- ✅ WS2812 彩灯效果

### 音频配置
- **音频编码器**: NoAudioCodecSimplex（双 I2S 通道）
- **麦克风采样率**: 16000 Hz
- **扬声器采样率**: 24000 Hz
- **音频位深**: 24-bit

## 关键修改

### 1. 音频编码器初始化
在 `main/boards/my-s3-st7789-154/my_s3_st7789_154.cc` 中添加了音频编码器初始化：

```cpp
void InitializeAudioCodec() {
    audio_codec_ = new NoAudioCodecSimplex(
        AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
        AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
        AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
    ESP_LOGI(TAG, "Audio codec initialized (Simplex mode)");
}
```

### 2. 板型配置
在 `main/CMakeLists.txt` 中添加了自定义板型配置：
```cmake
set(BOARD_TYPE "my-s3-st7789-154")
```

### 3. 编译环境
- **ESP-IDF**: v5.5.3
- **工具链**: xtensa-esp-elf 14.2.0
- **Python**: 3.11
- **组件缓存**: C:\EcCache（避免 Windows MAX_PATH 限制）

## 编译和烧录

### 编译
```bash
cd D:\Desktop\xiaozhime\xiaozhi-esp32-car-v1.0
idf.py set-target esp32s3
idf.py build
```

### 烧录
```bash
idf.py -p COM3 flash -b 460800
```

### 串口监控
```bash
idf.py -p COM3 monitor
```

## 已知问题

### 1. 麦克风硬件故障
- **现象**: 麦克风数据持续为 0（`MIC RAW max=0`）
- **影响**: 唤醒词无法检测，语音识别无法触发，喇叭无声音输出
- **原因**: 麦克风硬件问题（待检查 VCC、GND、I2S 引脚连接）
- **状态**: ⚠️ 待解决

### 2. 系统稳定性
- ✅ 无 watchdog 错误
- ✅ 无 I2C 错误
- ✅ WiFi 自动重连正常
- ✅ MQTT 连接稳定
- ✅ Free SRAM: ~127KB

## 版本历史

### v1.0 (2026-04-03)
- 基于 xiaozhi-esp32-2.2.4
- 添加音频编码器初始化
- 添加自定义板型支持
- 添加电机驱动控制
- 添加 TOF 测距传感器
- 添加 WS2812 彩灯
- 编译烧录成功

## 许可证

遵循原项目 xiaozhi-esp32 的许可证。
