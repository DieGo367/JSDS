#ifndef JSDS_EXECUTE_H
#define JSDS_EXECUTE_H

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



struct Task {
	void (*run) (const jerry_value_t *args, u32 argCount);
	jerry_value_t *args;
	u32 argCount;
};

extern bool inREPL;
extern bool abortFlag;

jerry_value_t execute(jerry_value_t parsedCode);
void onPromiseRejectionOp(jerry_value_t promise, jerry_promise_rejection_operation_t operation);
void runMicrotasks();

void runTasks();
void queueTask(void (*run) (const jerry_value_t *, u32), const jerry_value_t *args, u32 argCount);
void clearTasks();
bool workExists();

void handleError(jerry_value_t error, bool sync);
void handleRejection(jerry_value_t promise);

bool dispatchEvent(jerry_value_t target, jerry_value_t event, bool sync);
void queueEvent(jerry_value_t target, jerry_value_t event);
void queueEventName(const char *eventName);

#endif /* JSDS_EXECUTE_H */