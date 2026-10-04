#include "stm32f10x.h"                  // Device header
#include "OLED.h"
#include "Key.h"
#include "Page.h"
#include "MyRTC.h"
#include "Cursor.h"

//UI状态枚举的设计原则是：正交、互斥、可扩展
//先想好有几个界面，然后每一个界面一个枚举

typedef enum {
    PAGE_SETTING_MENU = 0,
    PAGE_SETTING_DATE,
    PAGE_SETTING_TIME,
} setting_page_t;
setting_page_t Sett_Page_State;
static setting_page_t last_Sett_Page_State = (setting_page_t)0xFF;

/* 2. 每个页面的焦点用独立的、小范围的enum或宏 */
/* 日期页焦点 */
typedef enum {
	DATE_FOCUS_BACK = 0,
    DATE_FOCUS_YEAR,
    DATE_FOCUS_MONTH,
    DATE_FOCUS_DAY,
	DATE_FOCUS_NEXT,
    DATE_FOCUS_COUNT      // 哨兵值，表示总数，方便边界判断
	
} date_focus_t;
date_focus_t Date_Focus_State;

static date_focus_t last_Date_Focus_State = (date_focus_t)0xFF;
static uint8_t last_Day = 0xFF;

/* 时间页焦点 */
typedef enum {
	TIME_FOCUS_BACK = 0,
    TIME_FOCUS_HOUR,
    TIME_FOCUS_MIN,
    TIME_FOCUS_SEC,
    TIME_FOCUS_COUNT
} time_focus_t;

time_focus_t Time_Focus_State;

static time_focus_t last_Time_Focus_State = (time_focus_t)0xFF;

/* 3. 菜单页焦点 */
typedef enum {
    MENU_FOCUS_BACK = 0,
	MENU_FOCUS_DATETIMESETT,
    MENU_FOCUS_COUNT
} menu_focus_t;
menu_focus_t Menu_Focus_State;
static menu_focus_t last_Menu_Focus_State = (menu_focus_t)0xFF;

static uint8_t Blink_Flag = 0;

static uint8_t Month_Len, Day_Len;

// 判断闰年：能被4整除但不能被100整除，或能被400整除
uint8_t Is_Leap_Year(uint16_t year)
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
}

// 根据年月获取当月最大天数
uint8_t Get_Month_Max_Day(uint16_t year, uint8_t month)
{
    switch (month)
    {
    case 1: case 3: case 5: case 7: case 8: case 10: case 12:
        return 31;
    case 4: case 6: case 9: case 11:
        return 30;
    case 2:
        return Is_Leap_Year(year) ? 29 : 28;
    default:
        return 31; // 兜底保护
    }
}

void Setting_Show_Menu_UI(void)
{
	OLED_ShowImage(0, 0, 16, 16, Back);
	OLED_Printf(0, 16, OLED_8X16, "日期时间设置");
	
	switch (Menu_Focus_State)
	{
		case MENU_FOCUS_BACK:
			OLED_ReverseArea(0, 0, 16, 16);
			break;
		
		case MENU_FOCUS_DATETIMESETT:
			OLED_ReverseArea(0, 16, 128, 16);
			break;

		default:
			Menu_Focus_State = MENU_FOCUS_BACK;
			break;
	}
	
	if (Menu_Focus_State != MENU_FOCUS_DATETIMESETT) OLED_ClearArea(6*16, 16, 128 - 6*16, 16);
}

void Setting_Menu_Switch(void)
{
	uint8_t KeyNum;
	KeyNum = Key_GetNum();
	
	if (KeyNum == 1) //PB1释放 +
	{
		Menu_Focus_State ++;
	}
	else if (KeyNum == 2)  //-
	{
		Menu_Focus_State --;
	}
	else if (KeyNum == 3)  //确定
	{
		switch (Menu_Focus_State)
		{
			case MENU_FOCUS_BACK:
				Page_Mode = Page_Time;
				break;

			case MENU_FOCUS_DATETIMESETT:
				Sett_Page_State = PAGE_SETTING_DATE;
				break;

			default:
				// 兜底，防止变量乱掉，切回首页
				Menu_Focus_State = MENU_FOCUS_BACK;
				break;
		}
	}

	if (Menu_Focus_State >= MENU_FOCUS_COUNT) Menu_Focus_State = MENU_FOCUS_BACK;
}

void Setting_Menu_Main(void)
{
	Setting_Menu_Switch();
	
	if (last_Menu_Focus_State != Menu_Focus_State)
	{
		Setting_Show_Menu_UI();
		last_Menu_Focus_State = Menu_Focus_State;
	}
	
	if (Page_Mode != Page_Setting) last_Menu_Focus_State = (menu_focus_t)0xFF;
}

void Setting_Show_Date_UI(void)
{	
	if (MyRTC_TimeArray[1] <= 0) MyRTC_TimeArray[1] = 12;
	if (MyRTC_TimeArray[1] > 12) MyRTC_TimeArray[1] = 1;
	
	if (MyRTC_TimeArray[2] <= 0) MyRTC_TimeArray[2] = Get_Month_Max_Day(MyRTC_TimeArray[0], MyRTC_TimeArray[1]);
	if (MyRTC_TimeArray[2] > Get_Month_Max_Day(MyRTC_TimeArray[0], MyRTC_TimeArray[1]))
		MyRTC_TimeArray[2] = 1;
	
	if (MyRTC_TimeArray[1] < 10) Month_Len = 1; else Month_Len = 2;
	if (MyRTC_TimeArray[2] < 10) Day_Len = 1; else Day_Len = 2;
	
	if (last_Day != MyRTC_TimeArray[2])
	{
		OLED_Printf(28, 24, OLED_8X16, "%d-%d-%d", MyRTC_TimeArray[0], MyRTC_TimeArray[1], MyRTC_TimeArray[2]);
		last_Day = MyRTC_TimeArray[2];
	}
	
	if (last_Date_Focus_State != Date_Focus_State)
	{
		last_Date_Focus_State = Date_Focus_State;
		
		OLED_Clear();
		
		OLED_ShowImage(0, 0, 16, 16, Back);
		OLED_ShowImage(112, 48, 16, 16, Next);
		
		OLED_Printf(28, 24, OLED_8X16, "%d-%d-%d", MyRTC_TimeArray[0], MyRTC_TimeArray[1], MyRTC_TimeArray[2]);
		
		switch (Date_Focus_State)
		{
			case DATE_FOCUS_BACK:
				OLED_ReverseArea(0, 0, 16, 16);
				break;
			
			case DATE_FOCUS_YEAR:
				if (Blink_Flag == 0)
				{
					OLED_ReverseArea(28, 24, 4*8, 16);
					Cursor_Clear_Blink();
				}
				else if (Blink_Flag == 1)
				{
					Cursor_Blink(28, 24, 4*8, 16);
				}
				break;
			
			case DATE_FOCUS_MONTH:
				if (Blink_Flag == 0)
				{
					OLED_ReverseArea(68, 24, Month_Len*8, 16);
					Cursor_Clear_Blink();
				}
				else if (Blink_Flag == 1)
				{
					Cursor_Blink(68, 24, Month_Len*8, 16);
				}
				break;
			
			case DATE_FOCUS_DAY:
				if (Blink_Flag == 0)
				{
					OLED_ReverseArea(76 + Month_Len*8, 24, Day_Len*8, 16);
					Cursor_Clear_Blink();
				}
				else if (Blink_Flag == 1)
				{
					Cursor_Blink(76 + Month_Len*8, 24, Day_Len*8, 16);
				}
				break;
			
			case DATE_FOCUS_NEXT:
				OLED_ReverseArea(112, 48, 16, 16);
				break;

			default:
				Date_Focus_State = DATE_FOCUS_BACK;
				break;
		}
		
	}
}

void Setting_Date_Switch(void)
{
	uint8_t KeyNum;
	KeyNum = Key_GetNum();
	
	if (KeyNum == 1 && Blink_Flag != 1) //PB1释放 +
	{
		Date_Focus_State ++;
	}
	else if (KeyNum == 2 && Blink_Flag != 1)  //-
	{
		Date_Focus_State --;
	}
	else if (KeyNum == 3)  //确定
	{
		switch (Date_Focus_State)
		{
			case DATE_FOCUS_BACK:
				Sett_Page_State = PAGE_SETTING_MENU;
				Menu_Focus_State = MENU_FOCUS_BACK;
				break;

			case DATE_FOCUS_YEAR:
				Blink_Flag = !Blink_Flag;
				last_Date_Focus_State = (date_focus_t)0xFF;
				break;
			
			case DATE_FOCUS_MONTH:
				Blink_Flag = !Blink_Flag;
				last_Date_Focus_State = (date_focus_t)0xFF;
				break;
			
			case DATE_FOCUS_DAY:
				Blink_Flag = !Blink_Flag;
				last_Date_Focus_State = (date_focus_t)0xFF;
				break;
			
			case DATE_FOCUS_NEXT:
				Sett_Page_State = PAGE_SETTING_TIME;
				break;

			default:
				// 兜底，防止变量乱掉，切回首页
				Date_Focus_State = DATE_FOCUS_BACK;
				break;
		}
	}

	if (Date_Focus_State >= DATE_FOCUS_COUNT) Date_Focus_State = DATE_FOCUS_BACK;
}

void Setting_Set_Date(void)
{
	uint8_t KeyNum;
	KeyNum = Key_GetNum();
	
	if (Blink_Flag == 1)
	{
		if (KeyNum == 1)
		{
			switch (Date_Focus_State)
			{
				case DATE_FOCUS_YEAR:
					MyRTC_TimeArray[0] ++;
					last_Date_Focus_State = (date_focus_t)0xFF;
					break;
				
				case DATE_FOCUS_MONTH:
					MyRTC_TimeArray[1] ++;
					last_Date_Focus_State = (date_focus_t)0xFF;
					break;
				
				case DATE_FOCUS_DAY:
					MyRTC_TimeArray[2] ++;
					last_Date_Focus_State = (date_focus_t)0xFF;
					break;
				
				default:
					// 兜底，防止变量乱掉，切回首页
					Date_Focus_State = DATE_FOCUS_BACK;
					break;
			}
		}
		
		if (KeyNum == 2)
		{
			switch (Date_Focus_State)
			{
				case DATE_FOCUS_YEAR:
					MyRTC_TimeArray[0] --;
					last_Date_Focus_State = (date_focus_t)0xFF;
					break;
				
				case DATE_FOCUS_MONTH:
					MyRTC_TimeArray[1] --;
					last_Date_Focus_State = (date_focus_t)0xFF;
					break;
				
				case DATE_FOCUS_DAY:
					MyRTC_TimeArray[2] --;
					last_Date_Focus_State = (date_focus_t)0xFF;
					break;
				
				default:
					// 兜底，防止变量乱掉，切回首页
					Date_Focus_State = DATE_FOCUS_BACK;
					break;
			}
		}
		
		if (KeyNum == 3)
		{
			switch (Date_Focus_State)
			{
				case DATE_FOCUS_YEAR:
					MyRTC_SetTime();
					last_Date_Focus_State = (date_focus_t)0xFF;
					last_Day = 0xFF;
					break;
				
				case DATE_FOCUS_MONTH:
					MyRTC_SetTime();
					last_Date_Focus_State = (date_focus_t)0xFF;
					last_Day = 0xFF;
					break;
				
				case DATE_FOCUS_DAY:
					MyRTC_SetTime();
					last_Date_Focus_State = (date_focus_t)0xFF;
					last_Day = 0xFF;
					break;
				
				default:
					// 兜底，防止变量乱掉，切回首页
					Date_Focus_State = DATE_FOCUS_BACK;
					break;
			}
		}
	}
}

void Setting_Date_Main(void)
{	
	Setting_Set_Date();
	Setting_Date_Switch();
	Setting_Show_Date_UI();
	
	if (Sett_Page_State != PAGE_SETTING_DATE) last_Date_Focus_State = (date_focus_t)0xFF; //退出界面重置脏标记
}

void Setting_Show_Time_UI(void)
{	
	MyRTC_TimeArray[3] = ((int16_t)MyRTC_TimeArray[3] % 24 + 24) % 24;
	//当小时要为0再减1的时候，把它强转为int16_t变为-1，然后再取余24保证在-23 到 23的区间，再加24转化为23就可以实现0再减一位得到最大值了
	
	MyRTC_TimeArray[4] = ((int16_t)MyRTC_TimeArray[4] % 60 + 60) % 60;
	
	MyRTC_TimeArray[5] = ((int16_t)MyRTC_TimeArray[5] % 60 + 60) % 60;
	
	
	if (last_Time_Focus_State != Time_Focus_State)
	{
		last_Time_Focus_State = Time_Focus_State;
		
		OLED_Clear();
		
		OLED_ShowImage(0, 0, 16, 16, Back);
		
		OLED_Printf(32, 24, OLED_8X16, "%02d:%02d:%02d", MyRTC_TimeArray[3], MyRTC_TimeArray[4], MyRTC_TimeArray[5]);

		switch (Time_Focus_State)
		{
			case TIME_FOCUS_BACK:
				OLED_ReverseArea(0, 0, 16, 16);
				break;
			
			case TIME_FOCUS_HOUR:
				if (Blink_Flag == 0)
				{
					OLED_ReverseArea(32, 24, 2*8, 16);
					Cursor_Clear_Blink();
				}
				else if (Blink_Flag == 1)
				{
					Cursor_Blink(32, 24, 2*8, 16);
				}
				break;
			
			case TIME_FOCUS_MIN:
				if (Blink_Flag == 0)
				{
					OLED_ReverseArea(32 + 3*8, 24, 2*8, 16);
					Cursor_Clear_Blink();
				}
				else if (Blink_Flag == 1)
				{
					Cursor_Blink(32 + 3*8, 24, 2*8, 16);
				}
				break;
			
			case TIME_FOCUS_SEC:
				if (Blink_Flag == 0)
				{
					OLED_ReverseArea(32 + 6*8, 24, 2*8, 16);
					Cursor_Clear_Blink();
				}
				else if (Blink_Flag == 1)
				{
					Cursor_Blink(32 + 6*8, 24, 2*8, 16);
				}
				break;

			default:
				Time_Focus_State = TIME_FOCUS_BACK;
				break;
		}
		
	}
}

void Setting_Time_Switch(void)
{
	uint8_t KeyNum;
	KeyNum = Key_GetNum();
	
	if (KeyNum == 1 && Blink_Flag != 1) //PB1释放 +
	{
		Time_Focus_State ++;
	}
	else if (KeyNum == 2 && Blink_Flag != 1)  //-
	{
		Time_Focus_State --;
	}
	else if (KeyNum == 3)  //确定
	{
		switch (Time_Focus_State)
		{
			case TIME_FOCUS_BACK:
				Sett_Page_State = PAGE_SETTING_DATE;
				Date_Focus_State = DATE_FOCUS_BACK;
				break;

			case TIME_FOCUS_HOUR:
				Blink_Flag = !Blink_Flag;
				last_Time_Focus_State = (time_focus_t)0xFF;
				break;
			
			case TIME_FOCUS_MIN:
				Blink_Flag = !Blink_Flag;
				last_Time_Focus_State = (time_focus_t)0xFF;
				break;
			
			case TIME_FOCUS_SEC:
				Blink_Flag = !Blink_Flag;
				last_Time_Focus_State = (time_focus_t)0xFF;
				break;

			default:
				// 兜底，防止变量乱掉，切回首页
				Time_Focus_State = TIME_FOCUS_BACK;
				break;
		}
	}

	if (Time_Focus_State >= TIME_FOCUS_COUNT) Time_Focus_State = TIME_FOCUS_BACK;
}

void Setting_Set_Time(void)
{
	uint8_t KeyNum;
	KeyNum = Key_GetNum();
	
	if (Blink_Flag == 1)
	{
		if (KeyNum == 1)
		{
			switch (Time_Focus_State)
			{
				case TIME_FOCUS_HOUR:
					MyRTC_TimeArray[3] ++;
					last_Time_Focus_State = (time_focus_t)0xFF;
					break;
				
				case TIME_FOCUS_MIN:
					MyRTC_TimeArray[4] ++;
					last_Time_Focus_State = (time_focus_t)0xFF;
					break;
				
				case TIME_FOCUS_SEC:
					MyRTC_TimeArray[5] ++;
					last_Time_Focus_State = (time_focus_t)0xFF;
					break;
				
				default:
					// 兜底，防止变量乱掉，切回首页
					Time_Focus_State = TIME_FOCUS_BACK;
					break;
			}
		}
		
		if (KeyNum == 2)
		{
			switch (Time_Focus_State)
			{
				case TIME_FOCUS_HOUR:
					MyRTC_TimeArray[3] --;
					last_Time_Focus_State = (time_focus_t)0xFF;
					break;
				
				case TIME_FOCUS_MIN:
					MyRTC_TimeArray[4] --;
					last_Time_Focus_State = (time_focus_t)0xFF;
					break;
				
				case TIME_FOCUS_SEC:
					MyRTC_TimeArray[5] --;
					last_Time_Focus_State = (time_focus_t)0xFF;
					break;
				
				default:
					// 兜底，防止变量乱掉，切回首页
					Time_Focus_State = TIME_FOCUS_BACK;
					break;
			}
		}
		
		if (KeyNum == 3)
		{
			switch (Time_Focus_State)
			{
				case TIME_FOCUS_HOUR: case TIME_FOCUS_MIN: case TIME_FOCUS_SEC:
					MyRTC_SetTime();
					last_Time_Focus_State = (time_focus_t)0xFF;
					break;
				
				default:
					// 兜底，防止变量乱掉，切回首页
					Time_Focus_State = TIME_FOCUS_BACK;
					break;
			}
		}
	}
}

void Setting_Time_Main(void)
{	
	Setting_Set_Time();
	Setting_Time_Switch();
	Setting_Show_Time_UI();
	
	if (Sett_Page_State != PAGE_SETTING_TIME) last_Time_Focus_State = (time_focus_t)0xFF; //退出界面重置脏标记
}

void Setting_Page_Switch(void)
{	
	switch (Sett_Page_State)
	{
		case PAGE_SETTING_MENU:
			if (last_Sett_Page_State != Sett_Page_State)
			{
				OLED_Clear();
				last_Sett_Page_State = Sett_Page_State;
			}
			
			Setting_Menu_Main();
			break;
		
		case PAGE_SETTING_DATE:
			if (last_Sett_Page_State != Sett_Page_State)
			{
				OLED_Clear();
				last_Sett_Page_State = Sett_Page_State;
			}
			Setting_Date_Main();
			break;
			
		case PAGE_SETTING_TIME:
			if (last_Sett_Page_State != Sett_Page_State)
			{
				OLED_Clear();
				last_Sett_Page_State = Sett_Page_State;
			}
			Setting_Time_Main();
			break;
		
		default:
			Sett_Page_State = PAGE_SETTING_MENU;
			break;
	}
}

//typedef定义的结构体不能在定义的时候赋值

//切换界面之前要调用一下清屏函数

//遇到bug:在函数Setting_Menu_Switch里面定义menu_focus_t Menu_Focus_State;导致一直卡在初始值
//解决方法，定义枚举时不要定义在函数里面局部变量会导致一直被赋初值
//原因是因为局部变量在函数退出之后就会被清空，定义全局变量或者用static定义静态变量解决

//bug:光标闪烁时按键无法正常使用，因为keygetnum函数只能被读一次
//解决:用的帧同步和帧缓存来解决按键只能读取一次的问题

//bug：对月份加减处于9到10的时候10不会被判定为闪烁两格
//解决：计算长度和设置边界要设置在设置日期之后和UI显示之前

//小bug:如果先修改时间再修改日期，修改日期时会让时间计时停住，从而导致时间偏差
