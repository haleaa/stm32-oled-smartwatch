#include "stm32f10x.h"                  // Device header
#include "MyRTC.h"
#include "OLED.h"
#include "Key.h"
#include "Page.h"

typedef enum
{
	Home_Start = 0,
    Home_Menu = 1,   
    Home_Setting
}HOME_STATE;

static HOME_STATE last_homeflag = (HOME_STATE)0xFF; // 强制首次刷新

HOME_STATE homeflag = Home_Start;
uint8_t StartFlag = 0;

static uint8_t KeyNum;

void Home_Init(void)
{
	MyRTC_Init();
}

void Home_Show_Clock_UI(void)
{
	MyRTC_ReadTime();
	
	OLED_Printf(0, 0, OLED_6X8, "%d-%d-%d", MyRTC_TimeArray[0], MyRTC_TimeArray[1], MyRTC_TimeArray[2]);

	OLED_Printf(16, 16, OLED_12X24, "%02d:%02d:%02d", MyRTC_TimeArray[3], MyRTC_TimeArray[4], MyRTC_TimeArray[5]);

	//%02d代表输出的整数至少占 2 位，不足 2 位时在左边补零
	//%[标志][宽度].[精度]类型
	
	if (homeflag != last_homeflag)
	{
		OLED_Printf(0, 48, OLED_8X16, "菜单");
		OLED_Printf(96, 48, OLED_8X16, "设置");
		
	//光标显示部分	
		
		switch (homeflag)
		{
			case Home_Start:
				
				break;
			
			case Home_Menu:
				OLED_ReverseArea(0, 48, 32, 16);
				break;

			case Home_Setting:
				OLED_ReverseArea(96, 48, 32, 16);
				break;

			default:
				// 兜底，防止变量乱掉，切回首页
				homeflag = Home_Menu;
				break;
		}
		last_homeflag = homeflag;
	}
	
	//其他界面切回来刷新显示
	if (last_Page_Mode != Page_Mode)
	{
		last_homeflag = (HOME_STATE)0xFF; // 强制刷新
	}
}

void HomePageOperate(void)
{
	KeyNum = Key_GetNum();
	
	if (KeyNum == 1) //PB1释放 +
	{
		homeflag ++;
		StartFlag = 1;
	}
	else if (KeyNum == 2)  //-
	{
		homeflag --;
		StartFlag = 1;
	}
	else if (KeyNum == 3)  //确定
	{
		switch (homeflag)
		{
			case Home_Start:
				
				break;
			case Home_Menu:
				Page_Mode = Page_Menu;
				break;

			case Home_Setting:
				Page_Mode = Page_Setting;
				break;

			default:
				// 兜底，防止变量乱掉，切回首页
				homeflag = Home_Menu;
				break;
		}
	}
	
	if (homeflag > Home_Setting) homeflag = Home_Menu;
	
	if (homeflag < Home_Menu && StartFlag == 1) homeflag = Home_Setting;
	
}

//oled的显示函数每次执行前都会调用clear清空区域因此不需要手动在显示函数之前调用清空函数

//遇到bug，OLED_ReverseArea被循环调用导致光标一直闪烁
//解决方法：利用脏标记，只在状态发生变化时刷新1次
