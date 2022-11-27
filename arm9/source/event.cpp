#include "event.hpp"

#include <nds/arm9/input.h>
#include <nds/interrupts.h>
extern "C" {
#include <nds/system.h>
}
#include <queue>
#include <stdlib.h>

#include "api.hpp"
#include "error.hpp"
#include "inline.hpp"
#include "io/console.hpp"
#include "io/keyboard.hpp"
#include "input.hpp"
#include "jerry/jerryscript.h"
#include "logging.hpp"
#include "storage.hpp"
#include "timeouts.hpp"



bool inREPL = false;
bool abortFlag = false;
bool userClosed = false;
u8 dependentEvents = 0;

std::queue<Task> taskQueue;

// Executes all tasks currently in the task queue (newly enqueued tasks are not run).
void runTasks() {
	u32 size = taskQueue.size();
	while (size-- && !abortFlag) {
		Task task = taskQueue.front();
		taskQueue.pop();
		task.run(task.args, task.argCount);
		for (u32 i = 0; i < task.argCount; i++) jerry_release_value(task.args[i]);
	}
}

void queueTask(void (*run) (const jerry_value_t *, u32), const jerry_value_t *args, u32 argCount) {
	Task task;
	task.run = run;
	task.args = (jerry_value_t *) malloc(argCount * sizeof(jerry_value_t));
	for (u32 i = 0; i < argCount; i++) task.args[i] = jerry_acquire_value(args[i]);
	task.argCount = argCount;
	taskQueue.push(task);
}

void clearTasks() {
	u32 size = taskQueue.size();
	while (size--) {
		Task task = taskQueue.front();
		taskQueue.pop();
		for (u32 i = 0; i < task.argCount; i++) jerry_release_value(task.args[i]);
	}
}

void runMicrotasks() {
	jerry_value_t microtaskResult;
	while (true) {
		microtaskResult = jerry_run_all_enqueued_jobs();
		if (jerry_value_is_error(microtaskResult)) {
			handleError(microtaskResult, false);
			jerry_release_value(microtaskResult);
		}
		else break;
	}
	jerry_release_value(microtaskResult);
	handleRejectedPromises();
}

// Task which runs some previously parsed code.
void runParsedCodeTask(const jerry_value_t *args, u32 argCount) {
	jerry_value_t result;
	if (jerry_value_is_error(args[0])) result = jerry_acquire_value(args[0]);
	else result = jerry_run(args[0]);
	if (!abortFlag) {
		runMicrotasks();
		if (inREPL) {
			printf("-> ");
			logLiteral(result);
			putchar('\n');
		}
		else if (jerry_value_is_error(result)) {
			logLiteral(result);
			putchar('\n');
			abortFlag = true;
		}
	}
	jerry_release_value(result);
}

/* Dispatches event onto target.
 * If sync is true, runs "synchronously" (no microtasks are run). JS functions should set it to true.
 * Returns true if the event was canceled, false otherwise.
 */
bool dispatchEvent(jerry_value_t target, jerry_value_t event, bool sync) {
	jerry_value_t dispatchStr = createString("dispatch");
	jerry_value_t eventPhaseStr = createString("eventPhase");
	jerry_value_t targetStr = createString("target");
	jerry_value_t currentTargetStr = createString("currentTarget");

	jerry_set_internal_property(event, dispatchStr, True);

	jerry_value_t AT_TARGET = jerry_create_number(2);
	jerry_set_internal_property(event, eventPhaseStr, AT_TARGET);
	jerry_release_value(AT_TARGET);

	jerry_set_internal_property(event, targetStr, target);
	jerry_set_internal_property(event, currentTargetStr, target);

	jerry_value_t eventListeners = getInternalProperty(target, "eventListeners");
	jerry_value_t eventType = getInternalProperty(event, "type");
	jerry_value_t listenersOfType = jerry_get_property(eventListeners, eventType);
	if (jerry_value_is_array(listenersOfType)) {
		jerry_value_t sliceFunc = getProperty(listenersOfType, "slice");
		jerry_value_t listenersCopy = jerry_call_function(sliceFunc, listenersOfType, NULL, 0);
		jerry_release_value(sliceFunc);

		u32 length = jerry_get_array_length(listenersCopy);
		jerry_value_t removedStr = createString("removed");
		jerry_value_t onceStr = createString("once");
		jerry_value_t passiveStr = createString("passive");
		jerry_value_t callbackStr = createString("callback");
		jerry_value_t spliceFunc = getProperty(listenersOfType, "splice");
		jerry_value_t inPassiveListenerStr = createString("inPassiveListener");
		jerry_value_t handleEventStr = createString("handleEvent");
		jerry_value_t stopImmediatePropagationStr = createString("stopImmediatePropagation");

		for (u32 i = 0; i < length && !abortFlag; i++) {
			jerry_value_t stopImmediate = jerry_get_internal_property(event, stopImmediatePropagationStr);
			bool stop = jerry_get_boolean_value(stopImmediate);
			jerry_release_value(stopImmediate);
			if (stop) break;

			jerry_value_t listener = jerry_get_property_by_index(listenersCopy, i);
			jerry_value_t removedVal = jerry_get_property(listener, removedStr);
			bool removed = jerry_get_boolean_value(removedVal);
			jerry_release_value(removedVal);
			if (!removed) {
				jerry_value_t onceVal = jerry_get_property(listener, onceStr);
				bool once = jerry_get_boolean_value(onceVal);
				jerry_release_value(onceVal);
				if (once) {
					jerry_value_t spliceArgs[2] = {jerry_create_number(i), jerry_create_number(1)};
					jerry_release_value(jerry_call_function(spliceFunc, listenersOfType, spliceArgs, 2));
					jerry_release_value(spliceArgs[1]);
					jerry_release_value(spliceArgs[0]);
					jerry_release_value(jerry_set_property(listener, removedStr, True));
				}
				jerry_value_t passiveVal = jerry_get_property(listener, passiveStr);
				bool passive = jerry_get_boolean_value(passiveVal);
				jerry_release_value(passiveVal);
				if (passive) jerry_set_internal_property(event, inPassiveListenerStr, True);
				
				jerry_value_t callbackVal = jerry_get_property(listener, callbackStr);
				jerry_value_t result;
				bool ran = false;
				if (jerry_value_is_function(callbackVal)) {
					result = jerry_call_function(callbackVal, target, &event, 1);
					ran = true;
				}
				else if (jerry_value_is_object(callbackVal)) {
					jerry_value_t handler = jerry_get_property(callbackVal, handleEventStr);
					if (jerry_value_is_function(handler)) {
						result = jerry_call_function(handler, target, &event, 1);
						ran = true;
					}
					jerry_release_value(handler);
				}
				if (ran) {
					if (!abortFlag) {
						if (!sync) runMicrotasks();
						if (jerry_value_is_error(result)) handleError(result, sync);
					}
					jerry_release_value(result);
				}
				jerry_release_value(callbackVal);

				jerry_set_internal_property(event, inPassiveListenerStr, False);
			}
			jerry_release_value(listener);
		}

		jerry_release_value(stopImmediatePropagationStr);
		jerry_release_value(handleEventStr);
		jerry_release_value(inPassiveListenerStr);
		jerry_release_value(spliceFunc);
		jerry_release_value(callbackStr);
		jerry_release_value(passiveStr);
		jerry_release_value(onceStr);
		jerry_release_value(removedStr);
		jerry_release_value(listenersCopy);
	}
	jerry_release_value(listenersOfType);
	jerry_release_value(eventType);
	jerry_release_value(eventListeners);

	jerry_value_t NONE = jerry_create_number(0);
	jerry_set_internal_property(event, eventPhaseStr, NONE);
	jerry_release_value(NONE);
	jerry_set_internal_property(event, targetStr, null);
	jerry_set_internal_property(event, currentTargetStr, null);
	jerry_set_internal_property(event, dispatchStr, False);
	setInternalProperty(event, "stopPropagation", False);
	setInternalProperty(event, "stopImmediatePropagation", False);
	
	jerry_release_value(currentTargetStr);
	jerry_release_value(targetStr);
	jerry_release_value(eventPhaseStr);
	jerry_release_value(dispatchStr);
	
	jerry_value_t canceledVal = getInternalProperty(event, "defaultPrevented");
	bool canceled = jerry_get_boolean_value(canceledVal);
	jerry_release_value(canceledVal);
	return canceled;
}

// Task which dispatches an event. Args: EventTarget, Event
void dispatchEventTask(const jerry_value_t *args, u32 argCount) {
	dispatchEvent(args[0], args[1], false);
}

// Queues a task to dispatch event onto target.
void queueEvent(jerry_value_t target, jerry_value_t event) {
	jerry_value_t args[2] = {target, event};
	queueTask(dispatchEventTask, args, 2);
}

// Queues a task that fires a simple, uncancelable event on the global context.
void queueEventName(const char *eventName) {
	jerry_value_t eventNameVal = createString(eventName);
	jerry_value_t event = jerry_construct_object(ref_Event, &eventNameVal, 1);
	jerry_release_value(eventNameVal);
	setInternalProperty(event, "isTrusted", True);
	queueEvent(ref_global, event);
	jerry_release_value(event);
}

void onSleep() {
	jerry_value_t args[2] = {createString("sleep"), jerry_create_object()};
	setProperty(args[1], "cancelable", True);
	jerry_value_t event = jerry_construct_object(ref_Event, args, 2);
	bool canceled = dispatchEvent(ref_global, event, false);
	jerry_release_value(event);
	jerry_release_value(args[0]);
	jerry_release_value(args[1]);
	if (!canceled) systemSleep();
}
void onWake() {
	jerry_value_t args[2] = {createString("wake"), jerry_create_object()};
	jerry_value_t event = jerry_construct_object(ref_Event, args, 2);
	dispatchEvent(ref_global, event, false);
	jerry_release_value(event);
	jerry_release_value(args[0]);
	jerry_release_value(args[1]);
}


/* The Event Loop™
 * On every vblank, run necessary operations before then executing the current task queue.
 * Returns when there is no work left to do (not in the REPL and no tasks/timeouts left to execute) or when abortFlag is set.
 */
void eventLoop() {
	while (!abortFlag && (inREPL || dependentEvents || taskQueue.size() > 0 || timeoutsExist())) {
		swiWaitForVBlank();
		if (dependentEvents & vblank) queueEventName("vblank");
		scanKeys();
		if (keysDown() & KEY_LID) onSleep();
		if (keysUp() & KEY_LID) onWake();
		if (dependentEvents & (buttondown)) buttonEvents(true);
		if (dependentEvents & (buttonup)) buttonEvents(false);
		if (dependentEvents & (stylusdown | stylusmove | stylusup)) stylusEvents();
		timeoutUpdate();
		runTasks();
		if (inREPL) {
			if (keyboardEnterPressed) {
				putchar('\n');
				jerry_value_t parsedCode = jerry_parse(
					(const jerry_char_t *) "REPL", 4,
					(const jerry_char_t *) keyboardBuffer(), keyboardBufferLen(),
					JERRY_PARSE_STRICT_MODE
				);
				queueTask(runParsedCodeTask, &parsedCode, 1);
				jerry_release_value(parsedCode);
				keyboardClearBuffer();
			}
		}
		if (isKeyboardOpen()) keyboardUpdate();
		storageUpdate();
	}
}