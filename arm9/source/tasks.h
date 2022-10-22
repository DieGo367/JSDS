#ifndef JSDS_TASKS_H
#define JSDS_TASKS_H

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



struct Task {
	void (*run) (const jerry_value_t *args, u32 argCount);
	jerry_value_t *args;
	u32 argCount;
};

extern bool inREPL;
extern bool abortFlag;
extern u8 dependentEvents;
extern bool localStorageShouldSave;

enum DependentEvent {
	vblank = 0b1,
	buttondown = 0b10,
	buttonup = 0b100,
	stylusdown = 0b1000,
	stylusmove = 0b10000,
	stylusup = 0b100000,
};

void onPromiseRejectionOp(jerry_value_t promise, jerry_promise_rejection_operation_t operation);
void runMicrotasks();

void runTasks();
void queueTask(void (*run) (const jerry_value_t *, u32), const jerry_value_t *args, u32 argCount);
void clearTasks();
void eventLoop();

void loadStorage(const char *resourceName);
void saveStorage();

void runParsedCodeTask(const jerry_value_t *args, u32 argCount);

void handleError(jerry_value_t error, bool sync);
void handleRejection(jerry_value_t promise);

bool dispatchEvent(jerry_value_t target, jerry_value_t event, bool sync);
void queueEvent(jerry_value_t target, jerry_value_t event);
void queueEventName(const char *eventName);

void buttonEvents(bool down);
void stylusEvents();

#endif /* JSDS_TASKS_H */