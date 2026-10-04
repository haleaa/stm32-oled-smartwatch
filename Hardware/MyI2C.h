#ifndef __MYI2C_H
#define __MYI2C_H

#include <stdbool.h>

void MyI2C_WriteBit_SCL(uint8_t BitValue);
void MyI2C_WriteBit_SDA(uint8_t BitValue);
uint8_t MyI2C_ReadBit_SDA(void);
void MyI2C_Init(void);
void MyI2C_Start(void);
void MyI2C_Stop(void);
bool MyI2C_SendByte(uint8_t Byte);
uint8_t MyI2C_ReceiveByte(void);
void MyI2C_SendACK(uint8_t AckBit);
uint8_t MyI2C_ReceiveAck(void);

#endif
