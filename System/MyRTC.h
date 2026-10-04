#ifndef __MYRTC_H
#define __MYRTC_H

extern uint16_t MyRTC_TimeArray[];

void MyRTC_SetTime(void);
void MyRTC_ReadTime(void);
uint8_t MyRTC_GetWeek(void);

void MyRTC_Init(void);

#endif
