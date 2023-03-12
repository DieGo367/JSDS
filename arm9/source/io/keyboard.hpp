#ifndef JSDS_KEYBOARD_HPP
#define JSDS_KEYBOARD_HPP

#include <nds/ndstypes.h>



enum ComposeStatus { INACTIVE, COMPOSING, FINISHED };

void keyboardInit();
void keyboardUpdate(u32 blockedButtons = 0);

bool keyboardShow();
bool keyboardHide(); 

void keyboardSetPressHandler(bool (*handler) (const u16 codepoint, const char *name, bool shift, int layout, bool repeat));
void keyboardSetReleaseHandler(bool (*handler) (const u16 codepoint, const char *name, bool shift, int layout));

void keyboardCompose(bool allowCancel);
ComposeStatus keyboardComposeStatus();
void keyboardComposeAccept(char **strPtr, int *strSize);
void keyboardComposeCancel();

#endif /* JSDS_KEYBOARD_HPP */