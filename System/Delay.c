#include "stm32f10x.h"

/**
  * @brief  微秒级延时
  * @param  xus 延时时长，范围：0~233015
  * @retval 无
  */
void Delay_us(uint32_t xus)
{
	/* 保存 SysTick 当前配置（1ms 系统滴答：HCLK/8、中断使能），
	   防止延时结束后把系统滴答永久关闭，导致 Key_Tick/Cursor_Tick 失效 */
	uint32_t saved_LOAD = SysTick->LOAD;
	uint32_t saved_CTRL = SysTick->CTRL;

	SysTick->LOAD = 72 * xus;				//设置定时器重装值
	SysTick->VAL = 0x00;					//清空当前计数值
	SysTick->CTRL = 0x00000005;				//设置时钟源为HCLK，启动定时器
	while(!(SysTick->CTRL & 0x00010000));	//等待计数到0
	SysTick->CTRL = 0x00000004;				//关闭定时器

	/* 恢复 SysTick 配置，重新启动 1ms 中断 */
	SysTick->LOAD = saved_LOAD;
	SysTick->VAL = 0x00;
	SysTick->CTRL = saved_CTRL;
}

/**
  * @brief  毫秒级延时
  * @param  xms 延时时长，范围：0~4294967295
  * @retval 无
  */
void Delay_ms(uint32_t xms)
{
	while(xms--)
	{
		Delay_us(1000);
	}
}
 
/**
  * @brief  秒级延时
  * @param  xs 延时时长，范围：0~4294967295
  * @retval 无
  */
void Delay_s(uint32_t xs)
{
	while(xs--)
	{
		Delay_ms(1000);
	}
} 
