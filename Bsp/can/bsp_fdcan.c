/**
 * @file Bsp_FDCAN.c
 * @author noe (noneofever@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-01-15
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "Bsp_FDCAN.h"
#include "main.h"
#include "memory.h"
#include "stdlib.h"
/* fdcan instance ptrs storage, used for recv callback */
// 在FDCAN产生接收中断会遍历数组,选出hfdcan和rxid与发生中断的实例相同的那个,调用其回调函数
// @todo: 后续为每个FDCAN总线单独添加一个fdcan_instance指针数组,提高回调查找的性能
static FDCANInstance *fdcan_instance[FDCAN_MX_REGISTER_CNT] = {NULL};
static uint8_t idx; // 全局FDCAN实例索引,每次有新的模块注册会自增

/* ----------------two static function called by FDCANRegister()-------------------- */

/**
 * @brief 添加过滤器以实现对特定id的报文的接收,会被FDCANRegister()调用
 *        给FDCAN添加过滤器后,BxFDCAN会根据接收到的报文的id进行消息过滤,符合规则的id会被填入FIFO触发中断
 *
 * @note f407的bxFDCAN有28个过滤器,这里将其配置为前14个过滤器给FDCAN1使用,后14个被FDCAN2使用
 *       初始化时,奇数id的模块会被分配到FIFO0,偶数id的模块会被分配到FIFO1
 *       注册到FDCAN1的模块使用过滤器0-13,FDCAN2使用过滤器14-27
 *
 * @attention 你不需要完全理解这个函数的作用,因为它主要是用于初始化,在开发过程中不需要关心底层的实现
 *            享受开发的乐趣吧!如果你真的想知道这个函数在干什么,请联系作者或自己查阅资料(请直接查阅官方的reference manual)
 *
 * @param _instance fdcan instance owned by specific module
 */
static void FDCANAddFilter(FDCANInstance *_instance)
{
    FDCAN_FilterTypeDef fdcan_filter_conf;
    static uint8_t fdcan1_filter_idx = 0, fdcan2_filter_idx = 14; // 0-13给fdcan1用,14-27给fdcan2用

    fdcan_filter_conf.IdType = FDCAN_STANDARD_ID;//过滤标准ID，经典CAN只有标准ID
    fdcan_filter_conf.FilterIndex = 0;//过滤器编号，用几路CAN就依次类推0、1、2
    fdcan_filter_conf.FilterType = FDCAN_FILTER_MASK;//过滤器mask模式，关乎下面ID1，ID2的配置
    fdcan_filter_conf.FilterID1 = 0x00000000;//这个都行，只要ID2配置0x00000000就不会过滤掉任何ID
    fdcan_filter_conf.FilterID2 = 0x00000000;
    HAL_FDCAN_ConfigFilter(_instance->fdcan_handle, &fdcan_filter_conf);
    HAL_FDCAN_ConfigGlobalFilter(_instance->fdcan_handle, FDCAN_REJECT, FDCAN_REJECT, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE);//开启全局过滤

}

/**
 * @brief 在第一个FDCAN实例初始化的时候会自动调用此函数,启动FDCAN服务
 *
 * @note 此函数会启动FDCAN1和FDCAN2,开启FDCAN1和FDCAN2的FIFO0 & FIFO1溢出通知
 *
 */
static void FDCANServiceInit()
{
    HAL_FDCAN_Start(&hfdcan1);
    HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE,0);
    HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO1_NEW_MESSAGE,0);
    HAL_FDCAN_Start(&hfdcan2);
    HAL_FDCAN_ActivateNotification(&hfdcan2, FDCAN_IT_RX_FIFO0_NEW_MESSAGE,0);
    HAL_FDCAN_ActivateNotification(&hfdcan2, FDCAN_IT_RX_FIFO1_NEW_MESSAGE,0);
}

/* ----------------------- two extern callable function -----------------------*/

FDCANInstance *FDCANRegister(FDCAN_Init_Config_s *config)
{
    if (!idx)
    {
        FDCANServiceInit(); // 第一次注册,先进行硬件初始化
        // LOGINFO("[bsp_fdcan] FDCAN Service Init");
    }
    if (idx >= FDCAN_MX_REGISTER_CNT) // 超过最大实例数
    {
        // while (1)
            // LOGERROR("[bsp_fdcan] FDCAN instance exceeded MAX num, consider balance the load of FDCAN bus");
    }
    for (size_t i = 0; i < idx; i++)
    { // 重复注册 | id重复
        if (fdcan_instance[i]->rx_id == config->rx_id && fdcan_instance[i]->fdcan_handle == config->fdcan_handle)
        {
            // while (1)
                // LOGERROR("[}bsp_fdcan] FDCAN id crash ,tx [%d] or rx [%d] already registered", &config->tx_id, &config->rx_id);
        }
    }
    
    FDCANInstance *instance = (FDCANInstance *)malloc(sizeof(FDCANInstance)); // 分配空间
    memset(instance, 0, sizeof(FDCANInstance));                           // 分配的空间未必是0,所以要先清空
    // 进行发送报文的配置
    instance->txconf.IdType = FDCAN_STANDARD_ID;//标准id
    instance->txconf.TxFrameType = FDCAN_DATA_FRAME;//数据帧
    instance->txconf.DataLength = 8;//8字节
    instance->txconf.ErrorStateIndicator = FDCAN_ESI_ACTIVE;//can发送错误指示
    instance->txconf.BitRateSwitch = FDCAN_BRS_OFF;//波特率切换关闭
    instance->txconf.FDFormat = FDCAN_CLASSIC_CAN;//经典can模式
    instance->txconf.TxEventFifoControl = FDCAN_NO_TX_EVENTS;//不储存发送事件
    instance->txconf.MessageMarker = 0;//消息标记0

    // 设置回调函数和接收发送id
    instance->fdcan_handle = config->fdcan_handle;
    instance->tx_id = config->tx_id; // 好像没用,可以删掉
    instance->rx_id = config->rx_id;
    instance->fdcan_module_callback = config->fdcan_module_callback;
    instance->id = config->id;

    FDCANAddFilter(instance);         // 添加FDCAN过滤器规则
    fdcan_instance[idx++] = instance; // 将实例保存到fdcan_instance中

    return instance; // 返回fdcan实例指针
}

/* @todo 目前似乎封装过度,应该添加一个指向tx_buff的指针,tx_buff不应该由FDCAN instance保存 */
/* 如果让FDCANinstance保存txbuff,会增加一次复制的开销 */
uint8_t FDCANTransmit(FDCANInstance *_instance, float timeout)
{
    static uint32_t busy_count;
    static volatile float wait_time __attribute__((unused)); // for fdcancel warning
    // float dwt_start = DWT_GetTimeline_ms();
    while (HAL_FDCAN_GetTxMailboxesFreeLevel(_instance->fdcan_handle) == 0) // 等待邮箱空闲
    {
        // if (DWT_GetTimeline_ms() - dwt_start > timeout) // 超时
        // {
        //     LOGWARNING("[bsp_fdcan] FDCAN MAILbox full! failed to add msg to mailbox. Cnt [%d]", busy_count);
        //     busy_count++;
        //     return 0;
        // }
    }
    // wait_time = DWT_GetTimeline_ms() - dwt_start;
    // tx_mailbox会保存实际填入了这一帧消息的邮箱,但是知道是哪个邮箱发的似乎也没啥用
    if (HAL_FDCAN_AddTxMessage(_instance->fdcan_handle, &_instance->txconf, _instance->tx_buff, &_instance->tx_mailbox))
    {
        // LOGWARNING("[bsp_fdcan] FDCAN bus BUS! cnt:%d", busy_count);
        busy_count++;
        return 0;
    }
    return 1; // 发送成功
}

void FDCANSetDLC(FDCANInstance *_instance, uint8_t length)
{
    // 发送长度错误!检查调用参数是否出错,或出现野指针/越界访问
    if (length > 8 || length == 0) // 安全检查
        while (1)
            // LOGERROR("[bsp_fdcan] FDCAN DLC error! check your code or wild pointer");
    _instance->txconf.DataLength = length;
}

/* -----------------------belows are callback definitions--------------------------*/

/**
 * @brief 此函数会被下面两个函数调用,用于处理FIFO0和FIFO1溢出中断(说明收到了新的数据)
 *        所有的实例都会被遍历,找到fdcan_handle和rx_id相等的实例时,调用该实例的回调函数
 *
 * @param _hfdcan
 * @param fifox passed to HAL_FDCAN_GetRxMessage() to get mesg from a specific fifo
 */
static void FDCANFIFOxCallback(FDCAN_HandleTypeDef *_hfdcan, uint32_t fifox)
{
    static FDCAN_RxHeaderTypeDef rxconf; // 同上
    uint8_t fdcan_rx_buff[8];
    while (HAL_FDCAN_GetRxFifoFillLevel(_hfdcan, fifox)) // FIFO不为空,有可能在其他中断时有多帧数据进入
    {
        HAL_FDCAN_GetRxMessage(_hfdcan, fifox, &rxconf, fdcan_rx_buff); // 从FIFO中获取数据
        for (size_t i = 0; i < idx; ++i)
        { // 两者相等说明这是要找的实例
            if (_hfdcan == fdcan_instance[i]->fdcan_handle && rxconf.Identifier == fdcan_instance[i]->rx_id)
            {
                if (fdcan_instance[i]->fdcan_module_callback != NULL) // 回调函数不为空就调用
                {
                    fdcan_instance[i]->rx_len = rxconf.DataLength;                      // 保存接收到的数据长度
                    memcpy(fdcan_instance[i]->rx_buff, fdcan_rx_buff, rxconf.DataLength); // 消息拷贝到对应实例
                    fdcan_instance[i]->fdcan_module_callback(fdcan_instance[i]);     // 触发回调进行数据解析和处理
                }
                return;
            }
        }
    }
}

/**
 * @brief 注意,STM32的两个FDCAN设备共享两个FIFO
 * 下面两个函数是HAL库中的回调函数,他们被HAL声明为__weak,这里对他们进行重载(重写)
 * 当FIFO0或FIFO1溢出时会调用这两个函数
 */
// 下面的函数会调用FDCANFIFOxCallback()来进一步处理来自特定FDCAN设备的消息

/**
 * @brief rx fifo callback. Once FIFO_0 is full,this func would be called
 *
 * @param hfdcan FDCAN handle indicate which device the oddest mesg in FIFO_0 comes from
 */
void HAL_FDCAN_RxFifo0MsgPendingCallback(FDCAN_HandleTypeDef *hfdcan)
{
    FDCANFIFOxCallback(hfdcan, FDCAN_RX_FIFO0); // 调用我们自己写的函数来处理消息
}

/**
 * @brief rx fifo callback. Once FIFO_1 is full,this func would be called
 *
 * @param hfdcan FDCAN handle indicate which device the oddest mesg in FIFO_1 comes from
 */
void HAL_FDCAN_RxFifo1MsgPendingCallback(FDCAN_HandleTypeDef *hfdcan)
{
    FDCANFIFOxCallback(hfdcan, FDCAN_RX_FIFO1); // 调用我们自己写的函数来处理消息
}


