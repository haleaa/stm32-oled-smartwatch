#include "stm32f10x.h"                  // Device header
#include "Menu.h"

void Timer_Init(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);//rcc时钟使能
	
	TIM_InternalClockConfig(TIM2);//设置内部时钟
	
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; //指定时钟分频值
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; //计数模式设置为向上计数
	TIM_TimeBaseInitStructure.TIM_Period = 20000 - 1; //ARR自动重载寄存器的周期数值
	TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1; //定时器（TIM）时钟进行分频的预分频器数值
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0; //重复计数器的数值
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);
	
	TIM_ClearFlag(TIM2, TIM_FLAG_Update); //清除初始化导致的中断标志位
	
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);//设置定时器中断
	
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); //NVIC 优先级分组配置
	
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn; //设置中断通道
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; //对中断通道使能
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2; //设置抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1; //设置响应优先级
	NVIC_Init(&NVIC_InitStructure);
	
	TIM_Cmd(TIM2, ENABLE); //启动定时器
}

void TIM2_IRQHandler(void)
{
	if(TIM_GetFlagStatus(TIM2, TIM_FLAG_Update) == SET)
	{
		Menu_AnimUpdate();
		TIM_ClearITPendingBit(TIM2, TIM_FLAG_Update);
	}
}
