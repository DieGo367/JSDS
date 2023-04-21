#ifndef JSDS_TIMEOUTS_HPP
#define JSDS_TIMEOUTS_HPP

#include "jerry/jerryscript.h"

void timeoutUpdate();
void clearTimeouts();
bool timeoutsExist();

void exposeTimeoutAPI(jerry_value_t global);

#endif /* JSDS_TIMEOUTS_HPP */