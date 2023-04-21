#include "timeouts.hpp"

#include <map>
#include <stdlib.h>

#include "error.hpp"
#include "event.hpp"
#include "helpers.hpp"
#include "jerry/jerryscript.h"
#include "logging.hpp"
#include "util/timing.hpp"



struct Timeout {
	int id;
	int duration;
	jerry_value_t handler;
	jerry_value_t *args;
	u32 argCount;
	int nestLevel;
	bool repeat;
	bool queued;
};

std::map<int, Timeout> timeouts;
int nestLevel = 0;

int addTimeout(jerry_value_t handler, const jerry_value_t *args, u32 argCount, int ticks, bool repeat) {
	Timeout t;
	if (ticks < 0) ticks = 0;
	if (nestLevel > 5 && ticks < 4) ticks = 4;
	t.id = timerAdd(ticks);
	t.duration = ticks;
	t.handler = jerry_acquire_value(handler);
	t.argCount = argCount;
	if (argCount > 0) {
		t.args = (jerry_value_t *) malloc(argCount * sizeof(jerry_value_t));
		for (u32 i = 0; i < argCount; i++) {
			t.args[i] = jerry_acquire_value(args[i]);
		}
	}
	else t.args = NULL;
	t.repeat = repeat;
	t.nestLevel = nestLevel + 1;
	t.queued = false;

	timeouts[t.id] = t;
	return t.id;
}

void clearTimeout(int id) {
	if (timeouts.count(id) != 0) {
		Timeout t = timeouts[id];
		timeouts.erase(id);
		jerry_release_value(t.handler);
		for (u32 i = 0; i < t.argCount; i++) jerry_release_value(t.args[i]);
		timerRemove(id);
	}
}

void runTimeoutTask(const jerry_value_t args[], u32 argCount) {
	int id = jerry_get_number_value(args[0]);
	if (timeouts.count(id) == 0) return;
	Timeout t = timeouts[id];
	int prevNestLevel = nestLevel;

	// execute handler
	jerry_value_t resultVal;
	if (jerry_value_is_function(t.handler)) {
		resultVal = jerry_call_function(t.handler, ref_global, t.args, t.argCount);
	}
	else {
		jerry_length_t handlerSize;
		char *handler = toRawString(t.handler, &handlerSize);
		resultVal = jerry_eval((jerry_char_t *) handler, handlerSize, JERRY_PARSE_NO_OPTS);
		free(handler);
	}
	if (!abortFlag) {
		runMicrotasks();
		if (jerry_value_is_error(resultVal)) handleError(resultVal, false);
	}
	jerry_release_value(resultVal);

	nestLevel = prevNestLevel;
	if (timeouts.count(t.id) > 0) {
		if (t.repeat) { // continue interval
			timeouts[t.id].queued = false;
			timerSet(t.id, t.duration);
		}
		else { // remove timeout
			timeouts.erase(t.id);
			jerry_release_value(t.handler);
			for (u32 i = 0; i < t.argCount; i++) jerry_release_value(t.args[i]);
			timerRemove(t.id);
		}
	}
}

void timeoutUpdate() {
	if (!timingOn()) return;
	int minAmount = 0;
	while (minAmount < 1) {
		minAmount = 1;
		for (const auto &[id, timeout] : timeouts) {
			int remaining = timerGet(id);
			if (!timeout.queued && remaining < minAmount) minAmount = remaining;
		}
		if (minAmount < 1) for (const auto &[id, timeout] : timeouts) {
			int remaining = timerGet(id);
			if (!timeout.queued && remaining == minAmount) {
				jerry_value_t idNum = jerry_create_number(id);
				queueTask(runTimeoutTask, &idNum, 1);
				jerry_release_value(idNum);
				timeouts[id].queued = true;
			}
		}
	}
}

void clearTimeouts() {
	for (const auto &[id, timeout] : timeouts) {
		jerry_release_value(timeout.handler);
		for (u32 i = 0; i < timeout.argCount; i++) jerry_release_value(timeout.args[i]);
		timerRemove(id);
	}
	timeouts.clear();
}

bool timeoutsExist() {
	return timeouts.size() > 0;
}



FUNCTION(setTimeout) {
	if (argCount >= 2) {
		return jerry_create_number(addTimeout(args[0], args + 2, argCount - 2, jerry_value_as_int32(args[1]), false));
	}
	else return jerry_create_number(addTimeout(argCount > 0 ? args[0] : JS_UNDEFINED, NULL, 0, 0, false));
}

FUNCTION(setInterval) {
	if (argCount >= 2) {
		return jerry_create_number(addTimeout(args[0], args + 2, argCount - 2, jerry_value_as_int32(args[1]), true));
	}
	else return jerry_create_number(addTimeout(argCount > 0 ? args[0] : JS_UNDEFINED, NULL, 0, 0, true));
}

FUNCTION(clearInterval) {
	if (argCount > 0) clearTimeout(jerry_value_as_int32(args[0]));
	return JS_UNDEFINED;
}

void exposeTimeoutAPI(jerry_value_t global) {
	setMethod(global, "clearInterval", clearInterval);
	setMethod(global, "clearTimeout", clearInterval);
	setMethod(global, "setInterval", setInterval);
	setMethod(global, "setTimeout", setTimeout);
}