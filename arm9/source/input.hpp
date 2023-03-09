#ifndef JSDS_INPUT_HPP
#define JSDS_INPUT_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



void buttonEvents(bool down);
void stylusEvents();

extern bool pauseKeyEvents;
void onKeyDown(const u16 codepoint, const char *name, bool shift, bool ctrl, bool alt, bool meta, bool caps);
void onKeyUp(const u16 codepoint, const char *name, bool shift, bool ctrl, bool alt, bool meta, bool caps);

#endif /* JSDS_INPUT_HPP */