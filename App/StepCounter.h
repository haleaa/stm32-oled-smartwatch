#ifndef __STEPCOUNTER_H
#define __STEPCOUNTER_H

#include <stdint.h>

/*
 * 步数记录模块（MPU6050 六轴）
 * 采用主流手表的“底层粗计步 + 上层二次校验”两级架构：
 *   底层：合加速度 + 滑动平均 + 动态基线阈值 + 峰谷配对  -> 输出候选步
 *   上层：步频校验 + 幅值校验 + 连续步状态机 + 陀螺仪辅助 -> 输出最终有效步数
 * 采样由 TIM4 提供 50Hz 节拍，实际读取与运算在主循环 StepCounter_Update() 中完成，
 * 因此计步在任意页面都会后台持续进行，步数页面只负责显示累计结果。
 */

/* 计步状态机状态（对外可用于 UI 显示） */
typedef enum
{
	Step_State_Still = 0,   // 静止态：未检测到连续步态
	Step_State_Candidate,   // 候选态：已连续检测到有效候选步，尚未确认
	Step_State_Walking      // 计步态：已确认为连续步态，逐步累加
} Step_State_t;

/* 初始化：配置 MPU6050 为计步最优参数 + 复位状态机 + 启动 TIM4(50Hz) 采样节拍 */
void StepCounter_Init(void);

/* 主循环轮询：到达新的 50Hz 采样点时读取传感器并执行两级计步算法 */
void StepCounter_Update(void);

/* 获取最终有效步数 */
uint32_t StepCounter_GetSteps(void);

/* 获取当前计步状态 */
Step_State_t StepCounter_GetState(void);

/* 清零步数并复位状态机与滤波器 */
void StepCounter_Reset(void);

/* 步数页面 UI 绘制（在菜单 FEATURE_STEPS 中调用） */
void StepCounter_Show_UI(void);

/* 步数页面按键处理（在菜单 FEATURE_STEPS 中调用） */
void StepCounter_Switch(void);

#endif
