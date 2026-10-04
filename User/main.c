#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "Home.h"
#include "SysTick.h"
#include "Key.h"
#include "Page.h"
#include "Menu.h"
#include "Timer.h"
#include "Timer3.h"
#include "StepCounter.h"
#include "MAX30102.h"
#include "Bluetooth.h"

int main(void)
{
	/*OLED初始化*/
	SysTick1ms_Init();
	OLED_Init();
	Home_Init();
	Key_Init();
	Menu_Init();
	Timer_Init();
	Timer3_Init();
	StepCounter_Init();
	MAX30102_Init();
	BLE_Init();
	
	while (1)
	{
		Key_SyncFrame(); // 每帧开头同步一次按键，必须放在所有业务之前
		BLE_RefreshData();
		Page_Switch();
		StepCounter_Update();
		OLED_Update();
	}
}
