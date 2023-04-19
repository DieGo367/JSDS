#include "error.hpp"

#include <stdlib.h>
#include <string.h>
#include <unordered_set>

#include "event.hpp"
#include "helpers.hpp"
#include "io/console.hpp"
#include "jerry/jerryscript-port-default.h"
#include "logging.hpp"



/* Attempts to handle an error by dispatching an ErrorEvent.
 * If left unhandled, the error will be printed and (unless in the REPL) the program will exit.
 * Not for use within JS functions.
 */
void handleError(jerry_value_t error, bool sync) {
	bool errorHandled = false;

	jerry_value_t errorProp = String("error");
	jerry_value_t eventListenersObj = getInternalProperty(ref_global, "eventListeners");
	jerry_value_t errorEventListenersArr = jerry_get_property(eventListenersObj, errorProp);
	jerry_release_value(eventListenersObj);
	if (jerry_value_is_array(errorEventListenersArr) && jerry_get_array_length(errorEventListenersArr) > 0) {
		jerry_value_t errorEventInitObj = jerry_create_object();

		jerry_error_t errorCode = jerry_get_error_type(error);
		jerry_value_t thrownVal = jerry_get_value_from_error(error, false);
		setProperty(errorEventInitObj, "cancelable", JS_TRUE);
		jerry_release_value(jerry_set_property(errorEventInitObj, errorProp, thrownVal));
		if (errorCode == JERRY_ERROR_NONE) {
			jerry_value_t thrownStr = jerry_value_to_string(thrownVal);
			jerry_value_t uncaughtStr = String("Uncaught ");
			jerry_value_t concatenatedStr = jerry_binary_operation(JERRY_BIN_OP_ADD, uncaughtStr, thrownStr);
			setProperty(errorEventInitObj, "message", concatenatedStr);
			jerry_release_value(concatenatedStr);
			jerry_release_value(uncaughtStr);
			jerry_release_value(thrownStr);
		}
		else {
			jerry_value_t thrownStr = jerry_value_to_string(thrownVal);
			setProperty(errorEventInitObj, "message", thrownStr);
			jerry_release_value(thrownStr);

			jerry_value_t backtraceArr = jerry_get_internal_property(thrownVal, ref_str_backtrace);
			jerry_value_t resourceStr = jerry_get_property_by_index(backtraceArr, 0);
			char *resource = getString(resourceStr);
			jerry_release_value(resourceStr);
			jerry_release_value(backtraceArr);

			char *colon = strchr(resource, ':');
			jerry_value_t filenameStr = StringSized(resource, colon - resource);
			setProperty(errorEventInitObj, "filename", filenameStr);
			jerry_release_value(filenameStr);

			char *endptr = NULL;
			int64 lineno = strtoll(colon + 1, &endptr, 10);
			jerry_value_t linenoNum = endptr == (colon + 1) ? jerry_create_number_nan() : jerry_create_number(lineno);
			setProperty(errorEventInitObj, "lineno", linenoNum);
			jerry_release_value(linenoNum);
			free(resource);
		}
		jerry_release_value(thrownVal);

		jerry_value_t eventArgs[2] = {errorProp, errorEventInitObj};
		jerry_value_t errorEventObj = jerry_construct_object(ref_Event.constructor, eventArgs, 2);
		jerry_release_value(errorEventInitObj);
		errorHandled = dispatchEvent(ref_global, errorEventObj, sync);
		jerry_release_value(errorEventObj);
	}
	jerry_release_value(errorEventListenersArr);
	jerry_release_value(errorProp);

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
	
	jerry_value_t eventListenersObj = getInternalProperty(ref_global, "eventListeners");
	jerry_value_t unhandledrejectionProp = String("unhandledrejection");
	jerry_value_t rejectionEventListenersArr = jerry_get_property(eventListenersObj, unhandledrejectionProp);
	if (jerry_value_is_array(rejectionEventListenersArr) && jerry_get_array_length(rejectionEventListenersArr) > 0) {
		jerry_value_t rejectionEventInitObj = jerry_create_object();
		
		setProperty(rejectionEventInitObj, "cancelable", JS_TRUE);
		setProperty(rejectionEventInitObj, "promise", promise);
		jerry_value_t reasonVal = jerry_get_promise_result(promise);
		setProperty(rejectionEventInitObj, "reason", reasonVal);
		jerry_release_value(reasonVal);

		jerry_value_t eventArgs[2] = {unhandledrejectionProp, rejectionEventInitObj};
		jerry_value_t rejectionEventObj = jerry_construct_object(ref_Event.constructor, eventArgs, 2);
		jerry_release_value(rejectionEventInitObj);
		rejectionHandled = dispatchEvent(ref_global, rejectionEventObj, true);
		jerry_release_value(rejectionEventObj);
	}
	jerry_release_value(rejectionEventListenersArr);
	jerry_release_value(unhandledrejectionProp);
	jerry_release_value(eventListenersObj);

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