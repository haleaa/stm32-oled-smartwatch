#ifndef __BLUETOOTH_H
#define __BLUETOOTH_H

#include <stdint.h>
#include <stdbool.h>

// 命令名缓存最大长度
#define CMD_BUF_MAX			16
// 单个字段缓存最大长度
#define FIELD_BUF_MAX		16
// 天气状况字符串最大长度
#define WEATHER_STR_MAX		16
// 预报数据包最多天数
#define FORECAST_DAY_MAX	3

// 数据包命令类型
typedef enum
{
	BLE_CMD_NONE = 0,		// 无命令
	BLE_CMD_TIME,			// 时间数据包 $TIME
	BLE_CMD_WEATHER,		// 天气数据包 $WEATHER
	BLE_CMD_FORECAST,		// 预报数据包 $FORECAST
} BLECmd_t;

// 单日天气预报
typedef struct
{
	uint8_t Day;						// 第几天(1明天,2后天,3大后天)
	char Weather[WEATHER_STR_MAX];		// 天气状况
	int16_t Temp_High;					// 最高温
	int16_t Temp_Low;					// 最低温
	uint8_t Humidity;					// 湿度
} BLEForecastDay_t;

// 解析结果（根据 Cmd 判断哪一组字段有效）
typedef struct
{
	BLECmd_t Cmd;						// 命令类型
	// 时间数据包 $TIME,年,月,日,时,分,秒
	uint16_t Year;						// 年
	uint8_t Month;						// 月
	uint8_t Day;						// 日
	uint8_t Hour;						// 时
	uint8_t Minute;						// 分
	uint8_t Second;						// 秒
	// 天气数据包 $WEATHER,天气,当前温度,最高温,最低温,湿度
	char Weather[WEATHER_STR_MAX];		// 天气状况
	int16_t Temp_Current;				// 当前温度
	int16_t Temp_High;					// 最高温
	int16_t Temp_Low;					// 最低温
	uint8_t Humidity;					// 湿度
	// 预报数据包 $FORECAST,天,天气,最高温,最低温,湿度,...(每5个字段一天)
	uint8_t Forecast_Day_Count;						// 实际解析出的天数
	BLEForecastDay_t Forecast[FORECAST_DAY_MAX];	// 各天预报数据
} BLEParse_t;

// 有限状态机状态
typedef enum
{
	STATE_FIND_PREFIX = 0,	// 等待起始符 '$'
	STATE_GET_CMD,			// 接收命令名
	STATE_GET_DATA,			// 接收数据字段
	STATE_GET_CHECKSUM,		// 接收 '*' 后的两位十六进制校验码
	STATE_GET_END,			// 等待结束符 '\n'
} ParseState_t;

// 有限状态机解析上下文
typedef struct
{
	ParseState_t State;					// 当前状态
	uint8_t Cmd_Buf[CMD_BUF_MAX];		// 命令名缓存
	uint8_t Cmd_idx;					// 命令名已接收长度
	BLECmd_t Cmd;						// 已判定的命令类型
	uint8_t Field_idx;					// 当前是第几个数据字段(从0开始)
	char Field_Buf[FIELD_BUF_MAX];		// 当前字段内容缓存
	uint8_t Field_Buf_idx;				// 当前字段已接收长度
	uint8_t Calc_Checksum;				// 逐字节异或计算得到的校验和
	uint8_t Recv_Checksum;				// 接收到的校验码
	uint8_t Checksum_idx;				// 已接收的校验码位数
} ParseCtx_t;

uint16_t Command_GetLength(void);
uint16_t Command_WriteBuffer(uint8_t *Data, uint16_t Length);
void BLE_Printf(char *format, ...);
void BLE_Init(void);
void BLE_Parse_Init(ParseCtx_t *ctx);
uint8_t BLE_Parse_Buffer_Data(ParseCtx_t *ctx, BLEParse_t *Parse_Result);
void BLE_RefreshData(void);

#endif
