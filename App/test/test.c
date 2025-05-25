// app
#include "robot_def.h"
// #include "robot_cmd.h"
// module
#include "remote_control.h"
#include "master_process.h"
#include "message_center.h"
#include "general_def.h"
#include "LKmotor.h"
#include "servo_motor.h"
#include "dji_motor.h"

// bsp
#include "encoder.h"
#include "bsp_dwt.h"
#include "bsp_log.h"
#include "DRmotor.h"
#include "bsp_spi.h"

// SPIInstance *spi;
void selfTestInit()
{
    // SPI_Init_Config_s conf = {
    //     .spi_handle = &hspi3,
    //     .GPIOx = GPIOC,
    //     .spi_work_mode = 
    // }
    // spi = SPIRegister(&conf);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_SET); // 使能测试板LED
    //初始化设置
    Motor_Init_Config_s config = {
    .motor_type = M3508,
    .can_init_config = {
    .can_handle = &hfdcan1,
    .tx_id = 1
            },
    .controller_setting_init_config = {
                .angle_feedback_source = MOTOR_FEED, 
                .outer_loop_type = SPEED_LOOP,
                .close_loop_type = SPEED_LOOP | ANGLE_LOOP, 
                .speed_feedback_source = MOTOR_FEED, 
                .motor_reverse_flag = MOTOR_DIRECTION_NORMAL
            },
    .controller_param_init_config = {
                .angle_PID = {
                    .Improve = 0, 
                    .Kp = 1, 
                    .Ki = 0, 
                    .Kd = 0, 
                    .DeadBand = 0, 
                    .MaxOut = 4000}, 
                .speed_PID = {
                    .Improve = 0, 
                    .Kp = 1, 
                    .Ki = 0, 
                    .Kd = 0, 
                    .DeadBand = 0, 
                    .MaxOut = 4000
                }
            }
    };
    //注册电机并保存实例指针
    DJIMotorInstance *djimotor = DJIMotorInit(&config);
    DJIMotorSetRef(djimotor, 50);
    DJIMotorEnable(djimotor);
}

void selfTestTask()
{
}