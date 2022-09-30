#include "execute.h"

#include <nds/arm9/input.h>
#include <nds/interrupts.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "inline.h"
#include "jerry/jerryscript.h"



bool inREPL = false;

/* Executes and releases parsed code. Returns the result of execution, which must be released!
 * Automatically releases parsedCode, unless it was an error value initially, in which case it is returned as is.
 */
jerry_value_t execute(jerry_value_t parsedCode) {
	if (jerry_value_is_error(parsedCode)) return parsedCode;

	jerry_value_t result = jerry_run(parsedCode);
	jerry_release_value(parsedCode);
	jerry_value_t jobResult;
	while (true) {
		jobResult = jerry_run_all_enqueued_jobs();
		if (jerry_value_is_error(jobResult)) {
			handleError(jobResult);
			jerry_release_value(jobResult);
		}
		else break;
	}
	jerry_release_value(jobResult);
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

			jerry_value_t backtrace = getInternalProperty(errorThrown, "backtrace");
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

		jerry_value_t dispatchFunc = getProperty(global, "dispatchEvent");
		jerry_value_t result = jerry_call_function(dispatchFunc, global, &errorEvent, 1);
		if (jerry_value_is_error(result)) handleError(result); // lol
		else if (jerry_get_boolean_value(result) == false) errorHandled = true;
		jerry_release_value(result);
		jerry_release_value(dispatchFunc);

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

void fireLoadEvent() {
	jerry_value_t global = jerry_get_global_object();

	jerry_value_t Event = getProperty(global, "Event");
	jerry_value_t loadStr = jerry_create_string((jerry_char_t *) "load");
	jerry_value_t loadEvent = jerry_construct_object(Event, &loadStr, 1);
	jerry_release_value(loadStr);
	jerry_release_value(Event);

	jerry_value_t True = jerry_create_boolean(true);
	setInternalProperty(loadEvent, "isTrusted", True);
	jerry_release_value(True);

	jerry_value_t dispatchFunc = getProperty(global, "dispatchEvent");
	jerry_value_t result = jerry_call_function(dispatchFunc, global, &loadEvent, 1);
	if (jerry_value_is_error(result)) handleError(result);
	jerry_release_value(result);
	jerry_release_value(dispatchFunc);

	jerry_release_value(loadEvent);
	jerry_release_value(global);
}