#include "stm32f10x.h"                  // Device header
#include "OLED.h"
#include "Key.h"
#include "Menu.h"

#define FLIGHT_BACK 1
#define ITEM_TOTAL 	1

#define MENU_HOME  	0

typedef struct
{
	const uint8_t *Icon;
	uint8_t FeatureId;
}
FlashLight_Item_t;

FlashLight_Item_t FlashLight_Item[ITEM_TOTAL];

static uint8_t FLight_Idx = 0;

static uint8_t KeyNum;

void FlashLight_Init(void)
{
	FlashLight_Item[0].Icon = Back;
	FlashLight_Item[0].FeatureId = FLIGHT_BACK;
}

void FlashLight_Render(void)
{
	OLED_Clear();
	OLED_Reverse();
	OLED_ShowImage(0, 0, 16, 16, FlashLight_Item[FLight_Idx].Icon);
}

void FlashLight_Switch(void)
{
	KeyNum = Key_GetNum();
	if (KeyNum == 3)
	{
		switch (FlashLight_Item[FLight_Idx].FeatureId)
		{
			case FLIGHT_BACK:
				Menu_SetFeatPage(MENU_HOME);
				break;
		}
	}	
}
