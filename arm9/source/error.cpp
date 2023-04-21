#include "error.hpp"

#include <stdlib.h>
#include <string.h>
#include <unordered_set>

#include "event.hpp"
#include "io/console.hpp"
#include "jerry/jerryscript-port-default.h"
#include "logging.hpp"
#include "util/helpers.hpp"



/* Attempts to handle an error by dispatching an ErrorEvent.
 * If left unhandled, the error will be printed and (unless in the REPL) the program will exit.
 * Not for use within JS functions.
 */
void handleError(jerry_value_t error, bool sync) {
	bool errorHandled = false;

	jerry_value_t errorProp = String("error");
	jerry_value_t errorEvent = createEvent(errorProp, true);

	jerry_error_t errorCode = jerry_get_error_type(error);
	jerry_value_t thrownVal = jerry_get_value_from_error(error, false);
	defReadonly(errorEvent, errorProp, thrownVal);
	jerry_release_value(errorProp);
	if (errorCode == JERRY_ERROR_NONE) {
		jerry_value_t thrownStr = jerry_value_to_string(thrownVal);
		jerry_value_t uncaughtStr = String("Uncaught ");
		jerry_value_t concatenatedStr = jerry_binary_operation(JERRY_BIN_OP_ADD, uncaughtStr, thrownStr);
		defReadonly(errorEvent, "message", concatenatedStr);
		jerry_release_value(concatenatedStr);
		jerry_release_value(uncaughtStr);
		jerry_release_value(thrownStr);
	}
	else {
		jerry_value_t thrownStr = jerry_value_to_string(thrownVal);
		defReadonly(errorEvent, "message", thrownStr);
		jerry_release_value(thrownStr);

		jerry_value_t backtraceArr = jerry_get_internal_property(thrownVal, ref_str_backtrace);
		jerry_value_t resourceStr = jerry_get_property_by_index(backtraceArr, 0);
		char *resource = rawString(resourceStr);
		jerry_release_value(resourceStr);
		jerry_release_value(backtraceArr);

		char *colon = strchr(resource, ':');
		jerry_value_t filenameStr = StringSized(resource, colon - resource);
		defReadonly(errorEvent, "filename", filenameStr);
		jerry_release_value(filenameStr);

		char *endptr = NULL;
		int64 lineno = strtoll(colon + 1, &endptr, 10);
		jerry_value_t linenoNum = endptr == (colon + 1) ? jerry_create_number_nan() : jerry_create_number(lineno);
		defReadonly(errorEvent, "lineno", linenoNum);
		jerry_release_value(linenoNum);
		free(resource);
	}
	jerry_release_value(thrownVal);

	errorHandled = dispatchEvent(ref_global, errorEvent, sync);
	jerry_release_value(errorEvent);

	if (!errorHandled) {
		u16 previousColor = consoleSetColor(LOGCOLOR_ERROR);
		u16 previousBG = consoleSetBackground(LOGCOLOR_ERROR_BG);
		logLiteral(error);
		putchar('\n');
		consoleSetColor(previousColor);
		consoleSetBackground(previousBG);
		if (!inREPL) abortFlag = true;
	}
}

void handleRejection(jerry_value_t promise) {
	bool rejectionHandled = false;
	
	jerry_value_t rejectionEvent = createEvent("unhandledrejection", true);
	
	defReadonly(rejectionEvent, "promise", promise);
	jerry_value_t reasonVal = jerry_get_promise_result(promise);
	defReadonly(rejectionEvent, "reason", reasonVal);
	jerry_release_value(reasonVal);

	rejectionHandled = dispatchEvent(ref_global, rejectionEvent, true);
	jerry_release_value(rejectionEvent);

	if (!rejectionHandled) {
		jerry_value_t reasonVal = jerry_get_promise_result(promise);
		u16 previousColor = consoleSetColor(LOGCOLOR_ERROR);
		u16 previousBG = consoleSetBackground(LOGCOLOR_ERROR_BG);
		printf("Uncaught (in promise) ");
		logLiteral(reasonVal);
		putchar('\n');
		consoleSetColor(previousColor);
		consoleSetBackground(previousBG);
		jerry_release_value(reasonVal);
		if (!inREPL) abortFlag = true;
	}
}

std::unordered_set<jerry_value_t> rejectedPromises;

void onPromiseRejectionOp(jerry_value_t promise, jerry_promise_rejection_operation_t operation) {
	if (operation == JERRY_PROMISE_REJECTION_OPERATION_REJECT) {
		rejectedPromises.emplace(jerry_acquire_value(promise));
	}
	else if (operation == JERRY_PROMISE_REJECTION_OPERATION_HANDLE) { // promise was handled after initially being rejected
		auto it = rejectedPromises.begin();
		while (it != rejectedPromises.end()) {
			const jerry_value_t storedPromiseVal = *(it++);
			if (strictEqual(promise, storedPromiseVal)) {
				jerry_release_value(storedPromiseVal);
				rejectedPromises.erase(storedPromiseVal);
			}
		}
	}
}

void handleRejectedPromises() {
	for (const jerry_value_t &promiseVal : rejectedPromises) {
		handleRejection(promiseVal);
		jerry_release_value(promiseVal);
	}
	rejectedPromises.clear();
}

void onErrorCreated(jerry_value_t errorObject, void *userPtr) {
	jerry_value_t backtraceArr = jerry_get_backtrace(10);
	jerry_set_internal_property(errorObject, ref_str_backtrace, backtraceArr);
	jerry_release_value(backtraceArr);
}

void setErrorHandlers() {
	jerry_set_error_object_created_callback(onErrorCreated, NULL);
	jerry_jsds_set_promise_rejection_op_callback(onPromiseRejectionOp);
}