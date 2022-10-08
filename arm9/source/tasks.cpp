#include "tasks.h"

#include <nds/arm9/input.h>
#include <nds/interrupts.h>
#include <queue>
#include <stdlib.h>
#include <string.h>
#include <unordered_set>

#include "console.h"
#include "inline.h"
#include "jerry/jerryscript.h"
#include "keyboard.h"
#include "timeouts.h"



bool inREPL = false;
bool abortFlag = false;

std::unordered_set<jerry_value_t> rejectedPromises;

void onPromiseRejectionOp(jerry_value_t promise, jerry_promise_rejection_operation_t operation) {
	if (operation == JERRY_PROMISE_REJECTION_OPERATION_REJECT) {
		rejectedPromises.emplace(jerry_acquire_value(promise));
	}
	else if (operation == JERRY_PROMISE_REJECTION_OPERATION_HANDLE) { // promise was handled after initially being rejected
		auto it = rejectedPromises.begin();
		while (it != rejectedPromises.end()) {
			const jerry_value_t storedPromise = *(it++);
			jerry_value_t equal = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, promise, storedPromise);
			if (jerry_get_boolean_value(equal)) {
				jerry_release_value(storedPromise);
				rejectedPromises.erase(storedPromise);
			}
			jerry_release_value(equal);
		}
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
	for (const jerry_value_t &promise : rejectedPromises) {
		handleRejection(promise);
		jerry_release_value(promise);
	}
	rejectedPromises.clear();
}

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

/* The Event Loop™
 * On every vblank, run necessary operations before then executing the current task queue.
 * Returns when there is no work left to do (not in the REPL and no tasks/timeouts left to execute) or when abortFlag is set.
 */
void eventLoop() {
	while (!abortFlag && (inREPL || taskQueue.size() > 0 || timeoutsExist())) {
		swiWaitForVBlank();
		timeoutUpdate();
		runTasks();
		if (inREPL) {
			if (keyboardEnterPressed) {
				putchar('\n');
				jerry_value_t parsedCode = jerry_parse(
					(const jerry_char_t *) "<REPL>", 6,
					(const jerry_char_t *) keyboardBuffer(), keyboardBufferLen(),
					JERRY_PARSE_STRICT_MODE
				);
				queueTask(runParsedCodeTask, &parsedCode, 1);
				jerry_release_value(parsedCode);
				keyboardClearBuffer();
			}
			keyboardUpdate();
		}
	}
}

/* Attempts to handle an error by dispatching an ErrorEvent.
 * If left unhandled, the error will be printed and (unless in the REPL) the program will exit.
 * Not for use within JS functions.
 */
void handleError(jerry_value_t error, bool sync) {
	bool errorHandled = false;

	jerry_value_t global = jerry_get_global_object();
	jerry_value_t errorStr = jerry_create_string((jerry_char_t *) "error");
	jerry_value_t eventListeners = getInternalProperty(global, "eventListeners");
	jerry_value_t errorEventListeners = jerry_get_property(eventListeners, errorStr);
	if (jerry_value_is_array(errorEventListeners) && jerry_get_array_length(errorEventListeners) > 0) {
		jerry_value_t errorEventInit = jerry_create_object();

		jerry_error_t errorCode = jerry_get_error_type(error);
		jerry_value_t errorThrown = jerry_get_value_from_error(error, false);
		setProperty(errorEventInit, "cancelable", True);
		jerry_release_value(jerry_set_property(errorEventInit, errorStr, errorThrown));
		if (errorCode == JERRY_ERROR_NONE) {
			jerry_value_t strVal = jerry_value_to_string(errorThrown);
			jerry_value_t uncaughtStr = jerry_create_string((jerry_char_t *) "Uncaught ");
			jerry_value_t concatenated = jerry_binary_operation(JERRY_BIN_OP_ADD, uncaughtStr, strVal);
			setProperty(errorEventInit, "message", concatenated);
			jerry_release_value(concatenated);
			jerry_release_value(uncaughtStr);
			jerry_release_value(strVal);
		}
		else {
			jerry_value_t messageVal = jerry_value_to_string(errorThrown);
			setProperty(errorEventInit, "message", messageVal);
			jerry_release_value(messageVal);

			jerry_value_t backtrace = jerry_get_internal_property(errorThrown, ref_str_backtrace);
			jerry_value_t resourceVal = jerry_get_property_by_index(backtrace, 0);
			char *resource = getString(resourceVal);
			jerry_release_value(resourceVal);
			jerry_release_value(backtrace);

			char *colon = strchr(resource, ':');
			jerry_value_t filenameVal = jerry_create_string_sz((jerry_char_t *) resource, colon - resource);
			setProperty(errorEventInit, "filename", filenameVal);
			jerry_release_value(filenameVal);

			char *endptr = NULL;
			int64 lineno = strtoll(colon + 1, &endptr, 10);
			jerry_value_t linenoVal = endptr == (colon + 1) ? jerry_create_number_nan() : jerry_create_number(lineno);
			setProperty(errorEventInit, "lineno", linenoVal);
			jerry_release_value(linenoVal);
			free(resource);
		}
		jerry_release_value(errorThrown);

		jerry_value_t ErrorEvent = getProperty(global, "ErrorEvent");
		jerry_value_t args[2] = {errorStr, errorEventInit};
		jerry_value_t errorEvent = jerry_construct_object(ErrorEvent, args, 2);
		jerry_release_value(ErrorEvent);
		jerry_release_value(errorEventInit);

		setInternalProperty(errorEvent, "isTrusted", True);

		errorHandled = dispatchEvent(global, errorEvent, sync);

		jerry_release_value(errorEvent);
	}
	jerry_release_value(errorEventListeners);
	jerry_release_value(eventListeners);
	jerry_release_value(errorStr);
	jerry_release_value(global);

	if (!errorHandled) {
		consolePrintLiteral(error);
		putchar('\n');
		if (!inREPL) abortFlag = true;
	}
}

void handleRejection(jerry_value_t promise) {
	bool rejectionHandled = false;
	
	jerry_value_t global = jerry_get_global_object();
	jerry_value_t eventListeners = getInternalProperty(global, "eventListeners");
	jerry_value_t rejectionStr = jerry_create_string((jerry_char_t *) "unhandledrejection");
	jerry_value_t rejectionEventListeners = jerry_get_property(eventListeners, rejectionStr);
	if (jerry_value_is_array(rejectionEventListeners) && jerry_get_array_length(rejectionEventListeners) > 0) {
		jerry_value_t rejectionEventInit = jerry_create_object();
		
		setProperty(rejectionEventInit, "cancelable", True);
		setProperty(rejectionEventInit, "promise", promise);
		jerry_value_t reason = jerry_get_promise_result(promise);
		setProperty(rejectionEventInit, "reason", reason);
		jerry_release_value(reason);

		jerry_value_t PromiseRejectionEvent = getProperty(global, "PromiseRejectionEvent");
		jerry_value_t args[2] = {rejectionStr, rejectionEventInit};
		jerry_value_t rejectionEvent = jerry_construct_object(PromiseRejectionEvent, args, 2);
		jerry_release_value(PromiseRejectionEvent);
		jerry_release_value(rejectionEventInit);

		setInternalProperty(rejectionEvent, "isTrusted", True);

		rejectionHandled = dispatchEvent(global, rejectionEvent, false);

		jerry_release_value(rejectionEvent);
	}
	jerry_release_value(rejectionEventListeners);
	jerry_release_value(rejectionStr);
	jerry_release_value(eventListeners);
	jerry_release_value(global);

	if (!rejectionHandled) {
		jerry_value_t reason = jerry_get_promise_result(promise);
		printf("Uncaught (in promise) ");
		consolePrintLiteral(reason);
		putchar('\n');
		jerry_release_value(reason);
		if (!inREPL) abortFlag = true;
	}

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
			consolePrintLiteral(result);
			putchar('\n');
		}
		else if (jerry_value_is_error(result)) {
			consolePrintLiteral(result);
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
	jerry_value_t dispatchStr = jerry_create_string((jerry_char_t *) "dispatch");
	jerry_value_t eventPhaseStr = jerry_create_string((jerry_char_t *) "eventPhase");
	jerry_value_t targetStr = jerry_create_string((jerry_char_t *) "target");
	jerry_value_t currentTargetStr = jerry_create_string((jerry_char_t *) "currentTarget");

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
		jerry_value_t removedStr = jerry_create_string((jerry_char_t *) "removed");
		jerry_value_t onceStr = jerry_create_string((jerry_char_t *) "once");
		jerry_value_t passiveStr = jerry_create_string((jerry_char_t *) "passive");
		jerry_value_t callbackStr = jerry_create_string((jerry_char_t *) "callback");
		jerry_value_t spliceFunc = getProperty(listenersOfType, "splice");
		jerry_value_t inPassiveListenerStr = jerry_create_string((jerry_char_t *) "inPassiveListener");
		jerry_value_t handleEventStr = jerry_create_string((jerry_char_t *) "handleEvent");

		for (u32 i = 0; i < length && !abortFlag; i++) {
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
	jerry_value_t global = jerry_get_global_object();

	jerry_value_t eventNameVal = jerry_create_string((jerry_char_t *) eventName);
	jerry_value_t event = jerry_construct_object(ref_Event, &eventNameVal, 1);
	jerry_release_value(eventNameVal);

	setInternalProperty(event, "isTrusted", True);

	queueEvent(global, event);

	jerry_release_value(event);
	jerry_release_value(global);
}