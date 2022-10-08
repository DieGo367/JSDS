#include "timeouts.h"

#include <map>
#include <nds/timers.h>
#include <stdlib.h>

#include "console.h"
#include "inline.h"
#include "jerry/jerryscript.h"
#include "tasks.h"



std::map<int, Timeout> timeouts;
int ids = 0;
int internalIds = 0;
int nestLevel = 0;
bool timerOn = false;

void timerTick() {
	for (const auto &[id, timeout] : timeouts) {
		timeouts[id].remaining--;
	}
}

jerry_value_t addTimeout(jerry_value_t handler, jerry_value_t timeoutVal, jerry_value_t *args, u32 argCount, bool repeat, bool isInternal) {
	Timeout t;
	t.id = isInternal ? --internalIds : ++ids;
	jerry_value_t timeoutNumVal = jerry_value_to_number(timeoutVal);
	int ticks = jerry_value_as_int32(timeoutNumVal);
	jerry_release_value(timeoutNumVal);
	if (ticks < 0) ticks = 0;
	if (nestLevel > 5 && ticks < 4) ticks = 4;
	t.timeout = ticks;
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
	t.remaining = t.timeout;
	t.queued = false;

	timeouts[t.id] = t;
	if (!timerOn) {
		timerStart(0, ClockDivider_1024, TIMER_FREQ_1024(1000), timerTick);
		timerOn = true;
	}
	return jerry_create_number(t.id);
}

void clearTimeout(jerry_value_t idVal) {
	jerry_value_t idNumVal = jerry_value_to_number(idVal);
	int id = jerry_value_as_int32(idNumVal);
	jerry_release_value(idNumVal);
	if (timeouts.count(id) != 0) {
		Timeout t = timeouts[id];
		timeouts.erase(id);
		jerry_release_value(t.handler);
		for (u32 i = 0; i < t.argCount; i++) jerry_release_value(t.args[i]);
		// check if we can disable the timer
		if (timerOn && timeouts.size() == 0) {
			timerStop(0);
			timerOn = false;
		}
	}
}

void runTimeoutTask(const jerry_value_t args[], u32 argCount) {
	int id = jerry_get_number_value(args[0]);
	if (timeouts.count(id) == 0) return;
	Timeout t = timeouts[id];
	jerry_value_t global = jerry_get_global_object();
	int prevNestLevel = nestLevel;

	// execute handler
	jerry_value_t result;
	if (jerry_value_is_function(t.handler)) {
		result = jerry_call_function(t.handler, global, t.args, t.argCount);
	}
	else {
		jerry_length_t handlerSize;
		char *handlerStr = getAsString(t.handler, &handlerSize);
		result = jerry_eval((jerry_char_t *) handlerStr, handlerSize, JERRY_PARSE_NO_OPTS);
		free(handlerStr);
	}
	if (!abortFlag) {
		runMicrotasks();
		if (jerry_value_is_error(result)) handleError(result, false);
	}
	jerry_release_value(result);

	nestLevel = prevNestLevel;
	jerry_release_value(global);
	if (timeouts.count(t.id) > 0) {
		if (t.repeat) { // continue interval
			timeouts[t.id].remaining = t.timeout;
			timeouts[t.id].queued = false;
		}
		else { // remove timeout
			timeouts.erase(t.id);
			jerry_release_value(t.handler);
			for (u32 i = 0; i < t.argCount; i++) jerry_release_value(t.args[i]);
		}
	}
}

void timeoutUpdate() {
	if (!timerOn) return;
	int minAmount = 0;
	while (minAmount < 1) {
		minAmount = 1;
		for (const auto &[id, timeout] : timeouts) {
			if (!timeout.queued && timeout.remaining < minAmount) minAmount = timeout.remaining;
		}
		if (minAmount < 1) for (const auto &[id, timeout] : timeouts) {
			if (!timeout.queued && timeout.remaining == minAmount) {
				jerry_value_t idVal = jerry_create_number(id);
				queueTask(runTimeoutTask, &idVal, 1);
				jerry_release_value(idVal);
				timeouts[id].queued = true;
			}
		}
	}
	if (timerOn && timeouts.size() == 0) { // disable the timer while it's not being used
		timerStop(0);
		timerOn = false;
	}
}

void clearTimeouts() {
	for (const auto &[id, timeout] : timeouts) {
		jerry_release_value(timeout.handler);
		for (u32 i = 0; i < timeout.argCount; i++) jerry_release_value(timeout.args[i]);
	}
	timeouts.clear();
}

bool timeoutsExist() {
	return timeouts.size() > 0;
}