#include "LKmotor.h"
#include "stdlib.h"
#include "general_def.h"
#include "daemon.h"
#include "bsp_dwt.h"
#include "bsp_log.h"

static uint8_t idx;
static LKMotorInstance *lkmotor_instance[LK_MOTOR_MX_CNT] = {NULL};
// static CANInstance *sender_instance; // 多电机发送时使用的caninstance(当前保存的是注册的第一个电机的caninstance)
// 后续考虑兼容单电机和多电机指令.

/**
 * @brief 电机反馈报文解析
 *
 * @param _instance 发生中断的caninstance
 */
static void LKMotorDecode(CANInstance *_instance)
{
    LKMotorInstance *motor = (LKMotorInstance *)_instance->id; // 通过caninstance保存的father id获取对应的motorinstance
    LKMotor_Measure_t *measure = &motor->measure;
    uint8_t *rx_buff = _instance->rx_buff;

    DaemonReload(motor->daemon); // 喂狗
    measure->feed_dt = DWT_GetDeltaT(&measure->feed_dwt_cnt);

    measure->last_ecd = measure->ecd;
    measure->ecd = (uint16_t)((rx_buff[7] << 8) | rx_buff[6]);

    // measure->angle_single_round = ECD_ANGLE_COEF_LK * measure->ecd;
    // todo:优化
    measure->angle_single_round = (360.0f / (float)measure->max_ecd) * measure->ecd;

    measure->speed_rads = (1 - SPEED_SMOOTH_COEF) * measure->speed_rads +
                          DEGREE_2_RAD * SPEED_SMOOTH_COEF * (float)((int16_t)(rx_buff[5] << 8 | rx_buff[4]));

    measure->real_current = (1 - CURRENT_SMOOTH_COEF) * measure->real_current +
                            CURRENT_SMOOTH_COEF * (float)((int16_t)(rx_buff[3] << 8 | rx_buff[2]));

    measure->temperature = rx_buff[1];
    
    if ((int16_t)measure->ecd - measure->last_ecd > measure->max_ecd/2)
        measure->total_round--;
    else if ((int16_t)measure->ecd - measure->last_ecd < -measure->max_ecd/2)
        measure->total_round++;

    measure->total_angle = measure->total_round * 360 + measure->angle_single_round;

    motor->recv_data = 1;
}

static void LKMotorLostCallback(void *motor_ptr)
{
    LKMotorInstance *motor = (LKMotorInstance *)motor_ptr;
    // LOGWARNING("[LKMotor] motor lost, id: %x", motor->motor_can_ins->tx_id);
    motor->recv_data = 0;
}

LKMotorInstance *LKMotorInit(Motor_Init_Config_s *config)
{
    LKMotorInstance *motor = (LKMotorInstance *)malloc(sizeof(LKMotorInstance));
    // motor = (LKMotorInstance *)malloc(sizeof(LKMotorInstance));
    memset(motor, 0, sizeof(LKMotorInstance));

    motor->motor_settings = config->controller_setting_init_config;
    PIDInit(&motor->current_PID, &config->controller_param_init_config.current_PID);
    PIDInit(&motor->speed_PID, &config->controller_param_init_config.speed_PID);
    PIDInit(&motor->angle_PID, &config->controller_param_init_config.angle_PID);
    motor->other_angle_feedback_ptr = config->controller_param_init_config.other_angle_feedback_ptr;
    motor->other_speed_feedback_ptr = config->controller_param_init_config.other_speed_feedback_ptr;
    motor->motor_contro_type = config->motor_contro_type;

    motor->motor_type = config->motor_type;
    switch(motor->motor_type){
        case LK_MS5005:
            motor->measure.max_ecd = 32768;
            break;
        case LK9025:
            motor->measure.max_ecd = 16383;
            break;
        default: // other motors should not be registered here
            while (1)
                LOGERROR("[dji_motor]You must not register other motors using the API of DJI motor."); // 其他电机不应该在这里注册
    }

    config->can_init_config.id = motor;
    config->can_init_config.can_module_callback = LKMotorDecode;
    config->can_init_config.rx_id = 0x140 + config->can_init_config.tx_id;
    // 多电机发送需要在上位机中开启，因为臂臂上只有一个LK电机，暂时不用
    // config->can_init_config.tx_id = config->can_init_config.tx_id + 0x280 - 1; // 这样在发送写入buffer的时候更方便,因为下标从0开始,LK多电机发送id为0x280
    // if (idx == 0) // 用第一个电机的can instance发送数据
    // {
    //     sender_instance = motor->motor_can_ins;
    //     sender_instance->tx_id = 0x280; //  修改tx_id为0x280,用于多电机发送,不用管其他LKMotorInstance的tx_id,它们仅作初始化用
    // }
    config->can_init_config.tx_id = 0x140 + config->can_init_config.tx_id;
    motor->motor_can_ins = CANRegister(&config->can_init_config);    

    LKMotorStop(motor);
    DWT_GetDeltaT(&motor->measure.feed_dwt_cnt);
    lkmotor_instance[idx++] = motor;

    Daemon_Init_Config_s daemon_config = {
        .callback = LKMotorLostCallback,
        .owner_id = motor,
        .reload_count = 2, // 50ms
    };
    motor->daemon = DaemonRegister(&daemon_config);

    return motor;
}

/* 第一个电机的can instance用于发送数据,向其tx_buff填充数据 */
void LKMotorControl()
{
    float pid_measure, pid_ref;
    int16_t set;
    LKMotorInstance *motor;
    LKMotor_Measure_t *measure;
    Motor_Control_Setting_s *setting;

    for (size_t i = 0; i < idx; ++i)
    {
        motor = lkmotor_instance[i];
        measure = &motor->measure;
        setting = &motor->motor_settings;
        pid_ref = motor->pid_ref;

        /* 扭矩开环控制 */
        if (motor->motor_contro_type == TORQUE_LOOP_CONTRO){
            if ((setting->close_loop_type & ANGLE_LOOP) && setting->outer_loop_type == ANGLE_LOOP)
            {
                if (setting->angle_feedback_source == OTHER_FEED)
                    pid_measure = *motor->other_angle_feedback_ptr;
                else
                    pid_measure = measure->total_angle;
                pid_ref = PIDCalculate(&motor->angle_PID, pid_measure, pid_ref);
                if (setting->feedforward_flag & SPEED_FEEDFORWARD)
                    pid_ref += *motor->speed_feedforward_ptr;
            }

            //反转检测
            if (setting->motor_reverse_flag == MOTOR_DIRECTION_REVERSE)
                pid_ref *= -1;

            if ((setting->close_loop_type & SPEED_LOOP) && setting->outer_loop_type & (ANGLE_LOOP | SPEED_LOOP))
            {
                if (setting->speed_feedback_source == OTHER_FEED)
                    pid_measure = *motor->other_speed_feedback_ptr;
                else
                    pid_measure = measure->speed_rads;
                pid_ref = PIDCalculate(&motor->speed_PID, pid_measure, pid_ref);
                if (setting->feedforward_flag & CURRENT_FEEDFORWARD)
                    pid_ref += *motor->current_feedforward_ptr;
            }

            if (setting->close_loop_type & CURRENT_LOOP)
            {
                pid_ref = PIDCalculate(&motor->current_PID, measure->real_current, pid_ref);
            }

            set = pid_ref;
            
            // 同上，暂时不采用多电机发送
            // 这里随便写的,为了兼容多电机命令.后续应该将tx_id以更好的方式表达电机id,单独使用一个CANInstance,而不是用第一个电机的CANInstance
            // memcpy(sender_instance->tx_buff + (motor->motor_can_ins->tx_id - 0x280) * 2, &set, sizeof(uint16_t));
            memcpy(motor->motor_can_ins->tx_buff + 4, (uint8_t*)&set, sizeof(uint16_t));
            motor->motor_can_ins->tx_buff[0] = 0xA0;

            if (motor->stop_flag == MOTOR_STOP || !LKMotorIsOnline(motor))
            { // 若该电机处于停止状态,直接将发送buff置零
                // 同上，暂时不采用多电机发送
                // memset(sender_instance->tx_buff + (motor->motor_can_ins->tx_id - 0x280) * 2, 0, sizeof(uint16_t));
                memset(motor->motor_can_ins->tx_buff + 4, 0, sizeof(uint16_t));
            }
            
            CANTransmit(motor->motor_can_ins, 2);
            // if (idx) // 如果有电机注册了
            //     CANTransmit(sender_instance, 0.2);
        }else if(motor->motor_contro_type == ANGLE_LOOP_CONTRO){
            memcpy(motor->motor_can_ins->tx_buff + 4, (uint8_t*)&pid_ref, sizeof(uint16_t));
            motor->motor_can_ins->tx_buff[0] = 0xA3;
            CANTransmit(motor->motor_can_ins, 2);
        }
    }
}

void LKMotorStop(LKMotorInstance *motor)
{
    motor->stop_flag = MOTOR_STOP;
}

void LKMotorEnable(LKMotorInstance *motor)
{
    motor->stop_flag = MOTOR_ENABLED;
}

void LKMotorSetRef(LKMotorInstance *motor, float ref)
{
    motor->pid_ref = ref;
}

uint8_t LKMotorIsOnline(LKMotorInstance *motor)
{
    return DaemonIsOnline(motor->daemon);
}
