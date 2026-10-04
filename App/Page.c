#include "stm32f10x.h"                  // Device header
#include "Page.h"
#include "OLED.h"
#include "Home.h"
#include "Setting.h"
#include "Menu.h"

PAGE_STATE Page_Mode;
PAGE_STATE last_Page_Mode = (PAGE_STATE)0xFF; // 强制首次刷新

static void Page_Clear(void)
{
	if (last_Page_Mode != Page_Mode)
	{
		OLED_Clear();
		last_Page_Mode = Page_Mode;
	}
}

void Page_Switch(void)
{
	switch(Page_Mode)
	{
		case Page_Time:
			Page_Clear();
			HomePageOperate();
			Home_Show_Clock_UI();
			break;
			
		case Page_Setting:
			Page_Clear();
			Setting_Page_Switch();
			break;
		
		case Page_Menu:
			Page_Clear();
//			Menu_Switch();
//			Menu_Show_UI();
			Menu_FeatPageSwitch();
			break;
			
		default:
			Page_Mode = Page_Time; //非法状态兜底切回时钟页
			break;
	}
}

//BUG：之前界面没变化的时候还一直在循环调用oled_clear导致光标闪烁写不出来，并且这样提高了代码效率
//使只有界面要变化时才执行清屏函数
