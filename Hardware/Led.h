#include "stm32f10x.h"   // Device header
#ifndef __LED_H
#define __LED_H
               
#define Led_On 0
#define Led_Off 1
extern uint8_t Led1,Led2;

void Led_Init(void);
void LED1_ON(void);
void LED1_OFF(void);
void LED2_ON(void);
void LED2_OFF(void);
void Led1_Turn(void);
void Led2_Turn(void);
#endif
