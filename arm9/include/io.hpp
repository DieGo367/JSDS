#ifndef JSDS_IO_HPP
#define JSDS_IO_HPP

#include "jerry/jerryscript.h"

bool onKeyDown(const char16_t codepoint, const char *name, bool shift, int layout, bool repeat);
bool onKeyUp(const char16_t codepoint, const char *name, bool shift, int layout);

void exposeIOAPI(jerry_value_t global);
void releaseIOReferences();

#endif /* JSDS_IO_HPP */