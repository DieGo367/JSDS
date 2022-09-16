#ifndef JSDS_TIMEOUTS_H
#define JSDS_TIMEOUTS_H

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



struct timeout {
	int id;
	int timeout;
	jerry_value_t handler;
	jerry_value_t *args;
	u32 argCount;
	bool repeat;
	int nestLevel;
	int remaining;
};

jerry_value_t addTimeout(jerry_value_t handler, jerry_value_t ticks, jerry_value_t *args, u32 argCount, bool repeat);
void clearTimeout(jerry_value_t idVal);
void checkTimeouts();

#endif /* JSDS_TIMEOUTS_H */