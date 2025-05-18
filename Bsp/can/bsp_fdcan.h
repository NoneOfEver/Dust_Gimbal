/**
 * @file Bsp_FDCAN.h
 * @author noe (noneofever.com)
 * @brief 
 * @version 0.1
 * @date 2025-01-15
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef BSP_FDCAN_H
#define BSP_FDCAN_H

#include <stdint.h>
#include "fdcan.h"

// 最多能够支持的FDCAN设备数
#define FDCAN_MX_REGISTER_CNT 12     // 这个数量取决于FDCAN总线的负载
#define MX_FDCAN_FILTER_CNT (2 * 14) // 最多可以使用的FDCAN过滤器数量,目前远不会用到这么多
#define DEVICE_FDCAN_CNT 3           // H723有3个FDCAN
// 如果只有1个FDCAN,还需要把bsp_fdcan.c中所有的hfdcan2变量改为hfdcan1(别担心,主要是总线和FIFO的负载均衡,不影响功能)

/* fdcan instance typedef, every module registered to FDCAN should have this variable */
#pragma pack(1)
typedef struct _
{
    FDCAN_HandleTypeDef *fdcan_handle; // fdcan句柄
    FDCAN_TxHeaderTypeDef txconf;    // FDCAN报文发送配置
    uint32_t tx_id;                // 发送id
    uint32_t tx_mailbox;           // FDCAN消息填入的邮箱号
    uint8_t tx_buff[8];            // 发送缓存,发送消息长度可以通过FDCANSetDLC()设定,最大为8
    uint8_t rx_buff[8];            // 接收缓存,最大消息长度为8
    uint32_t rx_id;                // 接收id
    uint8_t rx_len;                // 接收长度,可能为0-8
    // 接收的回调函数,用于解析接收到的数据
    void (*fdcan_module_callback)(struct _ *); // callback needs an instance to tell among registered ones
    void *id;                                // 使用fdcan外设的模块指针(即id指向的模块拥有此fdcan实例,是父子关系)
} FDCANInstance;
#pragma pack()

/* FDCAN实例初始化结构体,将此结构体指针传入注册函数 */
typedef struct
{
    FDCAN_HandleTypeDef *fdcan_handle;              // fdcan句柄
    uint32_t tx_id;                             // 发送id
    uint32_t rx_id;                             // 接收id
    void (*fdcan_module_callback)(FDCANInstance *); // 处理接收数据的回调函数
    void *id;                                   // 拥有fdcan实例的模块地址,用于区分不同的模块(如果有需要的话),如果不需要可以不传入
} FDCAN_Init_Config_s;

/**
 * @brief Register a module to FDCAN service,remember to call this before using a FDCAN device
 *        注册(初始化)一个fdcan实例,需要传入初始化配置的指针.
 * @param config init config
 * @return FDCANInstance* fdcan instance owned by module
 */
FDCANInstance *FDCANRegister(FDCAN_Init_Config_s *config);

/**
 * @brief 修改FDCAN发送报文的数据帧长度;注意最大长度为8,在没有进行修改的时候,默认长度为8
 *
 * @param _instance 要修改长度的fdcan实例
 * @param length    设定长度
 */
void FDCANSetDLC(FDCANInstance *_instance, uint8_t length);

/**
 * @brief transmit mesg through FDCAN device,通过fdcan实例发送消息
 *        发送前需要向FDCAN实例的tx_buff写入发送数据
 * 
 * @attention 超时时间不应该超过调用此函数的任务的周期,否则会导致任务阻塞
 * 
 * @param timeout 超时时间,单位为ms;后续改为us,获得更精确的控制
 * @param _instance* fdcan instance owned by module
 */
uint8_t FDCANTransmit(FDCANInstance *_instance,float timeout);

#endif
