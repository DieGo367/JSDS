#ifndef JSDS_KEYBOARD_H
#define JSDS_KEYBOARD_H

#include <nds/ndstypes.h>
#include <nds/arm9/keyboard.h>
#include "jerry/jerryscript.h"



extern Keyboard *keyboard;

extern bool keyboardEnterPressed;
extern bool keyboardEscapePressed;

const char *keyboardBuffer();
u8 keyboardBufferLen();

void keyboardClearBuffer();
void onKeyboardKeyPress(int key);

#endif /* JSDS_KEYBOARD_H */