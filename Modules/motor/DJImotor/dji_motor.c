#include "dji_motor.h"
#include "general_def.h"
#include "bsp_dwt.h"
#include "bsp_log.h"

static uint8_t idx = 0; // register idx,是该文件的全局电机索引,在注册时使用
/* DJI电机的实例,此处仅保存指针,内存的分配将通过电机实例初始化时通过malloc()进行 */
static DJIMotorInstance *dji_motor_instance[DJI_MOTOR_CNT] = {NULL}; // 会在control任务中遍历该指针数组进行pid计算

void DJIMotorErrorDetection(DJIMotorInstance *motor);

/**
 * @brief 由于DJI电机发送以四个一组的形式进行,故对其进行特殊处理,用6个(2can*3group)can_instance专门负责发送
 *        该变量将在 DJIMotorControl() 中使用,分组在 MotorSenderGrouping()中进行
 *
 * @note  因为只用于发送,所以不需要在bsp_can中注册
 *
 * C610(m2006)/C620(m3508):0x1ff,0x200;
 * GM6020:0x1ff,0x2ff *0x1fe,0x2fe(新固件)
 * 反馈(rx_id): GM6020: 0x204+id ; C610/C620: 0x200+id
 * can1: [0]:0x1FF,[1]:0x200,[2]:0x2FF
 * can2: [3]:0x1FF,[4]:0x200,[5]:0x2FF
 */
static CANInstance sender_assignment[15] = {
    [0] = {.can_handle = &hfdcan1, .txconf.Identifier = 0x1ff, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    [1] = {.can_handle = &hfdcan1, .txconf.Identifier = 0x200, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    [2] = {.can_handle = &hfdcan1, .txconf.Identifier = 0x2ff, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    [3] = {.can_handle = &hfdcan2, .txconf.Identifier = 0x1ff, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    [4] = {.can_handle = &hfdcan2, .txconf.Identifier = 0x200, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    [5] = {.can_handle = &hfdcan2, .txconf.Identifier = 0x2ff, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    // [6] = {.can_handle = &hfdcan3, .txconf.Identifier = 0x1ff, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    // [7] = {.can_handle = &hfdcan3, .txconf.Identifier = 0x200, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    // [8] = {.can_handle = &hfdcan3, .txconf.Identifier = 0x2ff, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    [9] = {.can_handle = &hfdcan1, .txconf.Identifier = 0x1fe, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    [10] = {.can_handle = &hfdcan2, .txconf.Identifier = 0x1fe, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    // [11] = {.can_handle = &hfdcan3, .txconf.Identifier = 0x1fe, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    [12] = {.can_handle = &hfdcan1, .txconf.Identifier = 0x2fe, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    [13] = {.can_handle = &hfdcan2, .txconf.Identifier = 0x2fe, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
    // [14] = {.can_handle = &hfdcan3, .txconf.Identifier = 0x2fe, .txconf.IdType = FDCAN_STANDARD_ID, .txconf.DataLength = FDCAN_DLC_BYTES_8, .tx_buff = {0}},
};

/**
 * @brief 9个用于确认是否有电机注册到sender_assignment中的标志位,防止发送空帧,此变量将在DJIMotorControl()使用
 *        flag的初始化在 MotorSenderGrouping()中进行
 */
static uint8_t sender_enable_flag[15] = {0};

/**
 * @brief 根据电调/拨码开关上的ID,根据说明书的默认id分配方式计算发送ID和接收ID,
 *        并对电机进行分组以便处理多电机控制命令
 */
static void MotorSenderGrouping(DJIMotorInstance *motor, CAN_Init_Config_s *config)
{
    uint8_t motor_id = config->tx_id - 1; // 下标从零开始,先减一方便赋值
    uint8_t motor_send_num;
    uint8_t motor_grouping;

    switch (motor->motor_type) {
        case M2006:
        case M3508:
            if (motor_id < 4) // 根据ID分组
            {
                motor_send_num = motor_id;
                motor_grouping = config->can_handle == &hfdcan1 ? 1 : (config->can_handle == &hfdcan2 ? 4 : 7);
            } else {
                motor_send_num = motor_id - 4;
                motor_grouping = config->can_handle == &hfdcan1 ? 0 : (config->can_handle == &hfdcan2 ? 3 : 6);
            }

            // 计算接收id并设置分组发送id
            config->rx_id                      = 0x200 + motor_id + 1; // 把ID+1,进行分组设置
            sender_enable_flag[motor_grouping] = 1;                    // 设置发送标志位,防止发送空帧
            motor->message_num                 = motor_send_num;
            motor->sender_group                = motor_grouping;

            // 检查是否发生id冲突
            for (size_t i = 0; i < idx; ++i) {
                if (dji_motor_instance[i]->motor_can_instance->can_handle == config->can_handle && dji_motor_instance[i]->motor_can_instance->rx_id == config->rx_id) {
                    LOGERROR("[dji_motor] ID crash. Check in debug mode, add dji_motor_instance to watch to get more information.");
                    uint8_t can_bus = motor->motor_can_instance->can_handle == &hfdcan1 ? 1 : (motor->motor_can_instance->can_handle == &hfdcan2 ? 2 : 3);
                    while (1) // 6020的id 1-4和2006/3508的id 5-8会发生冲突(若有注册,即1!5,2!6,3!7,4!8) (1!5!,LTC! (((不是)
                        LOGERROR("[dji_motor] id [%x], can_bus [%d]", config->rx_id, can_bus);
                }
            }
            break;
        
        case GM6020:
            if (motor_id < 4) {
                motor_send_num = motor_id;
                motor_grouping = config->can_handle == &hfdcan1 ? 9 : (config->can_handle == &hfdcan2 ? 10 : 11);
            } else {
                motor_send_num = motor_id - 4;
                motor_grouping = config->can_handle == &hfdcan1 ? 12 : (config->can_handle == &hfdcan2 ? 13 : 14);
            }

            config->rx_id                      = 0x204 + motor_id + 1; // 把ID+1,进行分组设置
            sender_enable_flag[motor_grouping] = 1;                    // 只要有电机注册到这个分组,置为1;在发送函数中会通过此标志判断是否有电机注册
            motor->message_num                 = motor_send_num;
            motor->sender_group                = motor_grouping;

            for (size_t i = 0; i < idx; ++i) {
                if (dji_motor_instance[i]->motor_can_instance->can_handle == config->can_handle && dji_motor_instance[i]->motor_can_instance->rx_id == config->rx_id) {
                    LOGERROR("[dji_motor] ID crash. Check in debug mode, add dji_motor_instance to watch to get more information.");
                    uint16_t can_bus;
                    can_bus = motor->motor_can_instance->can_handle == &hfdcan1 ? 1 : (motor->motor_can_instance->can_handle == &hfdcan2 ? 2 : 3);
                    while (1) // 6020的id 1-4和2006/3508的id 5-8会发生冲突(若有注册,即1!5,2!6,3!7,4!8) (1!5!,LTC! (((不是)
                        LOGERROR("[dji_motor] id [%d], can_bus [%d]", config->rx_id, can_bus);
                }
            }
            break;

        default: // other motors should not be registered here
            while (1)
                LOGERROR("[dji_motor]You must not register other motors using the API of DJI motor."); // 其他电机不应该在这里注册
    }
}

/**
 * @todo  是否可以简化多圈角度的计算？
 * @brief 根据返回的can_instance对反馈报文进行解析
 *
 * @param _instance 收到数据的instance,通过遍历与所有电机进行对比以选择正确的实例
 */
static void DecodeDJIMotor(CANInstance *_instance)
{
    // 这里对can instance的id进行了强制转换,从而获得电机的instance实例地址
    // _instance指针指向的id是对应电机instance的地址,通过强制转换为电机instance的指针,再通过->运算符访问电机的成员motor_measure,最后取地址获得指针
    uint8_t *rxbuff              = _instance->rx_buff;
    DJIMotorInstance *motor      = (DJIMotorInstance *)_instance->id;
    Motor_Measure_s *measure = &motor->measure; // measure要多次使用,保存指针减小访存开销

    DaemonReload(motor->daemon);
    motor->dt = DWT_GetDeltaT(&motor->feed_cnt);

    // 解析数据并对电流和速度进行滤波,电机的反馈报文具体格式见电机说明手册
    measure->last_ecd           = measure->ecd;
    measure->ecd                = ((uint16_t)rxbuff[0]) << 8 | rxbuff[1];
    measure->ecd                = ((measure->ecd >= measure->offset_ecd) ? (measure->ecd - measure->offset_ecd) : (measure->ecd + 8191 - measure->offset_ecd));
    measure->angle_single_round = ECD_ANGLE_COEF_DJI * (float)measure->ecd;
    measure->last_speed_aps     = measure->speed_aps;
    measure->speed_aps          = (1.0f - SPEED_SMOOTH_COEF) * measure->speed_aps +
                         RPM_2_ANGLE_PER_SEC * SPEED_SMOOTH_COEF * (float)((int16_t)(rxbuff[2] << 8 | rxbuff[3]));
    measure->real_current = (1.0f - CURRENT_SMOOTH_COEF) * measure->real_current +
                            CURRENT_SMOOTH_COEF * (float)((int16_t)(rxbuff[4] << 8 | rxbuff[5]));
    measure->temperature = rxbuff[6];

    if ((int16_t)(measure->ecd - measure->last_ecd) > 4096)
        measure->total_round--;
    else if ((int16_t)(measure->ecd - measure->last_ecd) < -4096)
        measure->total_round++;

    measure->total_angle = measure->total_round * 360 + measure->angle_single_round;

    DJIMotorErrorDetection(motor);
}

static void DJIMotorLostCallback(void *motor_ptr)
{
    uint16_t can_bus;
    DJIMotorInstance *motor = (DJIMotorInstance *)motor_ptr;
    can_bus                 = motor->motor_can_instance->can_handle == &hfdcan1 ? 1 : (motor->motor_can_instance->can_handle == &hfdcan2 ? 2 : 3);
    LOGWARNING("[dji_motor] Motor lost, can bus [%d] , id [%d]", can_bus, motor->motor_can_instance->tx_id);
}

// 电机初始化,返回一个电机实例
DJIMotorInstance *DJIMotorInit(Motor_Init_Config_s *config)
{
    DJIMotorInstance *instance = (DJIMotorInstance *)malloc(sizeof(DJIMotorInstance));
    memset(instance, 0, sizeof(DJIMotorInstance));

    // motor basic setting 电机基本设置
    instance->motor_type     = config->motor_type;                     // 6020 or 2006 or 3508
    instance->motor_settings = config->controller_setting_init_config; // 正反转,闭环类型等

    // motor controller init 电机控制器初始化
    PIDInit(&instance->motor_controller.current_PID, &config->controller_param_init_config.current_PID);
    PIDInit(&instance->motor_controller.speed_PID, &config->controller_param_init_config.speed_PID);
    PIDInit(&instance->motor_controller.angle_PID, &config->controller_param_init_config.angle_PID);
    PIDInit(&instance->motor_controller.torque_PID, &config->controller_param_init_config.torque_PID);
    instance->motor_controller.other_angle_feedback_ptr = config->controller_param_init_config.other_angle_feedback_ptr;
    instance->motor_controller.other_speed_feedback_ptr = config->controller_param_init_config.other_speed_feedback_ptr;
    instance->motor_controller.current_feedforward_ptr  = config->controller_param_init_config.current_feedforward_ptr;
    instance->motor_controller.speed_feedforward_ptr    = config->controller_param_init_config.speed_feedforward_ptr;

    instance->motor_error_detection.current = (config->motor_error_detection_config.current!=NULL ? config->motor_error_detection_config.current : &instance->motor_controller.output_current);
    instance->motor_error_detection.last_speed = (config->motor_error_detection_config.last_speed!=NULL ? config->motor_error_detection_config.last_speed : &instance->measure.last_speed_aps);
    instance->motor_error_detection.speed = (config->motor_error_detection_config.speed!=NULL ? config->motor_error_detection_config.speed : &instance->measure.speed_aps);
    instance->motor_error_detection.stuck_speed = config->motor_error_detection_config.stuck_speed==0 ? 200.0f : config->motor_error_detection_config.stuck_speed;
    instance->motor_error_detection.crash_detective_sensitivity = config->motor_error_detection_config.crash_detective_sensitivity==0 ? 5 : config->motor_error_detection_config.crash_detective_sensitivity;
    instance->motor_error_detection.max_current = config->motor_error_detection_config.max_current==0 ? instance->motor_controller.speed_PID.MaxOut : config->motor_error_detection_config.max_current;
    instance->motor_error_detection.stuck_current_ptr = config->motor_error_detection_config.stuck_current_ptr;
    instance->motor_error_detection.error_callback = config->motor_error_detection_config.error_callback;
    instance->motor_error_detection.error_detection_flag = config->motor_error_detection_config.error_detection_flag;
    
    // 后续增加电机前馈控制器(速度和电流)

    // 电机分组,因为至多4个电机可以共用一帧CAN控制报文
    MotorSenderGrouping(instance, &config->can_init_config);

    // 注册电机到CAN总线
    config->can_init_config.can_module_callback = DecodeDJIMotor; // set callback
    config->can_init_config.id                  = instance;       // set id,eq to address(it is IdTypentity)
    instance->motor_can_instance                = CANRegister(&config->can_init_config);

    // 注册守护线程
    Daemon_Init_Config_s daemon_config = {
        .callback     = DJIMotorLostCallback,
        .owner_id     = instance,
        .reload_count = 2, // 20ms未收到数据则丢失
    };
    instance->daemon = DaemonRegister(&daemon_config);

    DJIMotorStop(instance);
    dji_motor_instance[idx++] = instance;
    return instance;
}

/* 电流只能通过电机自带传感器监测,后续考虑加入力矩传感器应变片等 */
void DJIMotorChangeFeed(DJIMotorInstance *motor, Closeloop_Type_e loop, Feedback_Source_e type)
{
    if (loop == ANGLE_LOOP)
        motor->motor_settings.angle_feedback_source = type;
    else if (loop == SPEED_LOOP)
        motor->motor_settings.speed_feedback_source = type;
    else
        LOGERROR("[dji_motor] loop type error, check memory access and func param"); // 检查是否传入了正确的LOOP类型,或发生了指针越界
}

void DJIMotorStop(DJIMotorInstance *motor)
{
    motor->stop_flag = MOTOR_STOP;
}

void DJIMotorEnable(DJIMotorInstance *motor)
{
    motor->stop_flag = MOTOR_ENABLED;
}

/* 修改电机的实际闭环对象 */
void DJIMotorOuterLoop(DJIMotorInstance *motor, Closeloop_Type_e outer_loop)
{
    motor->motor_settings.outer_loop_type = outer_loop;
}

// 设置参考值
void DJIMotorSetRef(DJIMotorInstance *motor, float ref)
{
    motor->motor_controller.pid_ref = ref;
}

// 异常检测
void DJIMotorErrorDetection(DJIMotorInstance *motor)
{
    /* todo:理论上该有个更泛用性的代码 */
    // 碰撞检测
    if(motor->motor_error_detection.error_detection_flag & MOTOR_ERROR_DETECTION_CRASH)
    {
        if(motor->motor_error_detection.stuck_current_ptr == NULL)
        {
            uint16_t can_bus;
            can_bus                 = motor->motor_can_instance->can_handle == &hfdcan1 ? 1 : (motor->motor_can_instance->can_handle == &hfdcan2 ? 2 : 3);
            LOGWARNING("[dji_motor] You should set the stuck current, can bus [%d] , id [%d]", can_bus, motor->motor_can_instance->tx_id);
            while(1);
        }else
        {
            if(!(motor->motor_error_detection.ErrorCode & MOTOR_ERROR_CRASH) && (fabsf(*motor->motor_error_detection.last_speed) - fabsf(*motor->motor_error_detection.speed) > fabsf(*motor->motor_error_detection.last_speed / (float)motor->motor_error_detection.crash_detective_sensitivity)) && (fabsf(*motor->motor_error_detection.last_speed) > motor->motor_error_detection.stuck_speed)  && (fabsf(*motor->motor_error_detection.current) > fabsf(*motor->motor_error_detection.stuck_current_ptr)) && motor->stop_flag)
            {
                motor->motor_error_detection.ErrorCode |= MOTOR_ERROR_DETECTION_CRASH;

                uint16_t can_bus;
                can_bus                 = motor->motor_can_instance->can_handle == &hfdcan1 ? 1 : (motor->motor_can_instance->can_handle == &hfdcan2 ? 2 : 3);
                LOGWARNING("[dji_motor] Motor crashed! can bus [%d] , id [%d]", can_bus, motor->motor_can_instance->tx_id);
            }
        }
    }
    // 堵转检测
    if(motor->motor_error_detection.error_detection_flag  & MOTOR_ERROR_DETECTION_STUCK)
    {
        if(motor->motor_error_detection.stuck_current_ptr == NULL)
        {
            uint16_t can_bus;
            can_bus                 = motor->motor_can_instance->can_handle == &hfdcan1 ? 1 : (motor->motor_can_instance->can_handle == &hfdcan2 ? 2 : 3);
            LOGWARNING("[dji_motor] You must set the stuck current, can bus [%d] , id [%d]", can_bus, motor->motor_can_instance->tx_id);
            while(1);
        }else
        {
            if(!(motor->motor_error_detection.ErrorCode & MOTOR_ERROR_DETECTION_STUCK) && (fabsf(*(motor->motor_error_detection.speed)) < motor->motor_error_detection.stuck_speed) && (fabsf(*(motor->motor_error_detection.current)) > *(motor->motor_error_detection.stuck_current_ptr)) && motor->stop_flag)
            {
                motor->motor_error_detection.stuck_cnt++;
                if(motor->motor_error_detection.stuck_cnt > 10)
                {
                    motor->motor_error_detection.ErrorCode |= MOTOR_ERROR_DETECTION_STUCK;

                    uint16_t can_bus;
                    can_bus                 = motor->motor_can_instance->can_handle == &hfdcan1 ? 1 : (motor->motor_can_instance->can_handle == &hfdcan2 ? 2 : 3);
                    LOGWARNING("[dji_motor] Motor stucked! can bus [%d] , id [%d]", can_bus, motor->motor_can_instance->tx_id);
                }
            }
            else
            {
                motor->motor_error_detection.stuck_cnt = 0;
            }
        }
    }

    // 调用异常回调函数
    if(motor->motor_error_detection.ErrorCode != MOTOR_ERROR_NONE)
        if(motor->motor_error_detection.error_callback != NULL)
            motor->motor_error_detection.error_callback(motor);
}   

// 为所有电机实例计算三环PID,发送控制报文
void DJIMotorControl()
{
    // 直接保存一次指针引用从而减小访存的开销,同样可以提高可读性
    uint8_t group, num; // 电机组号和组内编号
    int16_t set;        // 电机控制CAN发送设定值
    DJIMotorInstance *motor;
    Motor_Control_Setting_s *motor_setting; // 电机控制参数
    Motor_Controller_s *motor_controller;   // 电机控制器
    Motor_Measure_s *measure;           // 电机测量值
    float pid_measure, pid_ref;             // 电机PID测量值和设定值

    // 遍历所有电机实例,进行串级PID的计算并设置发送报文的值
    for (size_t i = 0; i < idx; ++i) { // 减小访存开销,先保存指针引用
        motor            = dji_motor_instance[i];
        motor_setting    = &motor->motor_settings;
        motor_controller = &motor->motor_controller;
        measure          = &motor->measure;
        pid_ref          = motor_controller->pid_ref; // 保存设定值,防止motor_controller->pid_ref在计算过程中被修改

        // pid_ref会顺次通过被启用的闭环充当数据的载体
        // 计算扭矩环,通过设置相对于当前角度偏移固定小角度的目标角度实现
        if ((motor_setting->close_loop_type & TORQUE_LOOP) && motor_setting->outer_loop_type == TORQUE_LOOP) {
            pid_measure = 0;
            pid_ref = PIDCalculate(&motor_controller->torque_PID, pid_measure, pid_ref);
            if (motor_setting->angle_feedback_source == OTHER_FEED)
                pid_ref += *motor_controller->other_angle_feedback_ptr;
            else
                pid_ref += measure->total_angle;
        }

        // 计算位置环,只有启用位置环且外层闭环为位置或扭矩时会计算速度环输出
        if ((motor_setting->close_loop_type & ANGLE_LOOP) && (motor_setting->outer_loop_type & (ANGLE_LOOP | TORQUE_LOOP))) {
            if (motor_setting->angle_feedback_source == OTHER_FEED)
                pid_measure = *motor_controller->other_angle_feedback_ptr;
            else
                pid_measure = measure->total_angle; // MOTOR_FEED,对total angle闭环,防止在边界处出现突跃
            // 更新pid_ref进入下一个环
            if (motor_setting->feedback_reverse_flag == FEEDBACK_DIRECTION_REVERSE)
                pid_ref = PIDCalculate(&motor_controller->angle_PID, -pid_measure, pid_ref);
            else
                pid_ref = PIDCalculate(&motor_controller->angle_PID, pid_measure, pid_ref);
        }

        // 电机反转，即将目标速度取反
        if (motor_setting->motor_reverse_flag == MOTOR_DIRECTION_REVERSE)
            pid_ref *= -1;

        // 计算速度环,(外层闭环为速度或位置或扭矩)且(启用速度环)时会计算速度环
        if ((motor_setting->close_loop_type & SPEED_LOOP) && (motor_setting->outer_loop_type & (ANGLE_LOOP | SPEED_LOOP | TORQUE_LOOP))) {
            if (motor_setting->feedforward_flag & SPEED_FEEDFORWARD)
                pid_ref += *motor_controller->speed_feedforward_ptr;

            if (motor_setting->speed_feedback_source == OTHER_FEED)
                pid_measure = *motor_controller->other_speed_feedback_ptr;
            else // MOTOR_FEED
                pid_measure = measure->speed_aps;
            // 更新pid_ref进入下一个环
            pid_ref = PIDCalculate(&motor_controller->speed_PID, pid_measure, pid_ref);
        }

        // 计算电流环,目前只要启用了电流环就计算,不管外层闭环是什么,并且电流只有电机自身传感器的反馈
        if (motor_setting->feedforward_flag & CURRENT_FEEDFORWARD && motor_controller->current_feedforward_ptr!=NULL)
            pid_ref += *motor_controller->current_feedforward_ptr;
        if (motor_setting->close_loop_type & CURRENT_LOOP) {
            pid_ref = PIDCalculate(&motor_controller->current_PID, measure->real_current, pid_ref);
        }

        // 获取最终输出
        set = (int16_t)pid_ref;
        // 若该电机处于停止状态,直接将buff置零
        if (motor->stop_flag == MOTOR_STOP)
            set = 0;
        motor_controller->output_current = set;
        // 分组填入发送数据
        group                                         = motor->sender_group;
        num                                           = motor->message_num;
        sender_assignment[group].tx_buff[2 * num]     = (uint8_t)(set >> 8);     // 低八位
        sender_assignment[group].tx_buff[2 * num + 1] = (uint8_t)(set & 0x00ff); // 高八位

            // memset(sender_assignment[group].tx_buff + 2 * num, 0, 16u);
    }

    // 遍历flag,检查是否要发送这一帧报文
    for (size_t i = 0; i < 15; ++i) {
        if (sender_enable_flag[i]) {
            // TODO:测试调试
            CANTransmit(&sender_assignment[i], 1);
        }
    }
}
