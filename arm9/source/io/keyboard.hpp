#ifndef JSDS_KEYBOARD_HPP
#define JSDS_KEYBOARD_HPP

#include <nds/ndstypes.h>



void keyboardInit();
void keyboardUpdate();

bool keyboardShow();
bool keyboardHide(); 

void keyboardSetPressHandler(void (*handler) (const char *key, const char *code, bool shift, bool ctrl, bool alt, bool meta, bool caps));

extern bool keyboardEnterPressed;
extern bool keyboardEscapePressed;

const char *keyboardBuffer();
u8 keyboardBufferLen();
void keyboardClearBuffer();

#endif /* JSDS_KEYBOARD_HPP */