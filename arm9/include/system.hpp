#ifndef JSDS_SYSTEM_HPP
#define JSDS_SYSTEM_HPP

#include "jerry/jerryscript.h"

void queueButtonEvents(bool down);
void queueTouchEvents();
void queueSleepEvent();

void exposeSystemAPI(jerry_value_t global);

#endif /* JSDS_SYSTEM_HPP */