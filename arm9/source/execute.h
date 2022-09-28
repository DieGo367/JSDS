#ifndef JSDS_EXECUTE_H
#define JSDS_EXECUTE_H

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



extern bool inREPL;

jerry_value_t execute(jerry_value_t parsedCode);
void handleError(jerry_value_t error);
void fireLoadEvent();

#endif /* JSDS_EXECUTE_H */