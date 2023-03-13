#include "error.hpp"

#include <stdlib.h>
#include <string.h>
#include <unordered_set>

#include "event.hpp"
#include "inline.hpp"
#include "jerry/jerryscript-port-default.h"
#include "logging.hpp"



/* Attempts to handle an error by dispatching an ErrorEvent.
 * If left unhandled, the error will be printed and (unless in the REPL) the program will exit.
 * Not for use within JS functions.
 */
void handleError(jerry_value_t error, bool sync) {
	bool errorHandled = false;

	jerry_value_t errorStr = createString("error");
	jerry_value_t eventListeners = getInternalProperty(ref_global, "eventListeners");
	jerry_value_t errorEventListeners = jerry_get_property(eventListeners, errorStr);
	if (jerry_value_is_array(errorEventListeners) && jerry_get_array_length(errorEventListeners) > 0) {
		jerry_value_t errorEventInit = jerry_create_object();

		jerry_error_t errorCode = jerry_get_error_type(error);
		jerry_value_t errorThrown = jerry_get_value_from_error(error, false);
		setProperty(errorEventInit, "cancelable", True);
		jerry_release_value(jerry_set_property(errorEventInit, errorStr, errorThrown));
		if (errorCode == JERRY_ERROR_NONE) {
			jerry_value_t strVal = jerry_value_to_string(errorThrown);
			jerry_value_t uncaughtStr = createString("Uncaught ");
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

		jerry_value_t args[2] = {errorStr, errorEventInit};
		jerry_value_t errorEvent = jerry_construct_object(ref_Event, args, 2);
		jerry_release_value(errorEventInit);
		errorHandled = dispatchEvent(ref_global, errorEvent, sync);
		jerry_release_value(errorEvent);
	}
	jerry_release_value(errorEventListeners);
	jerry_release_value(eventListeners);
	jerry_release_value(errorStr);

	if (!errorHandled) {
		logLiteral(error);
		putchar('\n');
		if (!inREPL) abortFlag = true;
	}
}

void handleRejection(jerry_value_t promise) {
	bool rejectionHandled = false;
	
	jerry_value_t eventListeners = getInternalProperty(ref_global, "eventListeners");
	jerry_value_t rejectionStr = createString("unhandledrejection");
	jerry_value_t rejectionEventListeners = jerry_get_property(eventListeners, rejectionStr);
	if (jerry_value_is_array(rejectionEventListeners) && jerry_get_array_length(rejectionEventListeners) > 0) {
		jerry_value_t rejectionEventInit = jerry_create_object();
		
		setProperty(rejectionEventInit, "cancelable", True);
		setProperty(rejectionEventInit, "promise", promise);
		jerry_value_t reason = jerry_get_promise_result(promise);
		setProperty(rejectionEventInit, "reason", reason);
		jerry_release_value(reason);

		jerry_value_t args[2] = {rejectionStr, rejectionEventInit};
		jerry_value_t rejectionEvent = jerry_construct_object(ref_Event, args, 2);
		jerry_release_value(rejectionEventInit);
		rejectionHandled = dispatchEvent(ref_global, rejectionEvent, true);
		jerry_release_value(rejectionEvent);
	}
	jerry_release_value(rejectionEventListeners);
	jerry_release_value(rejectionStr);
	jerry_release_value(eventListeners);

	if (!rejectionHandled) {
		jerry_value_t reason = jerry_get_promise_result(promise);
		printf("Uncaught (in promise) ");
		logLiteral(reason);
		putchar('\n');
		jerry_release_value(reason);
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

void handleRejectedPromises() {
	for (const jerry_value_t &promise : rejectedPromises) {
		handleRejection(promise);
		jerry_release_value(promise);
	}
	rejectedPromises.clear();
}

void onErrorCreated(jerry_value_t errorObject, void *userPtr) {
	jerry_value_t backtrace = jerry_get_backtrace(10);
	jerry_set_internal_property(errorObject, ref_str_backtrace, backtrace);
	jerry_release_value(backtrace);
}

void setErrorHandlers() {
	jerry_set_error_object_created_callback(onErrorCreated, NULL);
	jerry_jsds_set_promise_rejection_op_callback(onPromiseRejectionOp);
}