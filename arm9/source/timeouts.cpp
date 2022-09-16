#include "timeouts.h"

#include <map>
#include <nds/timers.h>
#include <stdlib.h>

#include "console.h"
#include "inline.h"
#include "jerry/jerryscript.h"



std::map<int, timeout> timeouts;
int ids = 0;
int nestLevel = 0;
bool timerOn = false;

void timerTick() {
	for (const auto &[id, timeout] : timeouts) {
		if (timeout.remaining > 0) timeouts[id].remaining--;
	}
}

jerry_value_t addTimeout(jerry_value_t handler, jerry_value_t timeoutVal, jerry_value_t *args, u32 argCount, bool repeat) {
	timeout t;
	t.id = ++ids;
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
		timeout t = timeouts[id];
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

void runTimeout(timeout t) {
	if (timeouts.count(t.id) == 0) return;
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

	// execute promises
	jerry_value_t jobResult;
	while (true) {
		jobResult = jerry_run_all_enqueued_jobs();
		if (jerry_value_is_error(jobResult)) {
			consolePrintLiteral(jobResult);
			jerry_release_value(jobResult);
		}
		else break;
	}
	jerry_release_value(jobResult);

	// handle execution result
	if (jerry_value_is_error(result)) {
		consolePrintLiteral(result);
	}
	jerry_release_value(result);

	nestLevel = prevNestLevel;
	jerry_release_value(global);
	if (timeouts.count(t.id) == 0) return;
	if (t.repeat) { // continue interval
		timeouts[t.id].remaining = t.timeout;
	}
	else { // remove timeout
		timeouts.erase(t.id);
		jerry_release_value(t.handler);
		for (u32 i = 0; i < t.argCount; i++) jerry_release_value(t.args[i]);
	}
}

void checkTimeouts() {
	for (auto it = timeouts.begin(); it != timeouts.end(); /* no increment here */) {
		// increment the iterator while keeping the current reference.
		// this allows the loop to continue even if "current" is invalidated when the timeout is removed from the map.
		auto current = it++;
		if (current->second.remaining <= 0) runTimeout(current->second);
	}
	if (timerOn && timeouts.size() == 0) { // disable the timer while it's not being used
		timerStop(0);
		timerOn = false;
	}
}