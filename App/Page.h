#ifndef __PAGE_H
#define __PAGE_H

typedef enum
{
	Page_Time,      
	Page_Setting,
	Page_Menu
}PAGE_STATE;

extern PAGE_STATE Page_Mode;
extern PAGE_STATE last_Page_Mode;

void Page_Switch(void);
	
#endif	
