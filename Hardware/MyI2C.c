#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include <stdbool.h>

void MyI2C_WriteBit_SCL(uint8_t BitValue)
{
	GPIO_WriteBit(GPIOA, GPIO_Pin_0, (BitAction)BitValue);
	Delay_us(10);
}

void MyI2C_WriteBit_SDA(uint8_t BitValue)
{
	GPIO_WriteBit(GPIOA, GPIO_Pin_1, (BitAction)BitValue);
	Delay_us(10);
}

uint8_t MyI2C_ReadBit_SDA(void)
{
	uint8_t BitValue = 0;
	BitValue = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1);
	Delay_us(10);
	return BitValue;
}

void MyI2C_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;  //设置为开漏输出模式
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStructure);
	GPIO_WriteBit(GPIOA,GPIO_Pin_0 | GPIO_Pin_1,Bit_SET); //初始把SCL和SDA置高电平
}

void MyI2C_Start(void) //关键是当SCL处于高电平的时候把SDA给拉低，产生下降沿
{
	MyI2C_WriteBit_SCL(1);
	MyI2C_WriteBit_SDA(1);
	MyI2C_WriteBit_SDA(0);
	MyI2C_WriteBit_SCL(0);
	//起始条件结束时SDA和SCL必须都为低电平
}

void MyI2C_Stop(void) //关键是当SCL处于高电平的时候把SDA给拉高，产生上升沿
{
	MyI2C_WriteBit_SDA(0);
	MyI2C_WriteBit_SCL(1);
	MyI2C_WriteBit_SDA(1);
	//终止条件结束时SDA和SCL必须都为高电平
}

bool MyI2C_SendByte(uint8_t Byte)
{
	for (uint8_t i = 0; i < 8; i++)
	{	
		MyI2C_WriteBit_SDA(Byte & 0x80 >> i); //取最高位固定用 & 0x80
		MyI2C_WriteBit_SCL(1);	
		MyI2C_WriteBit_SCL(0);
		//上一轮SCL是低电平，写入SDA电平之后SCL置高电平读取数据，再拉回低电平让下一轮写入
	}
	
	//检测从机是否收到
	MyI2C_WriteBit_SDA(1);    //主机释放SDA，交给从机
	MyI2C_WriteBit_SCL(1);    //SCL拉高，产生第9个时钟，从机输出ACK
	bool ack = !MyI2C_ReadBit_SDA(); //SDA=0返回true
	MyI2C_WriteBit_SCL(0);    //拉低SCL，结束应答时序

	return ack; //true=收到ACK；false=没收到ACK
}

uint8_t MyI2C_ReceiveByte(void)
{
	uint8_t Byte = 0x00;
	MyI2C_WriteBit_SDA(1); //把SDA的控制权交给从机
	for (uint8_t i = 0; i < 8; i++)
	{	
		MyI2C_WriteBit_SCL(1); //开始接收
		if(MyI2C_ReadBit_SDA() == 1) Byte |= (0x80 >> i);
		MyI2C_WriteBit_SCL(0);
	}
	return Byte;
}

//第二种写法（先放最低位通过左移再移到最高位）
//uint8_t MyI2C_ReceiveByte(void)
//{
//    uint8_t Byte = 0x00;
//    MyI2C_WriteBit_SDA(1);
//    
//    for (uint8_t i = 0; i < 8; i++)
//    {
//        MyI2C_WriteBit_SCL(1);
//        Byte <<= 1;                 // 整体左移，腾出最低位
//        Byte |= MyI2C_ReadBit_SDA();// 新读到的位放进最低位
//        MyI2C_WriteBit_SCL(0);
//    }
//    return Byte;
//}

void MyI2C_SendACK(uint8_t AckBit)
{
	MyI2C_WriteBit_SDA(AckBit); 
	MyI2C_WriteBit_SCL(1);	
	MyI2C_WriteBit_SCL(0);
}

uint8_t MyI2C_ReceiveAck(void)
{
	uint8_t AckBit = 0;
	MyI2C_WriteBit_SDA(1); //把SDA的控制权交给从机
	MyI2C_WriteBit_SCL(1); //开始接收
	if(MyI2C_ReadBit_SDA() == 1) AckBit = 1;
	MyI2C_WriteBit_SCL(0);
	return AckBit;
}

//应答位知识
//主机读取数据：读完一个Byte之后，还要数据就要发送ACK（SDA 置 0）告诉从机还要数据。
//最后一个字节，读完不再读了，主机输出 NACK（SDA 置 1）

//主机发送数据：主机发送一个Byte之后要检测一下从机的应答位判断是否还需要接收到数据
//从机正常收到数据，拉低 SDA（ACK，0）。从机没收到保持高（NACK，1）

//读写位知识
//0 = 写方向
//1 = 读方向
