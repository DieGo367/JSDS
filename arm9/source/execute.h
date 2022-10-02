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
extern bool abortFlag;

jerry_value_t execute(jerry_value_t parsedCode);
void onPromiseRejectionOp(jerry_value_t promise, jerry_promise_rejection_operation_t operation);

void eventLoop();
void queueTask(jerry_value_t function, jerry_value_t thisValue, jerry_value_t *args, jerry_length_t argCount);
void clearTasks();
bool workExists();

void handleError(jerry_value_t error);
void handleRejection(jerry_value_t promise);

void fireEvent(const char *eventName);
void dispatchUnloadEvent();

#endif /* JSDS_EXECUTE_H */