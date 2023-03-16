#include "timeouts.hpp"

#include <map>
#include <nds/timers.h>
#include <stdlib.h>

#include "error.hpp"
#include "event.hpp"
#include "helpers.hpp"
#include "jerry/jerryscript.h"
#include "logging.hpp"



struct Timeout {
	int id;
	int timeout;
	jerry_value_t handler;
	jerry_value_t *args;
	u32 argCount;
	int nestLevel;
	int remaining;
	bool repeat;
	bool queued;
};

std::map<int, Timeout> timeouts;
std::map<int, int> counters;
int ids = 0;
int internalIds = 0;
int counterIds = 0;
int nestLevel = 0;
bool timerOn = false;
int timerUsage = 0;

void timingTick() {
	for (const auto &[id, timeout] : timeouts) {
		timeouts[id].remaining--;
	}
	for (const auto &[id, tick] : counters) {
		counters[id] = counters[id] + 1;
	}
}
void timingUse() {
	timerUsage++;
	if (!timerOn) {
		timerStart(0, ClockDivider_1024, TIMER_FREQ_1024(1000), timingTick);
		timerOn = true;
	}
}
void timingDone() {
	if (--timerUsage <= 0) {
		timerUsage = 0;
		if (timerOn) {
			timerStop(0);
			timerOn = false;
		}
	}
}

jerry_value_t addTimeout(jerry_value_t handler, const jerry_value_t *args, u32 argCount, int ticks, bool repeat, bool isInternal) {
	Timeout t;
	t.id = isInternal ? --internalIds : ++ids;
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
	timingUse();
	return jerry_create_number(t.id);
}

void clearTimeout(jerry_value_t idVal) {
	jerry_value_t idNum = jerry_value_to_number(idVal);
	int id = jerry_value_as_int32(idNum);
	jerry_release_value(idNum);
	if (timeouts.count(id) != 0) {
		Timeout t = timeouts[id];
		timeouts.erase(id);
		jerry_release_value(t.handler);
		for (u32 i = 0; i < t.argCount; i++) jerry_release_value(t.args[i]);
		timingDone();
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
		char *handler = getAsString(t.handler, &handlerSize);
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
			timeouts[t.id].remaining = t.timeout;
			timeouts[t.id].queued = false;
		}
		else { // remove timeout
			timeouts.erase(t.id);
			jerry_release_value(t.handler);
			for (u32 i = 0; i < t.argCount; i++) jerry_release_value(t.args[i]);
			timingDone();
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
	}
	timeouts.clear();
}

bool timeoutsExist() {
	return timeouts.size() > 0;
}

int counterAdd() {
	counters[++counterIds] = 0;
	timingUse();
	return counterIds;
}
int counterGet(int id) {
	return counters.at(id);
}
void counterRemove(int id) {
	counters.erase(id);
	timingDone();
}