#ifndef __CURSOR_H
#define __CURSOR_H

void Cursor_Blink(int16_t X, int16_t Y, uint8_t Width, uint8_t Height);
void Cursor_Clear_Blink(void);
void Cursor_Tick(void);

#endif
