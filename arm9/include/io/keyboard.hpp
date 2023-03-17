#ifndef JSDS_KEYBOARD_HPP
#define JSDS_KEYBOARD_HPP

#include <nds/ndstypes.h>
#include "util/font.hpp"



enum ComposeStatus { KEYBOARD_INACTIVE, KEYBOARD_COMPOSING, KEYBOARD_FINISHED };

void keyboardInit(NitroFont composeFont);
void keyboardUpdate();

bool keyboardShow();
bool keyboardHide(); 
bool keyboardButtonControls(bool enable);

void keyboardSetPressHandler(bool (*handler) (const char16_t codepoint, const char *name, bool shift, int layout, bool repeat));
void keyboardSetReleaseHandler(bool (*handler) (const char16_t codepoint, const char *name, bool shift, int layout));

void keyboardCompose(bool allowCancel);
ComposeStatus keyboardComposeStatus();
void keyboardComposeAccept(char **strPtr, u32 *strSize);
void keyboardComposeCancel();

#endif /* JSDS_KEYBOARD_HPP */