#ifndef __WEATHER_H
#define __WEATHER_H

#include "Bluetooth.h"

typedef struct
{
	uint8_t Day;						// 第几天(1明天,2后天,3大后天)
	char Weather[WEATHER_STR_MAX];		// 天气状况
	int16_t Temp_High;					// 最高温
	int16_t Temp_Low;					// 最低温
	uint8_t Humidity;					// 湿度
}ForecastDay_t;

typedef struct
{
	char Weather[WEATHER_STR_MAX];		// 天气状况
	int16_t Temp_Current;				// 当前温度
	int16_t Temp_High;					// 最高温
	int16_t Temp_Low;					// 最低温
	uint8_t Humidity;					// 湿度
	ForecastDay_t Forecast[FORECAST_DAY_MAX];	// 各天预报数据
}Weather_Data_t;

void Weather_Data_Reflash(BLEParse_t* Result);
void Weather_Show_UI(void);
void Weather_Switch(void);

#endif
