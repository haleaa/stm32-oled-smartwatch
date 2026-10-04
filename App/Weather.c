#include "stm32f10x.h"                  // Device header
#include "OLED.h"
#include "Bluetooth.h"
#include "Weather.h"
#include "MyRTC.h"
#include "Key.h"
#include "Menu.h"
#include <string.h>

#define WEATHER_STR_MAX		16
#define FORECAST_DAY_MAX	3

#define MENU_HOME   		0

Weather_Data_t Weather_Data;

static uint8_t Week;
static uint8_t first_flag = 0;

typedef enum
{
	PAGE_CURRENT_WEATHER = 0,
	PAGE_FORECAST_WEATHER
}Weather_Page_t ;
Weather_Page_t Weather_Page = PAGE_CURRENT_WEATHER;

void Weather_Data_Reflash(BLEParse_t* Result)
{
	Weather_Data.Temp_Current = Result->Temp_Current;
	Weather_Data.Temp_High = Result->Temp_High;
	Weather_Data.Temp_Low = Result->Temp_Low;
	Weather_Data.Humidity = Result->Humidity;
	strncpy(Weather_Data.Weather, Result->Weather, sizeof(Weather_Data.Weather) - 1);
	Weather_Data.Weather[WEATHER_STR_MAX - 1] = '\0';
	
	for (uint8_t i = 0; i < FORECAST_DAY_MAX; i++)
	{
		Weather_Data.Forecast[i].Day = Result->Forecast[i].Day;
		Weather_Data.Forecast[i].Temp_High = Result->Forecast[i].Temp_High;
		Weather_Data.Forecast[i].Temp_Low = Result->Forecast[i].Temp_Low;
		Weather_Data.Forecast[i].Humidity = Result->Forecast[i].Humidity;
		strncpy(Weather_Data.Forecast[i].Weather, Result->Forecast[i].Weather, sizeof(Weather_Data.Forecast[i].Weather) - 1);
		Weather_Data.Forecast[i].Weather[WEATHER_STR_MAX - 1] = '\0';
	}
	
	first_flag = 0;  //蓝牙数据重新发送时更新一次记录的年月日
}

void Weather_DisplayJudge(void)
{
	if (!strcmp(Weather_Data.Weather, "SUNNY"))
	{
		OLED_ShowImage(16, 0, 32, 32, Sunny);
		OLED_Printf(48, 40, OLED_8X16, "晴");
	}
	else if (!strcmp(Weather_Data.Weather, "CLOUDY"))
	{
		OLED_ShowImage(16, 0, 32, 32, Cloudy);
		OLED_Printf(48, 40, OLED_8X16, "阴");
	}
	else if (!strcmp(Weather_Data.Weather, "OVERCAST"))
	{
		OLED_ShowImage(16, 0, 32, 32, Overcast);
		OLED_Printf(32, 40, OLED_8X16, "多云");
	}
	else if (!strcmp(Weather_Data.Weather, "RAINY"))
	{
		OLED_ShowImage(16, 0, 32, 32, Rain);
		OLED_Printf(48, 40, OLED_8X16, "雨");
	}
	else
	{
		OLED_Printf(0, 0, OLED_6X8, "Wait for BLE data upd");
		OLED_Printf(0, 8, OLED_6X8, "ate");
	}
}

void Forecast_DisplayJudge(void)
{
	for (uint8_t i = 0; i < FORECAST_DAY_MAX; i++)
	{
		if (!strcmp(Weather_Data.Forecast[i].Weather, "SUNNY"))
		{
			OLED_Printf(53, i*24, OLED_8X16, "晴");
			OLED_Printf(72, i*24, OLED_8X16, "%2d/%2d℃", Weather_Data.Forecast[i].Temp_High, Weather_Data.Forecast[i].Temp_Low);
		}
		else if (!strcmp(Weather_Data.Forecast[i].Weather, "CLOUDY"))
		{
			OLED_Printf(53, i*24, OLED_8X16, "阴");
			OLED_Printf(72, i*24, OLED_8X16, "%2d/%2d℃", Weather_Data.Forecast[i].Temp_High, Weather_Data.Forecast[i].Temp_Low);
		}
		else if (!strcmp(Weather_Data.Forecast[i].Weather, "OVERCAST"))
		{
			OLED_Printf(37, i*24, OLED_8X16, "多云");
			OLED_Printf(72, i*24, OLED_8X16, "%2d/%2d℃", Weather_Data.Forecast[i].Temp_High, Weather_Data.Forecast[i].Temp_Low);
		}
		else if (!strcmp(Weather_Data.Forecast[i].Weather, "RAINY"))
		{
			OLED_Printf(53, i*24, OLED_8X16, "雨");
			OLED_Printf(72, i*24, OLED_8X16, "%2d/%2d℃", Weather_Data.Forecast[i].Temp_High, Weather_Data.Forecast[i].Temp_Low);
		}
	}
}

void WeekJudge(void)
{
	Week = MyRTC_GetWeek();
	switch ((Week + 3) % 7)
	{
		case 0:
			OLED_Printf(0, 48, OLED_8X16, "周日");
			break;
		
		case 1:
			OLED_Printf(0, 48, OLED_8X16, "周一");
			break;
		
		case 2:
			OLED_Printf(0, 48, OLED_8X16, "周二");
			break;
		
		case 3:
			OLED_Printf(0, 48, OLED_8X16, "周三");
			break;
				
		case 4:
			OLED_Printf(0, 48, OLED_8X16, "周四");
			break;
				
		case 5:
			OLED_Printf(0, 48, OLED_8X16, "周五");
			break;
				
		case 6:
			OLED_Printf(0, 48, OLED_8X16, "周六");
			break;	
	}
}

void Weather_Show_UI(void)
{
	OLED_Clear();
	
	static uint8_t Last_Month, Last_Day;
	static uint16_t Last_Year;
	
	MyRTC_ReadTime();
	if (first_flag == 0)
    {
        // 第一次进来赋值不触发业务代码
        Last_Year = MyRTC_TimeArray[0];
		Last_Month = MyRTC_TimeArray[1];
		Last_Day = MyRTC_TimeArray[2];
        first_flag = 1;
    }
    else
    {
        // 不是第一次，才判断是否更新
        if (Last_Year != MyRTC_TimeArray[0] || Last_Month != MyRTC_TimeArray[1] || Last_Day != MyRTC_TimeArray[2])
        {
            OLED_Printf(0, 0, OLED_6X8, "Please Update Weather");
			return;
        }
		//还需添加数据更新逻辑（可以用软件记录标志位，也可以直接判断串口的硬件标志位？但感觉不是很严谨）
    }
	
	switch (Weather_Page)
	{
		case PAGE_CURRENT_WEATHER:
			Weather_DisplayJudge();
			if (Weather_Data.Weather[0] == '\0') return;
			OLED_Printf(0, 40, OLED_8X16, "%2d℃", Weather_Data.Temp_Current);
			OLED_Printf(64, 0, OLED_8X16, "最高%2d℃", Weather_Data.Temp_High);
			OLED_Printf(64, 24, OLED_8X16, "最低%2d℃", Weather_Data.Temp_Low);
			OLED_Printf(64, 48, OLED_8X16, "湿度%2d%%", Weather_Data.Humidity);
			break;
		
		case PAGE_FORECAST_WEATHER:
			if (Weather_Data.Forecast[0].Weather[0] == '\0') return;
			Forecast_DisplayJudge();
			OLED_Printf(0, 0, OLED_8X16, "明天");
			OLED_Printf(0, 24, OLED_8X16, "后天");
			WeekJudge();
			break;
	}
}

void Weather_Switch(void)
{
	uint8_t Key = 0;
	Key = Key_GetNum();
	if (Key == 3)
	{
		Menu_SetFeatPage(MENU_HOME);
	}
	else if (Key == 1)
	{
		Weather_Page ++;
	}
	else if (Key == 2)
	{
		Weather_Page --;
	}
	Weather_Page = (Weather_Page_t)((Weather_Page + 2) % 2);
}

//BUG:没加数据更新判断，导致只要过了一天后不复位就永远无法显示天气数据而显示Please Update Weather
