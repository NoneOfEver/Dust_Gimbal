/* 注意该文件应只用于任务初始化,只能被robot.c包含*/
#pragma once

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

#include "robot.h"
#include "LKmotor.h"
#include "bsp_log.h"
#include "user_lib.h"
#include "DRmotor.h"

// TASK
#include "daemon.h"
#include "gimbal.h"
#include "motor_task.h"
#include "test.h"
#include "iwdg.h"


#ifdef ROBOT_TEST
void TestTask(void *argument)
{
    UNUSED(argument);
    static uint32_t feed_dwt_cntt,feed_dtt;
    DWT_GetDeltaT(&feed_dwt_cntt);

    while (1) {
        selfTestTask();
        MotorControlTask();

        feed_dtt = DWT_GetDeltaT(&feed_dwt_cntt);

        osDelay(1);
    }
}
#endif
#ifndef ROBOT_TEST
void _cmdTASK(void *argument)
{
    UNUSED(argument);
    osDelay(300);
    HAL_IWDG_Init(&hiwdg1);
    while (1) {
        RobotCMDTask();

        osDelay(1);
    }
}

void _airpumpTASK(void *argument)
{
    UNUSED(argument);
    osDelay(300);
    while (1) {
        AIRPUMPTask();
        osDelay(1);
    }
}

void _gimbalTASK(void *argument)
{
    UNUSED(argument);
    osDelay(300);
    while (1) {
        GIMBALTask();
        osDelay(1);
    }
}
void _chassisTASK(void *argument)
{
    UNUSED(argument);
    osDelay(1500);
    while (1) {
        ChassisTask();

        osDelay(1);
    }
}

void _armTASK(void *argument)
{
    UNUSED(argument);
    osDelay(300);
    while (1) {
        ArmTask();
        osDelay(1);
    }
}

void _motorTASK(void *argument)
{
    UNUSED(argument);
    osDelay(300);
    while (1) {
        HAL_IWDG_Refresh(&hiwdg1);
        MotorControlTask();
        osDelay(1);
    }
}

void _DaemonTask(void *argument)
{
    UNUSED(argument);
    osDelay(300);
    while (1) {
        DaemonTask();
    }
}

void _BuzzerTask(void *argument)
{
    UNUSED(argument);
    osDelay(300);
    while (1) {
        BuzzerTask(argument);
    }
}

void _refereeTask(void *argument)
{
    UNUSED(argument);
    MyUIInit();
    osDelay(300);
    while (1) {
        MyUIRefresh();
    }
}

#endif