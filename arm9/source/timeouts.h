#ifndef JSDS_TIMEOUTS_H
#define JSDS_TIMEOUTS_H

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



struct Timeout {
	int id;
	int timeout;
	jerry_value_t handler;
	jerry_value_t *args;
	u32 argCount;
	int nestLevel;
	int remaining;
	bool repeat;
	bool queued;
};

jerry_value_t addTimeout(jerry_value_t handler, jerry_value_t ticks, jerry_value_t *args, u32 argCount, bool repeat);
void clearTimeout(jerry_value_t idVal);
jerry_value_t runTimeoutTask(jerry_value_t function, jerry_value_t thisValue, const jerry_value_t args[], u32 argCount);
void checkTimeouts();
void clearTimeouts();

#endif /* JSDS_TIMEOUTS_H */