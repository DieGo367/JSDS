#ifndef JSDS_SPRITE_HPP
#define JSDS_SPRITE_HPP

#include "jerry/jerryscript.h"

void exposeSpriteAPI(jerry_value_t global);
void releaseSpriteReferences();

#endif /* JSDS_SPRITE_HPP */