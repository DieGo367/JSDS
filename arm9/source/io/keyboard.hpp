#ifndef JSDS_KEYBOARD_HPP
#define JSDS_KEYBOARD_HPP

#include <nds/ndstypes.h>



void keyboardInit();
void keyboardUpdate();

void keyboardSetPressHandler(void (*handler) (const char *key, const char *code, bool shift, bool ctrl, bool alt, bool meta, bool caps));

extern bool keyboardEnterPressed;
extern bool keyboardEscapePressed;

const char *keyboardBuffer();
u8 keyboardBufferLen();
void keyboardClearBuffer();

void keyboardOpen(bool printInput);
void keyboardClose(bool clear = true);
bool isKeyboardOpen();

#endif /* JSDS_KEYBOARD_HPP */