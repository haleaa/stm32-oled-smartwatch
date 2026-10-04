#include "stm32f10x.h"                  // Device header
#include "MyI2C.h"
#include "MAX30102.h"

//指定地址写一个字节
bool MAX30102_SendByte(uint8_t Byte, uint8_t Adder)
{
	MyI2C_Start();
	if (MyI2C_SendByte(MAX30102_WRITE_ADDR) == false)
	{
		MyI2C_Stop();
		return false;
	}
	
	if (MyI2C_SendByte(Adder) == false)
	{
		MyI2C_Stop();
		return false;
	}
	
	if (MyI2C_SendByte(Byte) == false)
	{
		MyI2C_Stop();
		return false;
	}
	MyI2C_Stop();
	return true;
}

//指定地址读一个字节
bool MAX30102_SpecAddrRead(uint8_t *Data, uint8_t Adder)
{
	MyI2C_Start();
	if (MyI2C_SendByte(MAX30102_WRITE_ADDR) == false)
	{
		MyI2C_Stop();
		return false;
	}
	
	if (MyI2C_SendByte(Adder) == false)
	{
		MyI2C_Stop();
		return false;
	}

	MyI2C_Start();
	if (MyI2C_SendByte(MAX30102_READ_ADDR) == false)
	{
		MyI2C_Stop();
		return false;
	}
	*Data = MyI2C_ReceiveByte();
	MyI2C_SendACK(1); //发送NACK
	MyI2C_Stop();
	
	return true;
}

bool MAX30102_SetReadRegAddr(uint8_t RegAddr)
{
	MyI2C_Start();
	if (MyI2C_SendByte(MAX30102_WRITE_ADDR) == false)
	{
		MyI2C_Stop();
		return false;
	}
	
	if (MyI2C_SendByte(RegAddr) == false)
	{
		MyI2C_Stop();
		return false;
	}
	return true;
}

//当前地址读字节
bool MAX30102_CurrentAddrRead(uint8_t *pBuf, uint8_t len)
{
	MyI2C_Start();
	if (MyI2C_SendByte(MAX30102_READ_ADDR) == false)
	{
		MyI2C_Stop();
		return false;
	}
	
	for (uint8_t i = 0; i < len; i++)
	{
		pBuf[i] = MyI2C_ReceiveByte();
		if (i == len - 1) 
		{
			MyI2C_SendACK(1);
			continue;
		}
		MyI2C_SendACK(0);
	}

	MyI2C_Stop();
	
	return true;
}

bool MAX30102_Init(void)
{
	MyI2C_Init();
	
	if(!MAX30102_SendByte(0xc0, REG_INTR_ENABLE_1))   // INTR setting
		return false;
	if(!MAX30102_SendByte(0x00, REG_INTR_ENABLE_2))
		return false;
	if(!MAX30102_SendByte(0x00, REG_FIFO_WR_PTR))     // FIFO_WR_PTR[4:0]
		return false;
	if(!MAX30102_SendByte(0x00, REG_OVF_COUNTER))     // OVF_COUNTER[4:0]
		return false;
	if(!MAX30102_SendByte(0x00, REG_FIFO_RD_PTR))     // FIFO_RD_PTR[4:0]
		return false;
	if(!MAX30102_SendByte(0x0f, REG_FIFO_CONFIG))     // sample avg = 1, fifo rollover=false, fifo almost full = 17
		return false;
	if(!MAX30102_SendByte(0x03, REG_MODE_CONFIG))     // 0x02 for Red only, 0x03 for SpO2 mode, 0x07 multimode LED
		return false;
	if(!MAX30102_SendByte(0x27, REG_SPO2_CONFIG))     // SPO2_ADC range = 4096nA, SPO2 sample rate (100 Hz), LED pulseWidth (400uS)
		return false;
	if(!MAX30102_SendByte(0x26 , REG_LED1_PA))         // Choose value for ~ 7mA for LED1
		return false;
	if(!MAX30102_SendByte(0x24, REG_LED2_PA))         // Choose value for ~ 7mA for LED2
		return false;
	if(!MAX30102_SendByte(0x7f, REG_PILOT_PA))        // Choose value for ~ 25mA for Pilot LED
		return false;
	
	return true;  
}

bool Is_MAX30102_GetID(void)
{
	uint8_t Data = 0;
	if (MAX30102_SpecAddrRead(&Data, REG_PART_ID) == false)
		return false;
	
	if(Data != 0x15) return false;
	return true;
}

//传入红光和红外光数据数组的指针
bool MAX30102_Read_FIFO(uint32_t *Red_Led, uint32_t *Ir_Led)
{
	uint8_t Int_Status1 = 0;
    uint8_t int_Status2 = 0;
	uint32_t un_temp = 0;
	*Red_Led = 0;
	*Ir_Led = 0;
	uint8_t FIFO_Data[6];
	
	if (!MAX30102_SpecAddrRead(&Int_Status1, REG_INTR_STATUS_1))
		return false;
	
	//既无PPG数据就绪，也无FIFO满，直接返回false
    if ((Int_Status1 & 0x60) == 0)  // bit5=PPG_RDY, bit6=FIFO_A_FULL
        return false;
	
	MAX30102_SpecAddrRead(&int_Status2, REG_INTR_STATUS_2);
	//根据手册，读取两个中断寄存器会清空寄存器里的数据，所以这里是先清空一下寄存器的值
	
	//给当前地址读指定地址
	if (!MAX30102_SetReadRegAddr(REG_FIFO_DATA))
		return false;
	
	//当前地址读6个字节存放在FIFO数组
	if (!MAX30102_CurrentAddrRead(FIFO_Data, 6))
		return false;
	
	//注意这里用当前地址连续读6个字节而不用for循环多次调用指定地址读一个字节来读取是因为
	//正常情况下当前地址读完自动增加的是寄存器地址
	//但当读取的是FIFO_DATA寄存器时，当前地址读每读一个字节，增加的是样本内部字节索引而不是寄存器的指针
	//指定地址读只能指定寄存器地址，因此不能用
	
	un_temp = (unsigned char) FIFO_Data[0];
	un_temp <<= 16;
	*Red_Led += un_temp;
	un_temp = (unsigned char) FIFO_Data[1];
	un_temp <<= 8;
	*Red_Led += un_temp;
	un_temp = (unsigned char) FIFO_Data[2];
	*Red_Led += un_temp;

	un_temp = (unsigned char) FIFO_Data[3];
	un_temp <<= 16;
	*Ir_Led += un_temp;
	un_temp = (unsigned char) FIFO_Data[4];
	un_temp <<= 8;
	*Ir_Led += un_temp;
	un_temp = (unsigned char) FIFO_Data[5];
	*Ir_Led += un_temp;
	*Red_Led &= 0x03FFFF;  //根据手册读取到的FIFO数据只有低18位是有效数据，因此舍弃高位
	*Ir_Led &= 0x03FFFF;
	
	return true;
}

//从机地址在手册的Slave ID这里找，PartID的内容不等于从机地址

//BUG：只要调用MAX30102_SendByte，按键就会失效
//原因：软件i2c里面对I0口辅助是用的Delay_us会破坏systick的配置，而按键用的systick扫描
//解决：Delay_us里用变量缓存systick的配置，延时完成后还原systick的配置
