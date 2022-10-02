#ifndef JSDS_EXECUTE_H
#define JSDS_EXECUTE_H

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



struct Task {
	jerry_value_t function;
	jerry_value_t thisValue;
	jerry_value_t *args;
	jerry_value_t argCount;
};

extern bool inREPL;

jerry_value_t execute(jerry_value_t parsedCode);
void eventLoop();
void queueTask(jerry_value_t function, jerry_value_t thisValue, jerry_value_t *args, jerry_length_t argCount);
void clearTasks();
void handleError(jerry_value_t error);
void fireLoadEvent();

#endif /* JSDS_EXECUTE_H */