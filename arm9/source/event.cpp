#include "event.hpp"

#include <nds/arm9/input.h>
#include <nds/interrupts.h>
extern "C" {
#include <nds/system.h>
}
#include <queue>
#include <stdlib.h>

#include "error.hpp"
#include "file.hpp"
#include "helpers.hpp"
#include "io/console.hpp"
#include "io/keyboard.hpp"
#include "jerry/jerryscript.h"
#include "logging.hpp"
#include "timeouts.hpp"
#include "util/unicode.hpp"



bool inREPL = false;
bool abortFlag = false;
bool userClosed = false;
u8 dependentEvents = 0;
bool pauseKeyEvents = false;

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

void queueTask(TaskFunction run, const jerry_value_t *args, u32 argCount) {
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
	jerry_value_t microtaskResultVal;
	while (true) {
		microtaskResultVal = jerry_run_all_enqueued_jobs();
		if (jerry_value_is_error(microtaskResultVal)) {
			handleError(microtaskResultVal, false);
			jerry_release_value(microtaskResultVal);
		}
		else break;
	}
	jerry_release_value(microtaskResultVal);
	handleRejectedPromises();
}

// Task which runs some previously parsed code.
void runParsedCodeTask(const jerry_value_t *args, u32 argCount) {
	jerry_value_t resultVal;
	if (jerry_value_is_error(args[0])) resultVal = jerry_acquire_value(args[0]);
	else resultVal = jerry_run(args[0]);
	if (!abortFlag) {
		runMicrotasks();
		if (jerry_value_is_error(resultVal)) {
			u16 previousColor = consoleSetColor(LOGCOLOR_ERROR);
			u16 previousBG = consoleSetBackground(LOGCOLOR_ERROR_BG);
			logLiteral(resultVal);
			putchar('\n');
			consoleSetColor(previousColor);
			consoleSetBackground(previousBG);
			abortFlag = !inREPL;
		}
		else if (inREPL) {
			logLiteral(resultVal);
			putchar('\n');
		}
	}
	if (inREPL) setProperty(ref_global, "_", resultVal);
	jerry_release_value(resultVal);
}

/**
 * Dispatches event onto target.
 * If sync is true, runs "synchronously" (no microtasks are run). JS functions should set it to true.
 * Returns true if the event was canceled, false otherwise.
 */
bool dispatchEvent(jerry_value_t target, jerry_value_t event, bool sync) {
	jerry_value_t targetProp = String("target");
	jerry_set_internal_property(event, targetProp, target);

	jerry_value_t eventListenersObj = getInternalProperty(target, "eventListeners");
	jerry_value_t typeStr = getInternalProperty(event, "type");
	jerry_value_t listenersArr = jerry_get_property(eventListenersObj, typeStr); // listeners of given type
	if (jerry_value_is_array(listenersArr)) {
		jerry_value_t listenersCopyArr = jerry_call_function(ref_func_slice, listenersArr, NULL, 0);

		u32 length = jerry_get_array_length(listenersCopyArr);
		jerry_value_t removedProp = String("removed");
		jerry_value_t onceProp = String("once");
		jerry_value_t callbackProp = String("callback");
		jerry_value_t stopImmediatePropagationProp = String("stopImmediatePropagation");

		for (u32 i = 0; i < length && !abortFlag; i++) {
			if (JS_testInternalProperty(event, stopImmediatePropagationProp)) break;

			jerry_value_t listenerObj = jerry_get_property_by_index(listenersCopyArr, i);
			if (!JS_testProperty(listenerObj, removedProp)) {
				if (JS_testProperty(listenerObj, onceProp)) {
					arraySplice(listenersArr, i, 1);
					jerry_release_value(jerry_set_property(listenerObj, removedProp, JS_TRUE));
				}
				
				jerry_value_t callbackVal = jerry_get_property(listenerObj, callbackProp);
				jerry_value_t resultVal;
				if (jerry_value_is_function(callbackVal)) {
					resultVal = jerry_call_function(callbackVal, target, &event, 1);
					if (!abortFlag) {
						if (!sync) runMicrotasks();
						if (jerry_value_is_error(resultVal)) handleError(resultVal, sync);
					}
					jerry_release_value(resultVal);
				}
				jerry_release_value(callbackVal);
			}
			jerry_release_value(listenerObj);
		}

		jerry_release_value(stopImmediatePropagationProp);
		jerry_release_value(callbackProp);
		jerry_release_value(onceProp);
		jerry_release_value(removedProp);
		jerry_release_value(listenersCopyArr);
	}
	jerry_release_value(listenersArr);
	jerry_release_value(typeStr);
	jerry_release_value(eventListenersObj);

	jerry_set_internal_property(event, targetProp, JS_NULL);
	setInternalProperty(event, "stopImmediatePropagation", JS_FALSE);
	jerry_release_value(targetProp);
	
	return testInternalProperty(event, "defaultPrevented");
}

// Task which dispatches an event. Args: EventTarget, Event, optional callbackFunc
void dispatchEventTask(const jerry_value_t *args, u32 argCount) {
	bool canceled = dispatchEvent(args[0], args[1], false);
	if (!canceled && argCount > 2) jerry_call_function(args[2], args[0], args + 1, 1);
}

// Queues a task to dispatch event onto target.
void queueEvent(jerry_value_t target, jerry_value_t event, jerry_external_handler_t callback) {
	jerry_value_t eventArgs[3] = {target, event};
	if (callback != NULL) {
		eventArgs[2] = jerry_create_external_function(callback);
		queueTask(dispatchEventTask, eventArgs, 3);
		jerry_release_value(eventArgs[2]);
	}
	else queueTask(dispatchEventTask, eventArgs, 2);
}

// Queues a task that fires a simple event on the global context. Becomes canceleable if callback is provided.
void queueEventName(const char *eventName, jerry_external_handler_t callback) {
	jerry_value_t eventNameStr = String(eventName);
	jerry_value_t eventObj;
	if (callback != NULL) {
		jerry_value_t eventArgs[2] = {eventNameStr, jerry_create_object()};
		setProperty(eventArgs[1], "cancelable", JS_TRUE);
		eventObj = jerry_construct_object(ref_Event, eventArgs, 2);
		jerry_release_value(eventArgs[1]);
	}
	else eventObj = jerry_construct_object(ref_Event, &eventNameStr, 1);
	jerry_release_value(eventNameStr);
	queueEvent(ref_global, eventObj, callback);
	jerry_release_value(eventObj);
}

FUNCTION(sleepCallback) {
	systemSleep();
	return JS_UNDEFINED;
}

void queueButtonEvents(bool down) {
	u32 set = down ? keysDown() : keysUp();
	if (set) {
		jerry_value_t buttonProp = String("button");
		jerry_value_t eventArgs[2] = {String(down ? "buttondown" : "buttonup"), jerry_create_object()};
		jerry_release_value(jerry_set_property(eventArgs[1], buttonProp, JS_NULL));
		jerry_value_t eventObj = jerry_construct_object(ref_Event, eventArgs, 2);
		#define TEST_VALUE(button, keyCode) if (set & keyCode) {\
			jerry_value_t buttonStr = String(button); \
			jerry_set_internal_property(eventObj, buttonProp, buttonStr); \
			jerry_release_value(buttonStr); \
			queueEvent(ref_global, eventObj); \
		}
		FOR_BUTTONS(TEST_VALUE)
		jerry_release_value(eventObj);
		jerry_release_value(eventArgs[0]);
		jerry_release_value(eventArgs[1]);
		jerry_release_value(buttonProp);
	}
}

u16 prevX = 0, prevY = 0;
void queueTouchEvent(const char *name, int curX, int curY, bool usePrev) {
	jerry_value_t eventArgs[2] = {String(name), jerry_create_object()};
	jerry_value_t xNum = jerry_create_number(curX);
	jerry_value_t yNum = jerry_create_number(curY);
	jerry_value_t dxNum = usePrev ? jerry_create_number(curX - (int) prevX) : jerry_create_number_nan();
	jerry_value_t dyNum = usePrev ? jerry_create_number(curY - (int) prevY) : jerry_create_number_nan();
	setProperty(eventArgs[1], "x", xNum);
	setProperty(eventArgs[1], "y", yNum);
	setProperty(eventArgs[1], "dx", dxNum);
	setProperty(eventArgs[1], "dy", dyNum);
	jerry_release_value(xNum);
	jerry_release_value(yNum);
	jerry_release_value(dxNum);
	jerry_release_value(dyNum);
	jerry_value_t eventObj = jerry_construct_object(ref_Event, eventArgs, 2);
	queueEvent(ref_global, eventObj);
	jerry_release_value(eventArgs[0]);
	jerry_release_value(eventArgs[1]);
	jerry_release_value(eventObj);
}

void queueTouchEvents() {
	touchPosition pos;
	touchRead(&pos);
	if (keysDown() & KEY_TOUCH) queueTouchEvent("touchstart", pos.px, pos.py, false);
	else if (keysHeld() & KEY_TOUCH) {
		if (prevX != pos.px || prevY != pos.py) queueTouchEvent("touchmove", pos.px, pos.py, true);
	}
	else if (keysUp() & KEY_TOUCH) queueTouchEvent("touchend", prevX, prevY, false);
	prevX = pos.px;
	prevY = pos.py;
}

bool dispatchKeyboardEvent(bool down, const char16_t codepoint, const char *name, u8 location, bool shift, int layout, bool repeat) {
	jerry_value_t eventArgs[2] = {String(down ? "keydown" : "keyup"), jerry_create_object()};
	setProperty(eventArgs[1], "cancelable", JS_TRUE);

	jerry_value_t keyStr;
	if (codepoint == 2) keyStr = String("Shift"); // hardcoded override to remove Left/Right variants of Shift
	else if (codepoint < ' ') keyStr = String(name);
	else if (codepoint < 0x80) keyStr = String((char *) &codepoint);
	else {
		u32 convertedLength;
		char *converted = UTF16toUTF8(&codepoint, 1, &convertedLength);
		keyStr = StringSized(converted, convertedLength);
		free(converted);
	}
	jerry_value_t codeStr = String(name);
	jerry_value_t layoutStr = String(
		layout == 0 ? "AlphaNumeric" : 
		layout == 1 ? "LatinAccented" :
		layout == 2 ? "Kana" :
		layout == 3 ? "Symbol" :
		layout == 4 ? "Pictogram"
	: "");
	setProperty(eventArgs[1], "key", keyStr);
	setProperty(eventArgs[1], "code", codeStr);
	setProperty(eventArgs[1], "layout", layoutStr);
	setProperty(eventArgs[1], "repeat", jerry_create_boolean(repeat));
	jerry_release_value(keyStr);
	jerry_release_value(codeStr);
	jerry_release_value(layoutStr);

	setProperty(eventArgs[1], "shifted", jerry_create_boolean(shift));

	jerry_value_t kbdEventObj = jerry_construct_object(ref_Event, eventArgs, 2);
	bool canceled = dispatchEvent(ref_global, kbdEventObj, false);
	jerry_release_value(kbdEventObj);
	jerry_release_value(eventArgs[0]);
	jerry_release_value(eventArgs[1]);
	return canceled;
}

bool onKeyDown(const char16_t codepoint, const char *name, bool shift, int layout, bool repeat) {
	if (!pauseKeyEvents && dependentEvents & keydown) {
		return dispatchKeyboardEvent(true, codepoint, name, 0, shift, layout, repeat);
	}
	return false;
}
bool onKeyUp(const char16_t codepoint, const char *name, bool shift, int layout) {
	if (!pauseKeyEvents && dependentEvents & keyup) {
		return dispatchKeyboardEvent(false, codepoint, name, 0, shift, layout, false);
	}
	return false;
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
		if (keysDown() & KEY_LID) queueEventName("sleep", sleepCallback);
		if (keysUp() & KEY_LID) queueEventName("wake");
		if (dependentEvents & (buttondown)) queueButtonEvents(true);
		if (dependentEvents & (buttonup)) queueButtonEvents(false);
		if (dependentEvents & (touchstart | touchmove | touchend)) queueTouchEvents();
		timeoutUpdate();
		runTasks();
		keyboardUpdate();
		if (inREPL) {
			if (keyboardComposeStatus() == KEYBOARD_INACTIVE) {
				putchar('>'); putchar(' ');
				keyboardCompose(false);
			}
			else if (keyboardComposeStatus() == KEYBOARD_FINISHED) {
				char *response;
				u32 responseSize;
				keyboardComposeAccept(&response, &responseSize);
				printf(response);
				putchar('\n');
				jerry_value_t parsedCode = jerry_parse(
					(const jerry_char_t *) "REPL", 4,
					(const jerry_char_t *) response, responseSize,
					JERRY_PARSE_STRICT_MODE
				);
				free(response);
				queueTask(runParsedCodeTask, &parsedCode, 1);
				jerry_release_value(parsedCode);
			}
		}
	}
}