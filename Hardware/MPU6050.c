#include "stm32f10x.h"                  
#include "I2C.h"                        
#include "MPU6050_Reg.h"                

#define MPU6050_ADDRESS		0xD0        // MPU6050 I2C设备地址(写地址)，读地址是0xD1

/**
 * @brief   I2C事件等待函数，带超时防止卡死
 * @param   I2Cx: I2C外设，这里用I2C2
 * @param   I2C_EVENT: 需要等待的I2C事件标志
 * @retval  1=事件成功；0=超时失败
 */
uint8_t MPU6050_I2C_CheckEvent(I2C_TypeDef* I2Cx, uint32_t I2C_EVENT)
{
	uint32_t timeout = 10000;
	// 循环等待事件置位
	while(I2C_CheckEvent(I2Cx, I2C_EVENT) != SUCCESS)
	{
		timeout --;
		if(timeout == 0)    // 超时退出
		{
			return 0;
		}
	}
	return 1;
}

/**
 * @brief   I2C通信异常终止处理：发送停止信号，恢复ACK
 * @note    通信出错时调用，释放总线
 */
static void MPU6050_I2C_Abort(void)
{
	I2C_GenerateSTOP(I2C2, ENABLE);         // 产生I2C停止信号
	I2C_AcknowledgeConfig(I2C2, ENABLE);    // 开启应答，恢复默认状态
}

/**
 * @brief   MPU6050 写单个寄存器
 * @param   RegAddress: 寄存器地址
 * @param   Data: 要写入的数据
 * @retval  1成功，0失败
 */
uint8_t MPU6050_WriteReg(uint8_t RegAddress, uint8_t Data)
{
	I2C_GenerateSTART(I2C2, ENABLE);                                // 发起I2C起始信号
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_MODE_SELECT)) { MPU6050_I2C_Abort(); return 0; } //检测EV5事件
	
	I2C_Send7bitAddress(I2C2, MPU6050_ADDRESS, I2C_Direction_Transmitter); // 发送从机写地址
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { MPU6050_I2C_Abort(); return 0; } //EV6
	
	I2C_SendData(I2C2, RegAddress);                                 // 发送要访问的寄存器地址
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTING)) { MPU6050_I2C_Abort(); return 0; }
	
	I2C_SendData(I2C2, Data);                                       // 发送寄存器写入数据
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { MPU6050_I2C_Abort(); return 0; }//发送最后一个字节就检测这个事件
	
	I2C_GenerateSTOP(I2C2, ENABLE);                                 // 发送停止信号，结束本次写通信
	return 1;
}

/**
 * @brief   MPU6050 读取单个寄存器（指定地址读）
 * @param   RegAddress: 待读取寄存器地址
 * @retval  返回读到的寄存器值；通信异常返回0
 * @note    I2C读操作流程：起始->写地址+寄存器->重复起始->读地址->读1字节->NACK+停止
 */
uint8_t MPU6050_ReadReg(uint8_t RegAddress)
{
	uint8_t Data = 0;
	I2C_GenerateSTART(I2C2, ENABLE);                                // 起始信号
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_MODE_SELECT)) { MPU6050_I2C_Abort(); return 0; } //检测EV5事件
	
	I2C_Send7bitAddress(I2C2, MPU6050_ADDRESS, I2C_Direction_Transmitter); // 写方向，告诉MPU要访问哪个寄存器
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { MPU6050_I2C_Abort(); return 0; }//EV6
	
	I2C_SendData(I2C2, RegAddress);                                 // 发送寄存器地址
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { MPU6050_I2C_Abort(); return 0; }
	
	I2C_GenerateSTART(I2C2, ENABLE);                                // 重复起始信号，切换为读
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_MODE_SELECT)) { MPU6050_I2C_Abort(); return 0; }//检测EV5事件
	
	I2C_Send7bitAddress(I2C2, MPU6050_ADDRESS, I2C_Direction_Receiver); // 发送从机读地址
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) { MPU6050_I2C_Abort(); return 0; }//EV6
	
	I2C_AcknowledgeConfig(I2C2, DISABLE);                           // 最后1字节，关闭应答(NACK)
	I2C_GenerateSTOP(I2C2, ENABLE);                                 // 提前准备停止信号！！！手册上规定了硬件I2C要提前准备停止条件
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_BYTE_RECEIVED)) { I2C_AcknowledgeConfig(I2C2, ENABLE); return 0; }
	
	Data = I2C_ReceiveData(I2C2);                                   // 读取数据
	I2C_AcknowledgeConfig(I2C2, ENABLE);                            // 恢复ACK使能
	return Data;
}

/**
 * @brief   MPU6050 连续读取多个寄存器（MPU支持地址自增）就是当前地址连续读
 * @param   RegAddress: 起始寄存器地址
 * @param   Buffer: 接收数据缓冲区指针
 * @param   Length: 读取字节数量
 * @retval 1成功，0失败
 */
uint8_t MPU6050_ReadRegs(uint8_t RegAddress, uint8_t *Buffer, uint8_t Length)
{
	if(Buffer == 0 || Length == 0) return 0;                        // 空指针或长度为0直接返回
	
	I2C_GenerateSTART(I2C2, ENABLE);                                // 起始信号
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_MODE_SELECT)) { MPU6050_I2C_Abort(); return 0; }
	
	I2C_Send7bitAddress(I2C2, MPU6050_ADDRESS, I2C_Direction_Transmitter); // 写从机地址
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { MPU6050_I2C_Abort(); return 0; }
	
	I2C_SendData(I2C2, RegAddress);                                 // 发送起始寄存器地址
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { MPU6050_I2C_Abort(); return 0; }
	
	I2C_GenerateSTART(I2C2, ENABLE);                                // 重复起始，切换读模式
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_MODE_SELECT)) { MPU6050_I2C_Abort(); return 0; }
	
	I2C_Send7bitAddress(I2C2, MPU6050_ADDRESS, I2C_Direction_Receiver); // 读从机地址
	if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) { MPU6050_I2C_Abort(); return 0; }
	
	while(Length > 0)
	{
		if(Length == 1)
		{
			I2C_AcknowledgeConfig(I2C2, DISABLE);                   // 最后一字节，NACK
			I2C_GenerateSTOP(I2C2, ENABLE);                         // 提前产生停止
		}
		if(!MPU6050_I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_BYTE_RECEIVED)) { MPU6050_I2C_Abort(); return 0; }
		*Buffer = I2C_ReceiveData(I2C2);                            // 读取数据存入缓存
		Buffer ++;
		Length --;
	}
	I2C_AcknowledgeConfig(I2C2, ENABLE);                            // 恢复ACK
	return 1;
}

/**
 * @brief   软件空循环延时（阻塞式）
 * @param   n: 循环次数
 */
static void MPU6050_SoftDelay(volatile uint32_t n)
{
	while (n--) { }
}

/**
 * @brief   带校验的寄存器写入：写完立刻回读校验，失败自动重试
 * @param   RegAddress: 寄存器地址
 * @param   Data: 待写入数据
 * @param   tries: 最大重试次数
 * @retval 1写入校验成功；0多次重试仍然失败
 */
static uint8_t MPU6050_WriteVerify(uint8_t RegAddress, uint8_t Data, uint8_t tries)
{
	while (tries--)
	{
		MPU6050_WriteReg(RegAddress, Data);
		if (MPU6050_ReadReg(RegAddress) == Data) return 1;         // 回读相等，写入成功
	}
	return 0;
}

/**
 * @brief   MPU6050初始化函数
 * @note    复位MPU，唤醒，配置采样率、低通滤波、加速度计/陀螺仪量程
 */
void MPU6050_Init(void)
{
	Hardware_I2C_Init();                                            // 初始化I2C2硬件
	
	MPU6050_WriteReg(MPU6050_PWR_MGMT_1, 0x80);                     // 置位RESET，硬件复位MPU6050
	MPU6050_SoftDelay(2000000);                                     // 等待复位完成
	
	MPU6050_WriteVerify(MPU6050_PWR_MGMT_1, 0x00, 8);               // 清除复位，唤醒MPU，重试8次
	MPU6050_WriteVerify(MPU6050_PWR_MGMT_2, 0x00, 8);               // 使能加速度计和陀螺仪全部轴
	
	MPU6050_WriteReg(MPU6050_SMPLRT_DIV, 0x09);                     // 采样率分频：1kHz/(1+9)=100Hz
	MPU6050_WriteReg(MPU6050_CONFIG, 0x06);                         // 低通滤波，带宽5Hz
	MPU6050_WriteReg(MPU6050_GYRO_CONFIG, 0x18);                    // 陀螺仪量程 ±2000°/s
	MPU6050_WriteReg(MPU6050_ACCEL_CONFIG, 0x18);                   // 加速度计量程 ±16g
}

/**
 * @brief   读取MPU6050加速度、陀螺仪数据
 * @param   AccX/AccY/AccZ: 加速度三轴原始数据指针
 * @param   GyroX/GyroY/GyroZ: 陀螺仪三轴原始数据指针
 * @retval  1成功
 * @note    当前代码只读取加速度，陀螺仪变量置0；你后续可以改成调用ReadRegs一次性读全部7字节
 */
uint8_t MPU6050_GetData(int16_t *AccX, int16_t *AccY, int16_t *AccZ,
						int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ)
{
	uint8_t high, low;
	// 读取加速度X轴高、低字节，拼接成16位有符号原始值
	high = MPU6050_ReadReg(MPU6050_ACCEL_XOUT_H);
	low = MPU6050_ReadReg(MPU6050_ACCEL_XOUT_L);
	*AccX = (int16_t)((high << 8) | low);
	
	// Y轴
	high = MPU6050_ReadReg(MPU6050_ACCEL_YOUT_H);
	low = MPU6050_ReadReg(MPU6050_ACCEL_YOUT_L);
	*AccY = (int16_t)((high << 8) | low);
	
	// Z轴
	high = MPU6050_ReadReg(MPU6050_ACCEL_ZOUT_H);
	low = MPU6050_ReadReg(MPU6050_ACCEL_ZOUT_L);
	*AccZ = (int16_t)((high << 8) | low);
	
	// 当前代码暂不读取陀螺仪，置0
	*GyroX = 0;
	*GyroY = 0;
	*GyroZ = 0;
	return 1;
}

/**
 * @brief   读取MPU6050 WHO_AM_I ID寄存器
 * @retval  返回读到的ID值，正常MPU6050为0x68
 */
uint8_t MPU6050_GetID(void)
{
	return MPU6050_ReadReg(MPU6050_WHO_AM_I);
}

/**
 * @brief   检测MPU6050是否硬件连接正常
 * @retval 1设备在线；0读不到正确ID，接线/设备异常
 */
uint8_t MPU6050_IsConnected(void)
{
	return (MPU6050_GetID() == 0x68) ? 1 : 0;
}
