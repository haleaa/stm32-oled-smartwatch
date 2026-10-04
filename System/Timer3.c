#include "stm32f10x.h"                  // Device header 
#include "StopWatch.h"

void Timer3_Init(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE); // RCC时钟使能
	
	TIM_InternalClockConfig(TIM3); // 设置内部时钟
	
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 指定时钟分频值
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; // 计数模式设置为向上计数
	TIM_TimeBaseInitStructure.TIM_Period = 10000 - 1; // ARR: 10ms @ 1MHz
	TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1; // PSC: 72MHz/72 = 1MHz
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0; // 重复计数器的数值
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);
	
	TIM_ClearFlag(TIM3, TIM_FLAG_Update); // 清除初始化导致的中断标志位
	
	TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE); // 设置定时器中断
	
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); // NVIC优先级分组配置
	
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn; // 设置中断通道
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; // 对中断通道使能
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2; // 设置抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1; // 设置响应优先级
	NVIC_Init(&NVIC_InitStructure);
	
	TIM_Cmd(TIM3, ENABLE); // 启动定时器
}

void TIM3_IRQHandler(void)
{
	if(TIM_GetFlagStatus(TIM3, TIM_FLAG_Update) == SET)
	{
		StopWatch_Time_Update();
		TIM_ClearITPendingBit(TIM3, TIM_FLAG_Update);
	}
}
