#ifndef JSDS_KEYBOARD_HPP
#define JSDS_KEYBOARD_HPP

#include <nds/ndstypes.h>



enum ComposeStatus { INACTIVE, COMPOSING, FINISHED };

void keyboardInit();
void keyboardUpdate();

bool keyboardShow();
bool keyboardHide(); 

void keyboardSetPressHandler(void (*handler) (const u16 codepoint, const char *name, bool shift, bool ctrl, bool alt, bool meta, bool caps));
void keyboardSetReleaseHandler(void (*handler) (const u16 codepoint, const char *name, bool shift, bool ctrl, bool alt, bool meta, bool caps));

void keyboardCompose();
ComposeStatus keyboardComposeStatus();
void keyboardComposeAccept(char **strPtr, int *strSize);
void keyboardComposeCancel();

#endif /* JSDS_KEYBOARD_HPP */