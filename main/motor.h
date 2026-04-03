#ifndef _MOTOR_H_
#define _MOTOR_H_

#include <driver/gpio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// 电机动作枚举
typedef enum {
    MOTOR_STOP = 0,
    MOTOR_FORWARD,    // 前进
    MOTOR_BACKWARD,   // 后退
    MOTOR_TURN_LEFT,  // 左转
    MOTOR_TURN_RIGHT, // 右转
} motor_action_t;

// 电机命令枚举 (用于表情动作映射)
typedef enum {
    MOTOR_CMD_STOP = 0,      // 停止
    MOTOR_CMD_FORWARD = 1,   // 前进
    MOTOR_CMD_BACKWARD = 2,  // 后退
    MOTOR_CMD_LEFT = 3,      // 左转
    MOTOR_CMD_RIGHT = 4,     // 右转
    MOTOR_CMD_DANCE = 5,     // 跳舞
    MOTOR_CMD_EXCITED = 6,   // 兴奋表情动作
} motor_cmd_t;

// 智能车模式
typedef enum {
    CAR_MODE_MANUAL = 0,     // 手动模式 (语音/按键控制)
    CAR_MODE_OBSTACLE_AVOID, // 避障模式
    CAR_MODE_FOLLOW,         // 跟随模式
    CAR_MODE_EDGE_DETECT,   // 边缘检测/巡检模式
} car_mode_t;

// -------------------------------------------------------
// 异步电机命令：发送到 motor_task 执行，调用方立即返回
// -------------------------------------------------------
typedef struct {
    motor_action_t action;   // 要执行的动作
    uint32_t       duration_ms; // 持续时间 (ms)；0 = 一直持续直到下一条命令
} motor_cmd_msg_t;

// 智能小车控制类
class SmartCar {
private:
    static bool initialized;
    static car_mode_t current_mode;
    static TaskHandle_t control_task_handle;
    static TaskHandle_t motor_task_handle;   // 专用异步电机任务
    static QueueHandle_t command_queue;      // 供 motor_task 消费的动作队列
    
public:
    // 初始化智能小车 (包含电机 + 传感器)
    static bool Init();
    
    // 停止
    static void Stop();
    
    // 前进
    static void Forward();
    
    // 后退
    static void Backward();
    
    // 左转
    static void TurnLeft();
    
    // 右转
    static void TurnRight();
    
    // 执行指定动作
    static void Execute(motor_action_t action);
    
    // 执行动作并延时（同步阻塞，仅在 motor_task 内部使用）
    static void ExecuteWithDelay(motor_action_t action, uint32_t ms);

    // -------------------------------------------------------
    // 异步接口：将命令投入队列，调用方立即返回（非阻塞）
    // 适合在 HTTP handler / MCP 回调 / 音频任务中安全调用
    // -------------------------------------------------------
    static bool PostCommand(motor_action_t action, uint32_t duration_ms = 0);

    // 异步触发表情动作（将动作序列拆解为多条 PostCommand 消息）
    static void PostMotorEmotion(const char* emotion);
    static void PostMotorEmotion(motor_cmd_t cmd);

    // 异步舞蹈（将 8 步舞蹈序列全部投入队列）
    static void PostMotorDance();
    
    // 设置智能车模式
    static void SetMode(car_mode_t mode);
    
    // 获取当前模式
    static car_mode_t GetMode();
    
    // 开始智能控制任务（避障/跟随等自主模式）
    static void StartControlTask();
    
    // 停止智能控制任务
    static void StopControlTask();
    
    // 触发表情动作（同步版，保留以兼容旧调用，但内部改为 Post）
    static void TriggerMotorEmotion(const char* emotion);
    static void TriggerMotorEmotion(motor_cmd_t cmd);
    
    // 执行完整8步舞蹈序列（同步版，保留兼容）
    static void MotorDance();
};

// 测试设置 (非成员函数，保持兼容性)
void SmartCar_SetTestDistance(int16_t mm);

// 兼容旧的 MotorControl
class MotorControl {
public:
    static void Init() { SmartCar::Init(); }
    static void Stop() { SmartCar::Stop(); }
    static void Forward() { SmartCar::Forward(); }
    static void Backward() { SmartCar::Backward(); }
    static void TurnLeft() { SmartCar::TurnLeft(); }
    static void TurnRight() { SmartCar::TurnRight(); }
    static void Execute(motor_action_t action) { SmartCar::Execute(action); }
    static void ExecuteWithDelay(motor_action_t action, uint32_t ms) { SmartCar::ExecuteWithDelay(action, ms); }
    static void SetMode(car_mode_t mode) { SmartCar::SetMode(mode); }
};

#endif // _MOTOR_H_

