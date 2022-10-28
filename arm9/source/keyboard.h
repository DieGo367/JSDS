#ifndef JSDS_KEYBOARD_H
#define JSDS_KEYBOARD_H

#include <nds/arm9/keyboard.h>
#include "jerry/jerryscript.h"



extern Keyboard *keyboard;

extern bool keyboardEnterPressed;
extern bool keyboardEscapePressed;

const char *keyboardBuffer();
u8 keyboardBufferLen();

void keyboardClearBuffer();
void onKeyboardKeyPress(int key);
void onKeyboardKeyRelease(int key);

void keyboardOpen(bool printInput);
void keyboardClose();
bool isKeyboardOpen();

#endif /* JSDS_KEYBOARD_H */