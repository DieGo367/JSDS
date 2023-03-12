#ifndef JSDS_INPUT_HPP
#define JSDS_INPUT_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



void buttonEvents(bool down);
u32 getCanceledButtons();

void touchEvents();

extern bool pauseKeyEvents;
bool onKeyDown(const u16 codepoint, const char *name, bool shift, bool caps, int layout, bool repeat);
bool onKeyUp(const u16 codepoint, const char *name, bool shift, bool caps, int layout);

#endif /* JSDS_INPUT_HPP */