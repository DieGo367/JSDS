#include "event.hpp"

extern "C" {
#include <nds/system.h>
}
#include <nds/arm9/input.h>
#include <nds/arm9/sprite.h>
#include <nds/interrupts.h>
#include <queue>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "error.hpp"
#include "file.hpp"
#include "helpers.hpp"
#include "io/console.hpp"
#include "io/keyboard.hpp"
#include "jerry/jerryscript.h"
#include "logging.hpp"
#include "timeouts.hpp"
#include "util/unicode.hpp"



JS_class ref_Event;

bool inREPL = false;
bool abortFlag = false;
bool userClosed = false;
u8 dependentEvents = 0;
bool pauseKeyEvents = false;
bool spriteUpdateMain = false;
bool spriteUpdateSub = false;

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
		eventObj = jerry_construct_object(ref_Event.constructor, eventArgs, 2);
		jerry_release_value(eventArgs[1]);
	}
	else eventObj = jerry_construct_object(ref_Event.constructor, &eventNameStr, 1);
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
		jerry_value_t eventObj = jerry_construct_object(ref_Event.constructor, eventArgs, 2);
		#define TEST_VALUE(button, keyCode) if (set & keyCode) { \
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
	jerry_value_t eventObj = jerry_construct_object(ref_Event.constructor, eventArgs, 2);
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

	jerry_value_t kbdEventObj = jerry_construct_object(ref_Event.constructor, eventArgs, 2);
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
		if (spriteUpdateMain) oamUpdate(&oamMain);
		if (spriteUpdateSub) oamUpdate(&oamSub);
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


FUNCTION(EventConstructor) {
	CONSTRUCTOR(Event); REQUIRE(1);

	setInternalProperty(thisValue, "stopImmediatePropagation", JS_FALSE); // stop immediate propagation flag
	setReadonly(thisValue, "target", JS_NULL);
	setReadonly(thisValue, "cancelable", JS_FALSE);
	setReadonly(thisValue, "defaultPrevented", JS_FALSE);                 // canceled flag
	jerry_value_t currentTimeNum = jerry_create_number(time(NULL));
	setReadonly(thisValue, "timeStamp", currentTimeNum);
	jerry_release_value(currentTimeNum);
	jerry_value_t typeStr = jerry_value_to_string(args[0]);	
	setReadonly(thisValue, "type", typeStr);
	jerry_release_value(typeStr);

	if (argCount > 1 && jerry_value_is_object(args[1])) {
		jerry_value_t keysArr = jerry_get_object_keys(args[1]);
		u32 length = jerry_get_array_length(keysArr);
		for (u32 i = 0; i < length; i++) {
			jerry_value_t key = jerry_get_property_by_index(keysArr, i);
			jerry_value_t value = jerry_get_property(args[1], key);
			JS_setReadonly(thisValue, key, value);
			jerry_release_value(value);
			jerry_release_value(key);
		}
		jerry_release_value(keysArr);
	}

	return JS_UNDEFINED;
}

FUNCTION(Event_stopImmediatePropagation) {
	setInternalProperty(thisValue, "stopImmediatePropagation", JS_TRUE);
	return JS_UNDEFINED;
}

FUNCTION(Event_preventDefault) {
	if (testInternalProperty(thisValue, "cancelable")) {
		setInternalProperty(thisValue, "defaultPrevented", JS_TRUE);
	}
	return JS_UNDEFINED;
}

FUNCTION(EventTargetConstructor) {
	if (thisValue != ref_global) CONSTRUCTOR(EventTarget);

	jerry_value_t eventListenersObj = jerry_create_object();
	setInternalProperty(thisValue, "eventListeners", eventListenersObj);
	jerry_release_value(eventListenersObj);

	return JS_UNDEFINED;
}

FUNCTION(EventTarget_addEventListener) {
	REQUIRE(2);
	if (jerry_value_is_null(args[1])) return JS_UNDEFINED;
	jerry_value_t targetObj = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	
	jerry_value_t callbackProp = String("callback");
	jerry_value_t onceProp = String("once");

	jerry_value_t typeStr = jerry_value_to_string(args[0]);
	jerry_value_t callbackVal = args[1];
	bool once = false;
	if (argCount > 2 && jerry_value_is_object(args[2])) {
		once = JS_testProperty(args[2], onceProp);
	}

	jerry_value_t eventListenersObj = getInternalProperty(targetObj, "eventListeners");
	jerry_value_t listenersArr = jerry_get_property(eventListenersObj, typeStr); // listeners of the given type
	if (jerry_value_is_undefined(listenersArr)) {
		listenersArr = jerry_create_array(0);
		jerry_release_value(jerry_set_property(eventListenersObj, typeStr, listenersArr));
	}
	jerry_release_value(eventListenersObj);

	u32 length = jerry_get_array_length(listenersArr);
	bool shouldAppend = true;
	for (u32 i = 0; shouldAppend && i < length; i++) {
		jerry_value_t storedListenerObj = jerry_get_property_by_index(listenersArr, i);
		jerry_value_t storedCallbackVal = jerry_get_property(storedListenerObj, callbackProp);
		if (strictEqual(callbackVal, storedCallbackVal)) shouldAppend = false;
		jerry_release_value(storedCallbackVal);
		jerry_release_value(storedListenerObj);
	}

	if (shouldAppend) {
		jerry_value_t listenerObj = jerry_create_object();

		jerry_release_value(jerry_set_property(listenerObj, callbackProp, callbackVal));
		jerry_release_value(jerry_set_property(listenerObj, onceProp, jerry_create_boolean(once)));
		setProperty(listenerObj, "removed", JS_FALSE);

		jerry_release_value(jerry_call_function(ref_func_push, listenersArr, &listenerObj, 1));
		jerry_release_value(listenerObj);

		if (targetObj == ref_global) {
			char *type = getString(typeStr);
			if (strcmp(type, "vblank") == 0) dependentEvents |= vblank;
			else if (strcmp(type, "buttondown") == 0) dependentEvents |= buttondown;
			else if (strcmp(type, "buttonup") == 0) dependentEvents |= buttonup;
			else if (strcmp(type, "touchstart") == 0) dependentEvents |= touchstart;
			else if (strcmp(type, "touchmove") == 0) dependentEvents |= touchmove;
			else if (strcmp(type, "touchend") == 0) dependentEvents |= touchend;
			else if (strcmp(type, "keydown") == 0) dependentEvents |= keydown;
			else if (strcmp(type, "keyup") == 0) dependentEvents |= keyup;
			free(type);
		}
	}

	jerry_release_value(listenersArr);
	jerry_release_value(typeStr);
	jerry_release_value(onceProp);
	jerry_release_value(callbackProp);

	return JS_UNDEFINED;
}

FUNCTION(EventTarget_removeEventListener) {
	REQUIRE(2);
	jerry_value_t targetObj = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	jerry_value_t callbackProp = String("callback");
	jerry_value_t typeStr = jerry_value_to_string(args[0]);
	jerry_value_t callbackVal = args[1];

	jerry_value_t eventListenersObj = getInternalProperty(targetObj, "eventListeners");
	jerry_value_t listenersArr = jerry_get_property(eventListenersObj, typeStr); // listeners of the given type
	jerry_release_value(eventListenersObj);
	if (jerry_value_is_array(listenersArr)) {
		u32 length = jerry_get_array_length(listenersArr);
		bool removed = false;
		for (u32 i = 0; !removed && i < length; i++) {
			jerry_value_t storedListenerObj = jerry_get_property_by_index(listenersArr, i);
			jerry_value_t storedCallbackVal = jerry_get_property(storedListenerObj, callbackProp);
			if (strictEqual(callbackVal, storedCallbackVal)) {
				arraySplice(listenersArr, i, 1);
				setProperty(storedListenerObj, "removed", JS_TRUE);
				removed = true;
				if (targetObj == ref_global && jerry_get_array_length(listenersArr) == 0) {
					char *type = getString(typeStr);
					if (strcmp(type, "vblank") == 0) dependentEvents &= ~(vblank);
					else if (strcmp(type, "buttondown") == 0) dependentEvents &= ~(buttondown);
					else if (strcmp(type, "buttonup") == 0) dependentEvents &= ~(buttonup);
					else if (strcmp(type, "touchstart") == 0) dependentEvents &= ~(touchstart);
					else if (strcmp(type, "touchmove") == 0) dependentEvents &= ~(touchmove);
					else if (strcmp(type, "touchend") == 0) dependentEvents &= ~(touchend);
					else if (strcmp(type, "keydown") == 0) dependentEvents &= ~(keydown);
					else if (strcmp(type, "keyup") == 0) dependentEvents &= ~(keyup);
					free(type);
				}
			}
			jerry_release_value(storedCallbackVal);
			jerry_release_value(storedListenerObj);
		}
	}
	jerry_release_value(listenersArr);
	jerry_release_value(typeStr);
	jerry_release_value(callbackProp);

	return JS_UNDEFINED;
}

FUNCTION(EventTarget_dispatchEvent) {
	REQUIRE(1); EXPECT(isInstance(args[0], ref_Event), Event);
	jerry_value_t targetObj = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	bool canceled = dispatchEvent(targetObj, args[0], true);
	return jerry_create_boolean(!canceled);
}

void exposeEventAPI(jerry_value_t global) {
	JS_class Event = createClass(global, "Event", EventConstructor);
	setMethod(Event.prototype, "stopImmediatePropagation", Event_stopImmediatePropagation);
	setMethod(Event.prototype, "preventDefault", Event_preventDefault);
	ref_Event = Event;

	JS_class EventTarget = createClass(global, "EventTarget", EventTargetConstructor);
	setMethod(EventTarget.prototype, "addEventListener", EventTarget_addEventListener);
	setMethod(EventTarget.prototype, "removeEventListener", EventTarget_removeEventListener);
	setMethod(EventTarget.prototype, "dispatchEvent", EventTarget_dispatchEvent);

	// turn global into an EventTarget
	setPrototype(global, EventTarget.prototype);
	EventTargetConstructor(EventTarget.constructor, global, NULL, 0);
	releaseClass(EventTarget);

	defEventAttribute(global, "onerror");
	defEventAttribute(global, "onunhandledrejection");
	defEventAttribute(global, "onkeydown");
	defEventAttribute(global, "onkeyup");
	defEventAttribute(global, "onbuttondown");
	defEventAttribute(global, "onbuttonup");
	defEventAttribute(global, "onsleep");
	defEventAttribute(global, "ontouchstart");
	defEventAttribute(global, "ontouchmove");
	defEventAttribute(global, "ontouchend");
	defEventAttribute(global, "onvblank");
	defEventAttribute(global, "onwake");
}

void releaseEventReferences() {
	releaseClass(ref_Event);
}