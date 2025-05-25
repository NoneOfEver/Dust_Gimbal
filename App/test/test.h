#ifndef Test_H
#define Test_H

#define ROBOT_TEST
/**
 * @brief 初始化机械臂,会被RobotInit()调用
 * 
 */
void selfTestInit();

/**
 * @brief 机械臂任务
 * 
 */
void selfTestTask();

#endif // !Test_H