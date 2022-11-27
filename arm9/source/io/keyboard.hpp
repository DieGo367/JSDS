#ifndef JSDS_KEYBOARD_HPP
#define JSDS_KEYBOARD_HPP

#include <nds/ndstypes.h>



void keyboardInit();
void keyboardUpdate();

extern bool keyboardEnterPressed;
extern bool keyboardEscapePressed;

const char *keyboardBuffer();
u8 keyboardBufferLen();

void keyboardClearBuffer();
void onKeyboardKeyPress(int key);
void onKeyboardKeyRelease(int key);

void keyboardOpen(bool printInput);
void keyboardClose(bool clear = true);
bool isKeyboardOpen();

#endif /* JSDS_KEYBOARD_HPP */