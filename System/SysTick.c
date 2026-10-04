#include "stm32f10x.h"                  // Device header
#include "Key.h"
#include "Cursor.h"
#include "Menu.h"
#include "HeartRate.h"

void SysTick1ms_Init(void)
{
	//9000 tick =1ms��9Mʱ�ӣ�
    SysTick_Config(9000);
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK_Div8);
}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */

void SysTick_Handler(void)
{
	Key_Tick(); 
	Cursor_Tick();
	HeartRate_Tick(); //����ҳ�� 1ms ��ʱ������ 3 ��ˢ����������
}
