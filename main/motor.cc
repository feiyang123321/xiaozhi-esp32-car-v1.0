#include "motor.h"
#include <driver/gpio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "boards/bread-compact-wifi/config.h"
#include "tof0200c.h"
#include "board.h"

#define TAG "SMART_CAR"

// 全局 ToF 传感器实例
static TOF0200C tof_sensor;

bool SmartCar::initialized         = false;
car_mode_t SmartCar::current_mode  = CAR_MODE_MANUAL;
TaskHandle_t SmartCar::control_task_handle = nullptr;
TaskHandle_t SmartCar::motor_task_handle   = nullptr;
QueueHandle_t SmartCar::command_queue      = nullptr;

// 模拟 ToF 传感器读数 (实际需要接真实传感器)
static int16_t simulated_tof_distance = 1000;

// 设置模拟距离 (用于测试)
void SmartCar_SetTestDistance(int16_t mm) {
    simulated_tof_distance = mm;
}

// 获取 ToF 距离 (mm)
static int16_t get_tof_distance() {
    return tof_sensor.ReadDistance();
}

// 初始化GPIO
bool SmartCar::Init() {
    if (initialized) {
        return true;
    }
    
    gpio_config_t io_conf = {};
    
    // 配置左电机引脚
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << MOTOR_LEFT_IN1) | (1ULL << MOTOR_LEFT_IN2);
    gpio_config(&io_conf);
    
    // 配置右电机引脚
    io_conf.pin_bit_mask = (1ULL << MOTOR_RIGHT_IN3) | (1ULL << MOTOR_RIGHT_IN4);
    gpio_config(&io_conf);
    
    // 创建命令队列（容量 16，motor_task 消费，HTTP/MCP 投递）
    command_queue = xQueueCreate(16, sizeof(motor_cmd_msg_t));
    if (command_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return false;
    }
    
    // 初始化 ToF 传感器：复用 Board 提供的共享 I2C 总线，避免重复创建冲突
    // Board::GetI2cBus() 由 CompactWifiBoard override，返回 OLED 共享的 display_i2c_bus_
    i2c_master_bus_handle_t shared_i2c = Board::GetInstance().GetI2cBus();
    if (shared_i2c != nullptr && tof_sensor.Init(shared_i2c)) {
        ESP_LOGI(TAG, "ToF sensor initialized on shared I2C bus");
    } else {
        // Board 不支持共享 I2C（GetI2cBus 返回 nullptr），ToF 传感器不可用
        // 小车仍可在手动模式下正常运行
        ESP_LOGW(TAG, "Shared I2C bus unavailable, ToF sensor disabled (manual mode only)");
    }
    
    // 启动专用电机任务（栈 3072，优先级 6，高于 httpd 默认优先级 5）
    xTaskCreate([](void*) {
        ESP_LOGI(TAG, "Motor task started");
        motor_cmd_msg_t msg;
        while (true) {
            // 阻塞等待队列命令，超时 500ms 后自动停止（看门狗式自停）
            if (xQueueReceive(SmartCar::command_queue, &msg, pdMS_TO_TICKS(500)) == pdTRUE) {
                SmartCar::Execute(msg.action);
                if (msg.duration_ms > 0) {
                    vTaskDelay(pdMS_TO_TICKS(msg.duration_ms));
                    SmartCar::Stop();
                }
            }
            // 若队列超时且当前为手动模式，不自动停止（保持最后状态）
        }
    }, "motor_task", 3072, nullptr, 6, &motor_task_handle);
    
    // 初始停止
    Stop();
    
    initialized = true;
    ESP_LOGI(TAG, "Smart Car initialized");
    ESP_LOGI(TAG, "Motor pins: L(%d,%d) R(%d,%d)", MOTOR_LEFT_IN1, MOTOR_LEFT_IN2, MOTOR_RIGHT_IN3, MOTOR_RIGHT_IN4);
    
    return true;
}

// 停止
void SmartCar::Stop() {
    gpio_set_level(MOTOR_LEFT_IN1, 0);
    gpio_set_level(MOTOR_LEFT_IN2, 0);
    gpio_set_level(MOTOR_RIGHT_IN3, 0);
    gpio_set_level(MOTOR_RIGHT_IN4, 0);
    ESP_LOGD(TAG, "Stop");
}

// 前进
void SmartCar::Forward() {
    // 左电机正转
    gpio_set_level(MOTOR_LEFT_IN1, 1);
    gpio_set_level(MOTOR_LEFT_IN2, 0);
    // 右电机正转
    gpio_set_level(MOTOR_RIGHT_IN3, 1);
    gpio_set_level(MOTOR_RIGHT_IN4, 0);
    ESP_LOGI(TAG, "Forward");
}

// 后退
void SmartCar::Backward() {
    // 左电机反转
    gpio_set_level(MOTOR_LEFT_IN1, 0);
    gpio_set_level(MOTOR_LEFT_IN2, 1);
    // 右电机反转
    gpio_set_level(MOTOR_RIGHT_IN3, 0);
    gpio_set_level(MOTOR_RIGHT_IN4, 1);
    ESP_LOGI(TAG, "Backward");
}

// 左转
void SmartCar::TurnLeft() {
    // 左电机停止，右电机正转
    gpio_set_level(MOTOR_LEFT_IN1, 0);
    gpio_set_level(MOTOR_LEFT_IN2, 0);
    gpio_set_level(MOTOR_RIGHT_IN3, 1);
    gpio_set_level(MOTOR_RIGHT_IN4, 0);
    ESP_LOGI(TAG, "Turn Left");
}

// 右转
void SmartCar::TurnRight() {
    // 左电机正转，右电机停止
    gpio_set_level(MOTOR_LEFT_IN1, 1);
    gpio_set_level(MOTOR_LEFT_IN2, 0);
    gpio_set_level(MOTOR_RIGHT_IN3, 0);
    gpio_set_level(MOTOR_RIGHT_IN4, 0);
    ESP_LOGI(TAG, "Turn Right");
}

// 执行指定动作
void SmartCar::Execute(motor_action_t action) {
    switch (action) {
        case MOTOR_STOP:     Stop();      break;
        case MOTOR_FORWARD:  Forward();   break;
        case MOTOR_BACKWARD: Backward();  break;
        case MOTOR_TURN_LEFT:  TurnLeft();  break;
        case MOTOR_TURN_RIGHT: TurnRight(); break;
    }
}

// 执行动作并延时（同步阻塞，仅供 motor_task 内部使用）
void SmartCar::ExecuteWithDelay(motor_action_t action, uint32_t ms) {
    Execute(action);
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
        Stop();
    }
}

// -------------------------------------------------------
// 异步接口实现
// -------------------------------------------------------

// 将一条命令投入队列，调用方立即返回（非阻塞）
// 若队列已满（超过 16 条），丢弃并记录警告
bool SmartCar::PostCommand(motor_action_t action, uint32_t duration_ms) {
    if (command_queue == nullptr) {
        ESP_LOGW(TAG, "PostCommand: queue not ready");
        return false;
    }
    motor_cmd_msg_t msg = { action, duration_ms };
    if (xQueueSend(command_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "PostCommand: queue full, command dropped");
        return false;
    }
    return true;
}

// 异步舞蹈：8 步全部投队列，motor_task 顺序执行
void SmartCar::PostMotorDance() {
    ESP_LOGI(TAG, "Posting 8-step dance to motor_task");
    PostCommand(MOTOR_FORWARD,    500);
    PostCommand(MOTOR_TURN_LEFT,  400);
    PostCommand(MOTOR_TURN_RIGHT, 400);
    PostCommand(MOTOR_BACKWARD,   500);
    PostCommand(MOTOR_FORWARD,    500);
    PostCommand(MOTOR_TURN_LEFT,  400);
    PostCommand(MOTOR_TURN_RIGHT, 400);
    PostCommand(MOTOR_STOP,       0);
}

// 异步 motor_cmd 执行
void SmartCar::PostMotorEmotion(motor_cmd_t cmd) {
    switch (cmd) {
        case MOTOR_CMD_STOP:     PostCommand(MOTOR_STOP, 0);                   break;
        case MOTOR_CMD_FORWARD:  PostCommand(MOTOR_FORWARD,  1500);            break;
        case MOTOR_CMD_BACKWARD: PostCommand(MOTOR_BACKWARD, 1500);            break;
        case MOTOR_CMD_LEFT:     PostCommand(MOTOR_TURN_LEFT,  800);           break;
        case MOTOR_CMD_RIGHT:    PostCommand(MOTOR_TURN_RIGHT, 800);           break;
        case MOTOR_CMD_DANCE:    PostMotorDance();                              break;
        case MOTOR_CMD_EXCITED:
            PostCommand(MOTOR_FORWARD,    400);
            PostCommand(MOTOR_TURN_LEFT,  300);
            PostCommand(MOTOR_TURN_RIGHT, 300);
            PostCommand(MOTOR_STOP,       0);
            break;
    }
}

// 异步表情名称到动作映射
void SmartCar::PostMotorEmotion(const char* emotion) {
    if (emotion == nullptr) return;
    ESP_LOGI(TAG, "PostMotorEmotion: %s", emotion);

    if (strcmp(emotion, "happy") == 0 || strcmp(emotion, "smile") == 0 ||
        strcmp(emotion, "laugh") == 0 || strcmp(emotion, "grin") == 0) {
        PostMotorEmotion(MOTOR_CMD_FORWARD);
    } else if (strcmp(emotion, "sad") == 0 || strcmp(emotion, "cry") == 0 ||
               strcmp(emotion, "frown") == 0 || strcmp(emotion, "tear") == 0) {
        PostMotorEmotion(MOTOR_CMD_BACKWARD);
    } else if (strcmp(emotion, "angry") == 0 || strcmp(emotion, "rage") == 0) {
        PostCommand(MOTOR_BACKWARD, 300);
        PostCommand(MOTOR_FORWARD,  400);
        PostCommand(MOTOR_STOP, 0);
    } else if (strcmp(emotion, "surprised") == 0 || strcmp(emotion, "shock") == 0 ||
               strcmp(emotion, "astonished") == 0) {
        PostCommand(MOTOR_BACKWARD, 200);
        PostCommand(MOTOR_FORWARD,  300);
        PostCommand(MOTOR_STOP, 0);
    } else if (strcmp(emotion, "excited") == 0 || strcmp(emotion, "happy2") == 0) {
        PostMotorEmotion(MOTOR_CMD_EXCITED);
    } else if (strcmp(emotion, "loving") == 0 || strcmp(emotion, "love") == 0 ||
               strcmp(emotion, "heart") == 0 || strcmp(emotion, "affection") == 0) {
        PostCommand(MOTOR_FORWARD,   600);
        PostCommand(MOTOR_TURN_LEFT, 500);
        PostCommand(MOTOR_STOP, 0);
    } else if (strcmp(emotion, "confused") == 0 || strcmp(emotion, "confusion") == 0 ||
               strcmp(emotion, "puzzled") == 0) {
        PostCommand(MOTOR_TURN_LEFT,  300);
        PostCommand(MOTOR_TURN_RIGHT, 300);
        PostCommand(MOTOR_TURN_LEFT,  300);
        PostCommand(MOTOR_TURN_RIGHT, 300);
        PostCommand(MOTOR_STOP, 0);
    } else if (strcmp(emotion, "neutral") == 0 || strcmp(emotion, "idle") == 0 ||
               strcmp(emotion, "sleepy") == 0) {
        PostMotorEmotion(MOTOR_CMD_STOP);
    } else if (strcmp(emotion, "dancing") == 0 || strcmp(emotion, "dance") == 0 ||
               strcmp(emotion, "party") == 0) {
        PostMotorDance();
    } else {
        ESP_LOGI(TAG, "Unknown emotion '%s', stopping", emotion);
        PostCommand(MOTOR_STOP, 0);
    }
}

// -------------------------------------------------------
// 同步版本保留（供 motor_task 内部或确有需要的场合调用）
// -------------------------------------------------------

void SmartCar::TriggerMotorEmotion(motor_cmd_t cmd) {
    // 改为异步投队列，不再在调用任务中阻塞
    PostMotorEmotion(cmd);
}

void SmartCar::TriggerMotorEmotion(const char* emotion) {
    PostMotorEmotion(emotion);
}

void SmartCar::MotorDance() {
    PostMotorDance();
}

// -------------------------------------------------------
// 智能控制任务（避障/跟随/边缘检测）
// -------------------------------------------------------

void SmartCar::SetMode(car_mode_t mode) {
    current_mode = mode;
    ESP_LOGI(TAG, "Mode changed to: %d", mode);
    
    if (mode == CAR_MODE_MANUAL) {
        StopControlTask();
    } else {
        StartControlTask();
    }
}

car_mode_t SmartCar::GetMode() {
    return current_mode;
}

static void car_control_task(void* param) {
    ESP_LOGI(TAG, "Car control task started");
    
    while (true) {
        car_mode_t mode = SmartCar::GetMode();
        int16_t distance = get_tof_distance();
        
        switch (mode) {
            case CAR_MODE_OBSTACLE_AVOID: {
                if (distance > 0 && distance < TOF_OBSTACLE_THRESHOLD_MM) {
                    ESP_LOGI(TAG, "Obstacle detected at %dmm, avoiding...", distance);
                    SmartCar::Backward();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    SmartCar::TurnRight();
                    vTaskDelay(pdMS_TO_TICKS(300));
                    SmartCar::Stop();
                } else {
                    SmartCar::Forward();
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            }
            
            case CAR_MODE_FOLLOW: {
                if (distance > 0 && distance < TOF_OBSTACLE_THRESHOLD_MM) {
                    SmartCar::Backward();
                    ESP_LOGI(TAG, "Following: too close (%dmm)", distance);
                } else if (distance > TOF_OBSTACLE_THRESHOLD_MM && distance < 500) {
                    SmartCar::Forward();
                    ESP_LOGI(TAG, "Following: moving forward (%dmm)", distance);
                } else {
                    SmartCar::Stop();
                    ESP_LOGI(TAG, "Following: target out of range (%dmm)", distance);
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            }
            
            case CAR_MODE_EDGE_DETECT: {
                if (distance > 0 && distance < 80) {
                    ESP_LOGI(TAG, "Edge detected at %dmm, retreating!", distance);
                    SmartCar::Stop();
                    vTaskDelay(pdMS_TO_TICKS(100));
                    SmartCar::Backward();
                    vTaskDelay(pdMS_TO_TICKS(300));
                    SmartCar::TurnRight();
                    vTaskDelay(pdMS_TO_TICKS(200));
                    SmartCar::Stop();
                } else {
                    SmartCar::Forward();
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            }
            
            default:
                vTaskDelay(pdMS_TO_TICKS(500));
                break;
        }
    }
}

void SmartCar::StartControlTask() {
    if (control_task_handle != nullptr) {
        return;
    }
    xTaskCreate(car_control_task, "car_ctrl", 4096, nullptr, 5, &control_task_handle);
    ESP_LOGI(TAG, "Control task started");
}

void SmartCar::StopControlTask() {
    if (control_task_handle != nullptr) {
        vTaskDelete(control_task_handle);
        control_task_handle = nullptr;
        ESP_LOGI(TAG, "Control task stopped");
    }
    Stop();
}

