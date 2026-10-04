#include "stm32f10x.h"                  // Device header
#include "OLED.h"
#include "Key.h"
#include "Menu.h"

#define MENU_HOME   	0

#define BUTTON_Y 		46
#define START_X 		8
#define PAUSE_X 		48
#define CLEAR_X			88
#define TIME_X 			16
#define TIME_Y			20 
#define CHINESE_SIZE 	16

typedef enum {
    STOPWATCH_STOP = 0,    // Í£Ö¹/¹éÁã×´Ì¬
    STOPWATCH_RUNNING,     // ¼ÆÊ±ÔËÐÐÖÐ
    STOPWATCH_PAUSED       // ÔÝÍ£×´Ì¬
} StopWatch_State_t;
StopWatch_State_t StopWatch_State = STOPWATCH_STOP;

typedef enum {
	FOCUS_BACK = 0,
    FOCUS_START,
    FOCUS_PAUSE,
    FOCUS_CLEAR,
    FOCUS_COUNT
} Focus_Index_t;
Focus_Index_t Focus_Index = FOCUS_BACK;

static uint8_t Min, Sec, Centisec;

static uint8_t KeyNum;

void StopWatch_Time_Update(void)
{
	if (StopWatch_State != STOPWATCH_RUNNING) return;
	Centisec ++;
}

void StopWatch_Time_Calculate(void)
{
	if (Centisec >= 100)
	{
		Centisec = 0;
		Sec ++;
	}
	
	if (Sec >= 60)
	{
		Sec = 0;
		Min ++;
	}
	
	Min %= 100;
}

static void StopWatch_Focus_Display(void)
{
	switch (Focus_Index)
	{
		case FOCUS_BACK:
			OLED_ReverseArea(0, 0, 16, 16);
			break;
		
		case FOCUS_START:
			OLED_ReverseArea(START_X, BUTTON_Y, 2 * CHINESE_SIZE, CHINESE_SIZE);
			break;
		
		case FOCUS_PAUSE:
			OLED_ReverseArea(PAUSE_X, BUTTON_Y, 2 * CHINESE_SIZE, CHINESE_SIZE);
			break;
		
		case FOCUS_CLEAR:
			OLED_ReverseArea(CLEAR_X, BUTTON_Y, 2 * CHINESE_SIZE, CHINESE_SIZE);
			break;
		
		default:
			Focus_Index = FOCUS_BACK;
			break;
	}
}

void StopWatch_Show_UI(void)
{
	OLED_Clear();
	OLED_ShowImage(0, 0, 16, 16, Back);
	OLED_Printf(TIME_X, TIME_Y, OLED_12X24,"%02d:%02d:%02d", Min, Sec, Centisec);
	OLED_Printf(START_X, BUTTON_Y, OLED_8X16, "¿ªÊ¼");
	OLED_Printf(PAUSE_X, BUTTON_Y, OLED_8X16, "ÔÝÍ£");
	OLED_Printf(CLEAR_X, BUTTON_Y, OLED_8X16, "ÇåÁã");
	
	StopWatch_Focus_Display();
}

void StopWatch_Switch(void)
{
	KeyNum = Key_GetNum();
	
	if (KeyNum == 1)
	{
		Focus_Index++;
	}
	else if (KeyNum == 2)
	{
		Focus_Index--;
	}
	else if (KeyNum == 3)
	{
		switch (Focus_Index)
		{
			case FOCUS_BACK:
				Menu_SetFeatPage(MENU_HOME);
				break;
			
			case FOCUS_START:
				StopWatch_State = STOPWATCH_RUNNING;
				break;
			
			case FOCUS_PAUSE:
				StopWatch_State = STOPWATCH_PAUSED;
				break;
			
			case FOCUS_CLEAR:
				StopWatch_State = STOPWATCH_STOP;
				Min = 0; Sec = 0; Centisec = 0;
				break;
			
			default:
				Focus_Index = FOCUS_BACK;
				break;
		}
	}
	
	Focus_Index = (Focus_Index_t)((Focus_Index + FOCUS_COUNT) % FOCUS_COUNT);
}
