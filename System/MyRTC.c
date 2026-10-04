#include "stm32f10x.h"                  // Device header
#include <time.h>

uint16_t MyRTC_TimeArray[] = {2026, 9, 2, 12, 23, 50};
uint8_t Week = 0;

void MyRTC_SetTime(void)
{
	time_t time_cnt;
	struct tm time_date;
	
	time_date.tm_year = MyRTC_TimeArray[0] - 1900;	 //库里定义的结构体年份变量是从1900开始的，因此要减1900
	time_date.tm_mon = MyRTC_TimeArray[1] - 1;       //库里定义的结构体月份变量是从0开始的，因此要减1
	time_date.tm_mday = MyRTC_TimeArray[2];			 //在月份里的日期
	time_date.tm_hour = MyRTC_TimeArray[3];
	time_date.tm_min = MyRTC_TimeArray[4];
	time_date.tm_sec = MyRTC_TimeArray[5];
	
	time_cnt = mktime(&time_date);  //mktime函数把时间结构体转化成Unix时间戳
	
	RTC_SetCounter(time_cnt - (8 * 60 * 60));
	RTC_WaitForLastTask();
}

void MyRTC_ReadTime(void)
{
	time_t time_cnt;
	struct tm time_date;
	
	time_cnt = RTC_GetCounter() + (8 * 60 * 60);
	time_date = *localtime(&time_cnt);			//localtime函数把Unix时间戳转化成当地时间的结构体
	
	MyRTC_TimeArray[0] = time_date.tm_year + 1900;
	MyRTC_TimeArray[1] = time_date.tm_mon + 1;
	MyRTC_TimeArray[2] = time_date.tm_mday;
	MyRTC_TimeArray[3] = time_date.tm_hour;
	MyRTC_TimeArray[4] = time_date.tm_min;
	MyRTC_TimeArray[5] = time_date.tm_sec;
	
	Week = time_date.tm_wday;
}

uint8_t MyRTC_GetWeek(void)
{
	MyRTC_ReadTime();
	return Week;
}

void MyRTC_Init(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP, ENABLE);
	
	PWR_BackupAccessCmd(ENABLE);
	
	RCC_LSEConfig(RCC_LSE_ON);
	while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) != SET); //等待LSE启动完成
	
	if (BKP_ReadBackupRegister(BKP_DR1) != 0x0608) //利用BKP里的数据掉电不丢失特性
	{
		RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
		RCC_RTCCLKCmd(ENABLE);
		
		RTC_WaitForSynchro();
		RTC_WaitForLastTask();
		
		RTC_SetPrescaler(32768 - 1);
		RTC_WaitForLastTask();
		
		MyRTC_SetTime();
		
		BKP_WriteBackupRegister(BKP_DR1, 0x0608);
	}
	else
	{
		RTC_WaitForSynchro();
		RTC_WaitForLastTask();
	}
}
