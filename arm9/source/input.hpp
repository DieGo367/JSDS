#ifndef JSDS_INPUT_HPP
#define JSDS_INPUT_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



void buttonEvents(bool down);
void stylusEvents();

bool dispatchKeyboardEvent(bool down, const char *key, const char *code, u8 location, bool shift, bool ctrl, bool alt, bool meta, bool caps);

#endif /* JSDS_INPUT_HPP */