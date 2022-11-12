#ifndef JSDS_EVENT_HPP
#define JSDS_EVENT_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



struct Task {
	void (*run) (const jerry_value_t *args, u32 argCount);
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
	stylusdown = BIT(3),
	stylusmove = BIT(4),
	stylusup   = BIT(5),
	keydown    = BIT(6),
	keyup      = BIT(7)
};

void runTasks();
void queueTask(void (*run) (const jerry_value_t *, u32), const jerry_value_t *args, u32 argCount);
void clearTasks();
void runMicrotasks();

void runParsedCodeTask(const jerry_value_t *args, u32 argCount);

bool dispatchEvent(jerry_value_t target, jerry_value_t event, bool sync);
void queueEvent(jerry_value_t target, jerry_value_t event);
void queueEventName(const char *eventName);

void eventLoop();

#endif /* JSDS_EVENT_HPP */