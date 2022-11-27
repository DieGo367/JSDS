#ifndef JSDS_INPUT_HPP
#define JSDS_INPUT_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



void buttonEvents(bool down);
void stylusEvents();

void onKeyDown(const char *key, const char *code, bool shift, bool ctrl, bool alt, bool meta, bool caps);

#endif /* JSDS_INPUT_HPP */