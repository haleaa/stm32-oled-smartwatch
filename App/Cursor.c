#include "stm32f10x.h"                  // Device header
#include "OLED.h"
#include "Setting.h"

static uint8_t Clear_flag = 1;
static int16_t Cursor_X, Cursor_Y;
static uint8_t Cursor_Width, Cursor_Height;

void Cursor_Blink(int16_t X, int16_t Y, uint8_t Width, uint8_t Height)
{	

	Cursor_X = X; Cursor_Y = Y; Cursor_Width = Width; Cursor_Height = Height;
	
	Clear_flag = 0;
}

void Cursor_Clear_Blink(void)
{
	Clear_flag = 1;
}

void Cursor_Tick(void)
{
	static uint16_t Cursor_Count = 0;
	
	if (Clear_flag == 0)
	{
		Cursor_Count ++;
		
		if (Cursor_Count >= 500)
		{
			OLED_ReverseArea(Cursor_X, Cursor_Y, Cursor_Width, Cursor_Height);
			Cursor_Count = 0;
		}
	}
}
