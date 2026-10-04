#include "stm32f10x.h"                  // Device header
#include "Bluetooth.h"
#include "Serial.h"
#include "MyRTC.h"
#include "Weather.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

// 循环缓冲区大小
#define BUFFER_SIZE 512
// 循环缓冲区
static uint8_t buffer[BUFFER_SIZE];
// 循环缓冲区读索引
static uint16_t readIndex = 0;
// 循环缓冲区写索引
static uint16_t writeIndex = 0;

#define PREFIX_CHAR		'$'
#define CHECKSUM_CHAR	'*'
#define END_CHAR		'\n'
#define CMD_TIME       	"TIME"
#define CMD_WEATHER    	"WEATHER"
#define CMD_FORECAST   	"FORECAST"

BLEParse_t Result;
ParseCtx_t Ctx;

void Command_AddReadIndex(uint8_t length) 
{
    readIndex += length;
    readIndex %= BUFFER_SIZE;
}

uint8_t Command_ReadBufferByte(uint16_t i) 
{
    uint16_t index = i % BUFFER_SIZE;
    return buffer[index];
}

//获取已经存放还没读取的数据长度
uint16_t Command_GetLength(void) 
{
    return (writeIndex + BUFFER_SIZE - readIndex) % BUFFER_SIZE;
}

//获取环形缓冲区剩余空闲空间
uint16_t Command_GetRemain(void) 
{
    return BUFFER_SIZE - Command_GetLength();
}

uint16_t Command_WriteBuffer(uint8_t *Data, uint16_t Length)
{
	//缓冲区剩余容量不够时抛弃数据
	if (Command_GetRemain() < Length)
	{
		return 0;
	}
	
	//当可以顺序写入，没有进入循环部分时
	if (writeIndex + Length < BUFFER_SIZE)
	{
		memcpy(buffer + writeIndex, Data, Length);
		writeIndex += Length;
	}
	else
	{
		uint16_t overflowLength = writeIndex + Length - BUFFER_SIZE;
		//先把没循环部分剩余的都填上
		memcpy(buffer + writeIndex, Data, BUFFER_SIZE - writeIndex);
		//再把剩下的数据填在循环起始位置上
		memcpy(buffer, Data + BUFFER_SIZE - writeIndex, overflowLength);
		writeIndex = overflowLength;
	}
	
	return Length;
}

void BLE_Printf(char *format, ...) //把sprintf打印的操作进行可变函数封装
{
	char String[128];
	va_list arg;
	va_start(arg, format);
	vsprintf(String, format, arg);
	va_end(arg);
	Serial_SendString(String);
}

void BLE_Parse_Init(ParseCtx_t *ctx);
void BLE_Init(void)
{
	Serial_Init();
	BLE_Parse_Init(&Ctx);
}

//初始化状态机上下文
void BLE_Parse_Init(ParseCtx_t *ctx)
{
	memset(ctx, 0, sizeof(ParseCtx_t));
	ctx->State = STATE_FIND_PREFIX;
}

//出错或一帧结束时，把上下文清空并回到寻找起始符状态
static void ResetToStart(ParseCtx_t *ctx)
{
	ctx->Cmd_idx = 0;
	ctx->Cmd = BLE_CMD_NONE;
	ctx->Field_idx = 0;
	ctx->Field_Buf_idx = 0;
	ctx->Calc_Checksum = 0;
	ctx->Recv_Checksum = 0;
	ctx->Checksum_idx = 0;
	ctx->State = STATE_FIND_PREFIX;
}

//把一个十六进制字符转换成对应的数值，非法字符返回-1
static int8_t BLE_HexCharToVal(uint8_t c)
{
	if (c >= '0' && c <= '9')
	{
		return (int8_t)(c - '0');
	}
	if (c >= 'A' && c <= 'F')
	{
		return (int8_t)(c - 'A' + 10);
	}
	if (c >= 'a' && c <= 'f')
	{
		return (int8_t)(c - 'a' + 10);
	}
	return -1;
}

//把字段内容拷贝到天气状况字符串，并保证以'\0'结尾
static void BLE_CopyWeatherStr(char *dst, const char *src)
{
	strncpy(dst, src, WEATHER_STR_MAX - 1);
	dst[WEATHER_STR_MAX - 1] = '\0';
}

//把当前字段缓存(Field_Buf)按命令类型和字段序号，提交到解析结果对应的位置
static void BLE_Commit_Field(ParseCtx_t *ctx, BLEParse_t *Parse_Result)
{
	switch (ctx->Cmd)
	{
		//$TIME,年,月,日,时,分,秒
		case BLE_CMD_TIME:
		{
			switch (ctx->Field_idx)
			{
				case 0: Parse_Result->Year   = (uint16_t)atoi(ctx->Field_Buf); break;
				case 1: Parse_Result->Month  = (uint8_t)atoi(ctx->Field_Buf);  break;
				case 2: Parse_Result->Day    = (uint8_t)atoi(ctx->Field_Buf);  break;
				case 3: Parse_Result->Hour   = (uint8_t)atoi(ctx->Field_Buf);  break;
				case 4: Parse_Result->Minute = (uint8_t)atoi(ctx->Field_Buf);  break;
				case 5: Parse_Result->Second = (uint8_t)atoi(ctx->Field_Buf);  break;
			}
			break;
		}

		//$WEATHER,天气,当前温度,最高温,最低温,湿度
		case BLE_CMD_WEATHER:
		{
			switch (ctx->Field_idx)
			{
				case 0: BLE_CopyWeatherStr(Parse_Result->Weather, ctx->Field_Buf); break;
				case 1: Parse_Result->Temp_Current = (int16_t)atoi(ctx->Field_Buf); break;
				case 2: Parse_Result->Temp_High    = (int16_t)atoi(ctx->Field_Buf); break;
				case 3: Parse_Result->Temp_Low     = (int16_t)atoi(ctx->Field_Buf); break;
				case 4: Parse_Result->Humidity     = (uint8_t)atoi(ctx->Field_Buf); break;
			}
			break;
		}

		//$FORECAST,天,天气,最高温,最低温,湿度,...(从第0个字段起每5个字段为一天)
		case BLE_CMD_FORECAST:
		{
			uint8_t group = ctx->Field_idx / 5;	//第几天
			uint8_t pos   = ctx->Field_idx % 5;	//这一天里的第几个字段
			if (group < FORECAST_DAY_MAX)
			{
				switch (pos)
				{
					case 0: Parse_Result->Forecast[group].Day = (uint8_t)atoi(ctx->Field_Buf); break;
					case 1: BLE_CopyWeatherStr(Parse_Result->Forecast[group].Weather, ctx->Field_Buf); break;
					case 2: Parse_Result->Forecast[group].Temp_High = (int16_t)atoi(ctx->Field_Buf); break;
					case 3: Parse_Result->Forecast[group].Temp_Low  = (int16_t)atoi(ctx->Field_Buf); break;
					case 4: Parse_Result->Forecast[group].Humidity  = (uint8_t)atoi(ctx->Field_Buf); break;
				}
				//记录已经解析出的天数
				if (group + 1 > Parse_Result->Forecast_Day_Count)
				{
					Parse_Result->Forecast_Day_Count = group + 1;
				}
			}
			break;
		}

		default:
			break;
	}
}

//每次调用只处理缓冲区里的一个字节，成功解析出一帧完整数据时返回1
uint8_t BLE_Parse_Buffer_Data(ParseCtx_t *ctx, BLEParse_t *Parse_Result)
{
	uint8_t byte;  //循环缓冲区中readIndex对应的那个字节

	if (Command_GetLength() == 0)
	{
		return 0;
	}

	byte = Command_ReadBufferByte(readIndex);

	switch (ctx->State)
	{
		case STATE_FIND_PREFIX:
		{
			Command_AddReadIndex(1);
			if (byte == PREFIX_CHAR)
			{
				ResetToStart(ctx);			//收到起始符，清空一帧的缓存
				ctx->State = STATE_GET_CMD;
			}
			break;
		}

		case STATE_GET_CMD:
		{
			if (byte == ',')//命令之后第一个逗号
			{
				ctx->Cmd_Buf[ctx->Cmd_idx] = '\0';
				ctx->Calc_Checksum ^= byte;	//逗号也在'$'和'*'之间，算进校验和
				Command_AddReadIndex(1);
				//根据命令名判定数据包类型
				if (strcmp((char *)ctx->Cmd_Buf, CMD_TIME) == 0)
				{
					ctx->Cmd = BLE_CMD_TIME;
				}
				else if (strcmp((char *)ctx->Cmd_Buf, CMD_WEATHER) == 0)
				{
					ctx->Cmd = BLE_CMD_WEATHER;
				}
				else if (strcmp((char *)ctx->Cmd_Buf, CMD_FORECAST) == 0)
				{
					ctx->Cmd = BLE_CMD_FORECAST;
				}
				else
				{
					ResetToStart(ctx);		//未知命令，丢弃这一帧
					break;
				}
				ctx->Field_idx = 0;
				ctx->Field_Buf_idx = 0;
				ctx->State = STATE_GET_DATA;
			}
			else if (byte == CHECKSUM_CHAR || byte == END_CHAR || byte == '\r')
			{
				ResetToStart(ctx);			//命令名后面没有数据，异常
			}
			else
			{
				if (ctx->Cmd_idx < CMD_BUF_MAX - 1)//把命令字符串按字节一个个存进数组里缓存
				{
					ctx->Cmd_Buf[ctx->Cmd_idx] = byte;
					ctx->Cmd_idx ++;
					ctx->Calc_Checksum ^= byte;
					Command_AddReadIndex(1);
				}
				else
				{
					ResetToStart(ctx);		//命令名过长，丢弃
				}
			}
			break;
		}

		case STATE_GET_DATA:
		{
			if (byte == ',')  //一个数据字段后面的逗号
			{
				//一个字段结束，提交到结果里，准备接收下一个字段
				ctx->Field_Buf[ctx->Field_Buf_idx] = '\0';
				BLE_Commit_Field(ctx, Parse_Result);
				ctx->Field_Buf_idx = 0;
				ctx->Field_idx++;
				ctx->Calc_Checksum ^= byte;	//逗号算进校验和
				Command_AddReadIndex(1);
			}
			else if (byte == CHECKSUM_CHAR)
			{
				//最后一个字段结束，'*'本身不算进校验和
				ctx->Field_Buf[ctx->Field_Buf_idx] = '\0';
				BLE_Commit_Field(ctx, Parse_Result); //把最后一个数据字段做判断存进去
				ctx->Field_Buf_idx = 0;
				ctx->Field_idx++;
				Command_AddReadIndex(1);
				ctx->State = STATE_GET_CHECKSUM;
			}
			else if (byte == END_CHAR || byte == '\r')
			{
				ResetToStart(ctx);			//数据里提前出现结束符，异常
			}
			else
			{
				if (ctx->Field_Buf_idx < FIELD_BUF_MAX - 1)
				{
					ctx->Field_Buf[ctx->Field_Buf_idx++] = (char)byte;
					ctx->Calc_Checksum ^= byte;
					Command_AddReadIndex(1);
				}
				else
				{
					ResetToStart(ctx);		//字段过长，丢弃
				}
			}
			break;
		}

		case STATE_GET_CHECKSUM:
		{
			//接收'*'后面的两位十六进制校验码
			int8_t val = BLE_HexCharToVal(byte);
			if (val < 0)
			{
				ResetToStart(ctx);			//出现非法字符，丢弃
				break;
			}
			ctx->Recv_Checksum = (uint8_t)((ctx->Recv_Checksum << 4) | val);
			ctx->Checksum_idx++;
			Command_AddReadIndex(1);
			if (ctx->Checksum_idx >= 2)
			{
				ctx->State = STATE_GET_END;
			}
			break;
		}

		case STATE_GET_END:
		{
			Command_AddReadIndex(1);
			BLE_Printf("接收校验位：%x\r\n", ctx->Recv_Checksum);
			BLE_Printf("期望校验位：%x\r\n", ctx->Calc_Checksum);
			Serial_SendByte(byte);
			//等到结束符并且校验和一致，才算成功解析出一帧
//			if (byte == END_CHAR && ctx->Recv_Checksum == ctx->Calc_Checksum)
			if (ctx->Recv_Checksum == ctx->Calc_Checksum)
			{
				Parse_Result->Cmd = ctx->Cmd;	//标记这一帧的命令类型
				ResetToStart(ctx);
				return 1;
			}
			ResetToStart(ctx);				//结束符错误或校验失败
			break;
		}

		default:
		{
			ResetToStart(ctx);
			break;
		}
	}
	return 0;
}

void BLE_RefreshData(void)
{
	if (BLE_Parse_Buffer_Data(&Ctx, &Result))
	{
		//时间同步
		MyRTC_TimeArray[0] = Result.Year;
		MyRTC_TimeArray[1] = Result.Month;
		MyRTC_TimeArray[2] = Result.Day;
		MyRTC_TimeArray[3] = Result.Hour;
		MyRTC_TimeArray[4] = Result.Minute;
		MyRTC_TimeArray[5] = Result.Second + 4;
		MyRTC_SetTime();
		
		//天气同步
		Weather_Data_Reflash(&Result);
	}
}
