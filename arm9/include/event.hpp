#ifndef JSDS_EVENT_HPP
#define JSDS_EVENT_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



typedef void (*TaskFunction) (const jerry_value_t *args, u32 argCount);
struct Task {
	TaskFunction run;
	jerry_value_t *args;
	u32 argCount;
};

extern bool inREPL;
extern bool abortFlag;
extern bool userClosed;
extern u8 dependentEvents;

enum DependentEvent {
	vblank     = BIT(0),
	buttondown = BIT(1),
	buttonup   = BIT(2),
	touchstart = BIT(3),
	touchmove  = BIT(4),
	touchend   = BIT(5),
	keydown    = BIT(6),
	keyup      = BIT(7)
};

void runTasks();
void queueTask(TaskFunction run, const jerry_value_t *args, u32 argCount);
void clearTasks();
void runMicrotasks();

void runParsedCodeTask(const jerry_value_t *args, u32 argCount);

jerry_value_t createEvent(jerry_value_t type, bool cancelable);
jerry_value_t createEvent(const char *type, bool cancelable);
bool dispatchEvent(jerry_value_t target, jerry_value_t event, bool sync);
void queueEvent(jerry_value_t target, jerry_value_t event, jerry_external_handler_t callback = NULL);
void queueEventName(const char *eventName, jerry_external_handler_t callback = NULL);

void eventLoop();

void exposeEventAPI(jerry_value_t global);
void releaseEventReferences();

#endif /* JSDS_EVENT_HPP */