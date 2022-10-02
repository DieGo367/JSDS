#include "execute.h"

#include <nds/arm9/input.h>
#include <nds/interrupts.h>
#include <queue>
#include <stdlib.h>
#include <string.h>
#include <unordered_set>

#include "console.h"
#include "inline.h"
#include "jerry/jerryscript.h"
#include "timeouts.h"



bool inREPL = false;

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
			handleError(microtaskResult);
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

/* Executes and releases parsed code. Returns the result of execution, which must be released!
 * Automatically releases parsedCode, unless it was an error value initially, in which case it is returned as is.
 */
jerry_value_t execute(jerry_value_t parsedCode) {
	if (jerry_value_is_error(parsedCode)) return parsedCode;

	jerry_value_t result = jerry_run(parsedCode);
	jerry_release_value(parsedCode);
	runMicrotasks();
	if (!inREPL && jerry_value_is_error(result)) {
		// if not in the REPL, abort on uncaught error. Waits for START like normal.
		consolePrintLiteral(result);
		putchar('\n');
		while (true) {
			swiWaitForVBlank();
			scanKeys();
			if (keysDown() & KEY_START) break;
		}
		exit(1);
	}
	return result;
}

std::queue<Task> taskQueue;

/* The Event Loop™
 * This runs just a single iteration, so it must be used in a loop to fully live up to its name.
 * Executes all tasks currently in the task queue (newly enqueued tasks are not run).
 * After each task, microtasks are executed until the microtask queue is empty.
 */
void eventLoop() {
	u32 size = taskQueue.size();
	while (size--) {
		Task task = taskQueue.front();
		taskQueue.pop();
		jerry_value_t taskResult = jerry_call_function(task.function, task.thisValue, task.args, task.argCount);
		runMicrotasks();
		if (jerry_value_is_error(taskResult)) {
			consolePrintLiteral(taskResult);
			putchar('\n');
			if (!inREPL) { // if not in the REPL, abort on uncaught error. Waits for START like normal.
				while (true) {
					swiWaitForVBlank();
					scanKeys();
					if (keysDown() & KEY_START) break;
				}
				exit(1);
			}
		}
		jerry_release_value(taskResult);
		jerry_release_value(task.function);
		jerry_release_value(task.thisValue);
		for (u32 i = 0; i < task.argCount; i++) jerry_release_value(task.args[i]);
	}
}

void queueTask(jerry_value_t function, jerry_value_t thisValue, jerry_value_t *args, jerry_length_t argCount) {
	Task task;
	task.function = jerry_acquire_value(function);
	task.thisValue = jerry_acquire_value(thisValue);
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
		jerry_release_value(task.function);
		jerry_release_value(task.thisValue);
		for (u32 i = 0; i < task.argCount; i++) jerry_release_value(task.args[i]);
	}
}

// Returns whether there are still tasks to run or timeouts in progress
bool workExists() {
	return taskQueue.size() > 0 || timeoutsExist();
}

/* Attempts to handle an error by dispatching an ErrorEvent.
 * If left unhandled, the error will be printed and (unless in the REPL) the program will exit.
 */
void handleError(jerry_value_t error) {
	bool errorHandled = false;

	jerry_value_t global = jerry_get_global_object();
	jerry_value_t errorStr = jerry_create_string((jerry_char_t *) "error");
	jerry_value_t eventListeners = getInternalProperty(global, "eventListeners");
	jerry_value_t errorEventListeners = jerry_get_property(eventListeners, errorStr);
	if (jerry_value_is_array(errorEventListeners) && jerry_get_array_length(errorEventListeners) > 0) {
		jerry_value_t errorEventInit = jerry_create_object();

		jerry_error_t errorCode = jerry_get_error_type(error);
		jerry_value_t errorThrown = jerry_get_value_from_error(error, false);
		jerry_value_t True = jerry_create_boolean(true);
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
		jerry_release_value(True);

		jerry_value_t result = jerry_call_function(ref_task_dispatchEvent, global, &errorEvent, 1);
		if (jerry_value_is_error(result)) handleError(result); // lol
		else if (jerry_get_boolean_value(result) == false) errorHandled = true;
		jerry_release_value(result);

		jerry_release_value(errorEvent);
	}
	jerry_release_value(errorEventListeners);
	jerry_release_value(eventListeners);
	jerry_release_value(errorStr);
	jerry_release_value(global);

	if (!errorHandled) {
		consolePrintLiteral(error);
		putchar('\n');
		if (!inREPL) { // if not in the REPL, abort on uncaught error. Waits for START like normal.
			while (true) {
				swiWaitForVBlank();
				scanKeys();
				if (keysDown() & KEY_START) break;
			}
			exit(1);
		}
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
		
		jerry_value_t True = jerry_create_boolean(true);
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
		jerry_release_value(True);

		jerry_value_t result = jerry_call_function(ref_task_dispatchEvent, global, &rejectionEvent, 1);
		if (jerry_value_is_error(result)) handleError(result);
		else if (jerry_get_boolean_value(result) == false) rejectionHandled = true;
		jerry_release_value(result);

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
		if (!inREPL) { // if not in the REPL, abort on uncaught error. Waits for START like normal.
			while (true) {
				swiWaitForVBlank();
				scanKeys();
				if (keysDown() & KEY_START) break;
			}
			exit(1);
		}
	}

}

void fireLoadEvent() {
	jerry_value_t global = jerry_get_global_object();

	jerry_value_t loadStr = jerry_create_string((jerry_char_t *) "load");
	jerry_value_t loadEvent = jerry_construct_object(ref_Event, &loadStr, 1);
	jerry_release_value(loadStr);

	jerry_value_t True = jerry_create_boolean(true);
	setInternalProperty(loadEvent, "isTrusted", True);
	jerry_release_value(True);

	queueTask(ref_task_dispatchEvent, global, &loadEvent, 1);

	jerry_release_value(loadEvent);
	jerry_release_value(global);
}