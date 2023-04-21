#ifndef JSDS_IO_HPP
#define JSDS_IO_HPP

#include "jerry/jerryscript.h"

void exposeConsoleKeyboardAPI(jerry_value_t global);
void releaseConsoleKeyboardReferences();

#endif /* JSDS_IO_HPP */