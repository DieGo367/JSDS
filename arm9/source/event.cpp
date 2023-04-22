#include "event.hpp"

#include <nds/arm9/input.h>
#include <nds/interrupts.h>
#include <queue>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "error.hpp"
#include "io/console.hpp"
#include "io/keyboard.hpp"
#include "logging.hpp"
#include "sprite.hpp"
#include "system.hpp"
#include "timeouts.hpp"
#include "util/helpers.hpp"



JS_class ref_Event;

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

jerry_value_t createEvent(jerry_value_t type, bool cancelable) {
	jerry_value_t event = jerry_create_object();
	setPrototype(event, ref_Event.prototype);
	setInternal(event, "stopImmediatePropagation", JS_FALSE); // stop immediate propagation flag
	defReadonly(event, "target", JS_NULL);
	defReadonly(event, "cancelable", jerry_create_boolean(cancelable));
	defReadonly(event, "defaultPrevented", JS_FALSE);        
	defReadonly(event, "timeStamp", (double) time(NULL));
	defReadonly(event, "type", type);
	return event;
}
jerry_value_t createEvent(const char *type, bool cancelable) {
	jerry_value_t string = String(type);
	jerry_value_t event = createEvent(string, cancelable);
	jerry_release_value(string);
	return event;
}

/**
 * Dispatches event onto target.
 * If sync is true, runs "synchronously" (no microtasks are run). JS functions should set it to true.
 * Returns true if the event was canceled, false otherwise.
 */
bool dispatchEvent(jerry_value_t target, jerry_value_t event, bool sync) {
	jerry_value_t targetProp = String("target");
	jerry_value_t stopImmediatePropagationProp = String("stopImmediatePropagation");
	jerry_set_internal_property(event, targetProp, target);

	jerry_value_t eventListenersObj = getInternal(target, "eventListeners");
	jerry_value_t typeStr = getInternal(event, "type");
	jerry_value_t listenersArr = jerry_get_property(eventListenersObj, typeStr); // listeners of given type
	if (jerry_value_is_array(listenersArr)) {
		jerry_value_t listenersCopyArr = jerry_call_function(ref_func_slice, listenersArr, NULL, 0);

		u32 length = jerry_get_array_length(listenersCopyArr);
		jerry_value_t onceProp = String("once");
		jerry_value_t callbackProp = String("callback");

		for (u32 i = 0; i < length && !abortFlag; i++) {
			if (testInternal(event, stopImmediatePropagationProp)) break;

			jerry_value_t listenerObj = jerry_get_property_by_index(listenersCopyArr, i);
			if (!testProperty(listenerObj, ref_str_removed)) {
				if (testProperty(listenerObj, onceProp)) {
					arraySplice(listenersArr, i, 1);
					jerry_release_value(jerry_set_property(listenerObj, ref_str_removed, JS_TRUE));
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

		jerry_release_value(callbackProp);
		jerry_release_value(onceProp);
		jerry_release_value(listenersCopyArr);
	}
	jerry_release_value(listenersArr);
	jerry_release_value(typeStr);
	jerry_release_value(eventListenersObj);

	jerry_set_internal_property(event, targetProp, JS_NULL);
	jerry_set_internal_property(event, stopImmediatePropagationProp, JS_FALSE);
	jerry_release_value(targetProp);
	jerry_release_value(stopImmediatePropagationProp);
	
	return testInternal(event, "defaultPrevented");
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
	jerry_value_t event = createEvent(eventName, callback != NULL);
	queueEvent(ref_global, event, callback);
	jerry_release_value(event);
}

bool dispatchKeyboardEvent(bool down, const char16_t codepoint, const char *name, u8 location, bool shift, int layout, bool repeat) {
	jerry_value_t keyboardEvent = createEvent(down ? "keydown" : "keyup", true);

	jerry_value_t keyStr;
	if (codepoint == 2) keyStr = String("Shift"); // hardcoded override to remove Left/Right variants of Shift
	else if (codepoint < ' ') keyStr = String(name);
	else if (codepoint < 0x80) keyStr = String((char *) &codepoint);
	else keyStr = StringUTF16(&codepoint, 1);
	defReadonly(keyboardEvent, "key", keyStr);
	jerry_release_value(keyStr);
	
	defReadonly(keyboardEvent, "code", name);
	defReadonly(keyboardEvent, "layout",
		layout == 0 ? "AlphaNumeric" : 
		layout == 1 ? "LatinAccented" :
		layout == 2 ? "Kana" :
		layout == 3 ? "Symbol" :
		layout == 4 ? "Pictogram"
	: "");
	defReadonly(keyboardEvent, "repeat", jerry_create_boolean(repeat));
	defReadonly(keyboardEvent, "shifted", jerry_create_boolean(shift));

	bool canceled = dispatchEvent(ref_global, keyboardEvent, false);
	jerry_release_value(keyboardEvent);
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
		if (keysDown() & KEY_LID) queueSleepEvent();
		if (keysUp() & KEY_LID) queueEventName("wake");
		if (dependentEvents & (buttondown)) queueButtonEvents(true);
		if (dependentEvents & (buttonup)) queueButtonEvents(false);
		if (dependentEvents & (touchstart | touchmove | touchend)) queueTouchEvents();
		timeoutUpdate();
		runTasks();
		spriteUpdate();
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



FUNCTION(Event_stopImmediatePropagation) {
	setInternal(thisValue, "stopImmediatePropagation", JS_TRUE);
	return JS_UNDEFINED;
}

FUNCTION(Event_preventDefault) {
	if (testInternal(thisValue, "cancelable")) {
		setInternal(thisValue, "defaultPrevented", JS_TRUE);
	}
	return JS_UNDEFINED;
}

FUNCTION(EventTargetConstructor) {
	if (thisValue != ref_global) CONSTRUCTOR(EventTarget);

	jerry_value_t eventListenersObj = jerry_create_object();
	setInternal(thisValue, "eventListeners", eventListenersObj);
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
		once = testProperty(args[2], onceProp);
	}

	jerry_value_t eventListenersObj = getInternal(targetObj, "eventListeners");
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
		jerry_release_value(jerry_set_property(listenerObj, ref_str_removed, JS_FALSE));

		jerry_release_value(jerry_call_function(ref_func_push, listenersArr, &listenerObj, 1));
		jerry_release_value(listenerObj);

		if (targetObj == ref_global) {
			char *type = rawString(typeStr);
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

	jerry_value_t eventListenersObj = getInternal(targetObj, "eventListeners");
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
				jerry_release_value(jerry_set_property(storedListenerObj, ref_str_removed, JS_TRUE));
				removed = true;
				if (targetObj == ref_global && jerry_get_array_length(listenersArr) == 0) {
					char *type = rawString(typeStr);
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
	JS_class Event = createClass(global, "Event", IllegalConstructor);
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