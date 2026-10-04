#include "stm32f10x.h"

// 两级缓存：事件缓存 + 当前帧缓存
static uint8_t Key_Event = 0;      // 按键事件缓存（扫描中断里写入）
static uint8_t Key_FrameValue = 0; // 当前帧按键值（所有读取函数只读这个）

void Key_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_1;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
}

uint8_t Key_GetState(void)
{
    uint8_t Key_State = 0;
    if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0)
        Key_State = 1;
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_7) == 0)
        Key_State = 2;
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_5) == 0)
        Key_State = 3;
    return Key_State;
}

// 【新增】帧同步函数，主循环开头必须调用一次
void Key_SyncFrame(void)
{
    Key_FrameValue = Key_Event; // 把事件缓存同步到当前帧
    Key_Event = 0;              // 清空事件缓存，等待下一次按键
}

// 【修改】只读帧缓存，不再清零
uint8_t Key_GetNum(void)
{
    return Key_FrameValue;
}

void Key_Tick(void)
{
    static uint8_t Timer20ms = 0;
    static uint8_t NewState, OldState;
    Timer20ms++;
    if (Timer20ms >= 20)
    {
        Timer20ms = 0;
        OldState = NewState;
        NewState = Key_GetState();
        
        // 检测到按键释放，写入事件缓存
        if (OldState != 0 && NewState == 0)
        {
            Key_Event = OldState; // 写入事件缓存，不直接被读取消耗
        }
    }
}
