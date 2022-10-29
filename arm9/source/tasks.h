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
extern int fatInitSuccess;
extern bool localStorageShouldSave;

enum DependentEvent {
	vblank     = BIT(0),
	buttondown = BIT(1),
	buttonup   = BIT(2),
	stylusdown = BIT(3),
	stylusmove = BIT(4),
	stylusup   = BIT(5),
	keydown    = BIT(6),
	keyup      = BIT(7)
};

void onPromiseRejectionOp(jerry_value_t promise, jerry_promise_rejection_operation_t operation);
void runMicrotasks();

void runTasks();
void queueTask(void (*run) (const jerry_value_t *, u32), const jerry_value_t *args, u32 argCount);
void clearTasks();

void loadStorage(const char *resourceName);

void runParsedCodeTask(const jerry_value_t *args, u32 argCount);

void handleError(jerry_value_t error, bool sync);
void handleRejection(jerry_value_t promise);

bool dispatchEvent(jerry_value_t target, jerry_value_t event, bool sync);
void queueEvent(jerry_value_t target, jerry_value_t event);
void queueEventName(const char *eventName);

bool dispatchKeyboardEvent(bool down, const char *key, const char *code, u8 location, bool shift, bool ctrl, bool alt, bool meta, bool caps);

void eventLoop();

#endif /* JSDS_TASKS_H */