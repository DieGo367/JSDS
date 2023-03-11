#include "api.hpp"

#include <dirent.h>
#include <nds/arm9/input.h>
#include <nds/arm9/video.h>
#include <nds/arm9/background.h>
#include <nds/interrupts.h>
extern "C" {
#include <nds/system.h>
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "error.hpp"
#include "event.hpp"
#include "inline.hpp"
#include "input.hpp"
#include "io/console.hpp"
#include "io/keyboard.hpp"
#include "jerry/jerryscript.h"
#include "logging.hpp"
#include "storage.hpp"
#include "timeouts.hpp"
#include "tonccpy.h"



jerry_value_t ref_global;
jerry_value_t ref_DS;
jerry_value_t ref_localStorage;
jerry_value_t ref_Event;
jerry_value_t ref_Error;
jerry_value_t ref_str_name;
jerry_value_t ref_str_constructor;
jerry_value_t ref_str_prototype;
jerry_value_t ref_str_backtrace;
jerry_value_t ref_sym_toStringTag;
jerry_value_t ref_proxyHandler_storage;
jerry_value_t ref_consoleCounters;
jerry_value_t ref_consoleTimers;

const u32 STORAGE_API_MAX_CAPACITY = 4096; // 4 KiB
const u32 STORAGE_API_LENGTH_BYTES = 8;

const char ONE_ARG[21] = "1 argument required.";

#define CALL_INFO const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], u32 argCount
#define REQUIRE_1() if (argCount == 0) return throwTypeError(ONE_ARG);
#define REQUIRE(n) if (argCount < n) return throwTypeError(#n " arguments required.")
#define EXPECT(test, type) if (!(test)) return throwTypeError("Expected type '" #type "'.")
#define CONSTRUCTOR(name) if (isNewTargetUndefined()) return throwTypeError("Constructor '" #name "' cannot be invoked without 'new'.");

static jerry_value_t IllegalConstructor(CALL_INFO) {
	return throwTypeError("Illegal constructor");
}

static jerry_value_t closeHandler(CALL_INFO) {
	abortFlag = true;
	userClosed = true;
	return jerry_create_abort_from_value(createString("close() was called."), true);
}

static jerry_value_t alertHandler(CALL_INFO) {
	if (argCount > 0) printValue(args[0]);
	else printf("Alert");
	printf(" [ OK]\n");
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_A) break;
	}
	return undefined;
}

static jerry_value_t confirmHandler(CALL_INFO) {
	if (argCount > 0) printValue(args[0]);
	else printf("Confirm");
	printf(" [ OK,  Cancel]\n");
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		u32 keys = keysDown();
		if (keys & KEY_A) return True;
		else if (keys & KEY_B) return False;
	}
}

static jerry_value_t promptHandler(CALL_INFO) {
	if (argCount > 0) printValue(args[0]);
	else printf("Prompt");
	putchar(' ');
	pauseKeyEvents = true;
	keyboardCompose(true);
	ComposeStatus status = keyboardComposeStatus();
	while (status == COMPOSING) {
		swiWaitForVBlank();
		scanKeys();
		keyboardUpdate();
		status = keyboardComposeStatus();
	}
	if (status == FINISHED) {
		char *str;
		int strSize;
		keyboardComposeAccept(&str, &strSize);
		printf(str); putchar('\n');
		jerry_value_t strVal = jerry_create_string_sz_from_utf8((jerry_char_t *) str, (jerry_size_t) strSize);
		free(str);
		keyboardUpdate();
		pauseKeyEvents = false;
		return strVal;
	}
	else {
		putchar('\n');
		keyboardUpdate();
		pauseKeyEvents = false;
		if (argCount > 1) return jerry_value_to_string(args[1]);
		else return null;
	}
}

static jerry_value_t setTimeoutHandler(CALL_INFO) {
	if (argCount >= 2) return addTimeout(args[0], args[1], (jerry_value_t *)(args) + 2, argCount - 2, false);
	else {
		jerry_value_t zero = jerry_create_number(0);
		jerry_value_t result = addTimeout(argCount > 0 ? args[0] : undefined, zero, NULL, 0, false);
		jerry_release_value(zero);
		return result;
	}
}

static jerry_value_t setIntervalHandler(CALL_INFO) {
	if (argCount >= 2) return addTimeout(args[0], args[1], (jerry_value_t *)(args) + 2, argCount - 2, true);
	else {
		jerry_value_t zero = jerry_create_number(0);
		jerry_value_t result = addTimeout(argCount > 0 ? args[0] : undefined, zero, NULL, 0, true);
		jerry_release_value(zero);
		return result;
	}
}

static jerry_value_t clearTimeoutHandler(CALL_INFO) {
	if (argCount > 0) clearTimeout(args[0]);
	else clearTimeout(undefined);
	return undefined;
}

static jerry_value_t consoleLogHandler(CALL_INFO) {
	if (argCount > 0) {
		logIndent();
		log(args, argCount);
	}
	return undefined;
}

static jerry_value_t consoleInfoHandler(CALL_INFO) {
	if (argCount > 0) {
		u16 prev = consoleSetColor(INFO);
		logIndent();
		log(args, argCount);
		consoleSetColor(prev);
	}
	return undefined;
}

static jerry_value_t consoleWarnHandler(CALL_INFO) {
	if (argCount > 0) {
		u16 prev = consoleSetColor(WARN);
		logIndent();
		log(args, argCount);
		consoleSetColor(prev);
	}
	return undefined;
}

static jerry_value_t consoleErrorHandler(CALL_INFO) {
	if (argCount > 0) {
		u16 prev = consoleSetColor(ERROR);
		logIndent();
		log(args, argCount);
		consoleSetColor(prev);
	}
	return undefined;
}

static jerry_value_t consoleAssertHandler(CALL_INFO) {
	if (argCount == 0 || !jerry_value_to_boolean(args[0])) {
		u16 prev = consoleSetColor(ERROR);
		logIndent();
		printf("Assertion failed: ");
		log(args + 1, argCount - 1);
		consoleSetColor(prev);
	}
	return undefined;
}

static jerry_value_t consoleDebugHandler(CALL_INFO) {
	if (argCount > 0) {
		u16 prev = consoleSetColor(DEBUG);
		logIndent();
		log(args, argCount);
		consoleSetColor(prev);
	}
	return undefined;
}

static jerry_value_t consoleTraceHandler(CALL_INFO) {
	logIndent();
	if (argCount == 0) printf("Trace\n");
	else log(args, argCount);
	jerry_value_t backtrace = jerry_get_backtrace(10);
	u32 length = jerry_get_array_length(backtrace);
	for (u32 i = 0; i < length; i++) {
		jerry_value_t traceLine = jerry_get_property_by_index(backtrace, i);
		char *step = getString(traceLine);
		logIndent();
		printf(" @ %s\n", step);
		free(step);
		jerry_release_value(traceLine);
	}
	jerry_release_value(backtrace);
	return undefined;
}

static jerry_value_t consoleDirHandler(CALL_INFO) {
	consolePause();
	if (argCount > 0) {
		logIndent();
		if (jerry_value_is_object(args[0])) logObject(args[0]);
		else logLiteral(args[0]);
		putchar('\n');
	}
	consoleResume();
	return undefined;
}

static jerry_value_t consoleTableHandler(CALL_INFO) {
	if (argCount > 0) logTable(args, argCount);
	return undefined;
}

static jerry_value_t consoleGroupHandler(CALL_INFO) {
	logIndentAdd();
	if (argCount > 0) {
		logIndent();
		log(args, argCount);
	}
	return undefined;
}

static jerry_value_t consoleGroupEndHandler(CALL_INFO) {
	logIndentRemove();
	return undefined;
}

static jerry_value_t consoleCountHandler(CALL_INFO) {
	jerry_value_t label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		label = jerry_value_to_string(args[0]);
	}
	else label = createString("default");
	
	jerry_value_t countVal = jerry_get_property(ref_consoleCounters, label);
	u32 count;
	if (jerry_value_is_undefined(label)) count = 1;
	else count = jerry_value_as_uint32(countVal) + 1;
	jerry_release_value(countVal);

	logIndent();
	printString(label);
	printf(": %lu\n", count);
	
	countVal = jerry_create_number(count);
	jerry_release_value(jerry_set_property(ref_consoleCounters, label, countVal));
	jerry_release_value(countVal);

	jerry_release_value(label);
	return undefined;
}

static jerry_value_t consoleCountResetHandler(CALL_INFO) {
	jerry_value_t label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		label = jerry_value_to_string(args[0]);
	}
	else label = createString("default");

	jerry_value_t hasLabel = jerry_has_own_property(ref_consoleCounters, label);
	if (jerry_get_boolean_value(hasLabel)) {
		jerry_value_t zero = jerry_create_number(0);
		jerry_set_property(ref_consoleCounters, label, zero);
		jerry_release_value(zero);
	}
	else {
		logIndent();
		printf("Count for '");
		printString(label);
		printf("' does not exist\n");
	}
	jerry_release_value(hasLabel);

	jerry_release_value(label);
	return undefined;
}

static jerry_value_t consoleTimeHandler(CALL_INFO) {
	jerry_value_t label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		label = jerry_value_to_string(args[0]);
	}
	else label = createString("default");

	jerry_value_t hasLabel = jerry_has_own_property(ref_consoleTimers, label);
	if (jerry_get_boolean_value(hasLabel)) {
		logIndent();
		printf("Timer '");
		printString(label);
		printf("' already exists\n");
	}
	else {
		jerry_value_t counterId = jerry_create_number(counterAdd());
		jerry_release_value(jerry_set_property(ref_consoleTimers, label, counterId));
		jerry_release_value(counterId);
	}
	jerry_release_value(hasLabel);
	
	jerry_release_value(label);
	return undefined;
}

static jerry_value_t consoleTimeLogHandler(CALL_INFO) {
	jerry_value_t label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		label = jerry_value_to_string(args[0]);
	}
	else label = createString("default");

	logIndent();
	jerry_value_t counterVal = jerry_get_property(ref_consoleTimers, label);
	if (jerry_value_is_undefined(counterVal)) {
		printf("Timer '");
		printString(label);
		printf("' does not exist\n");
	}
	else {
		int counterId = jerry_value_as_int32(counterVal);
		printString(label);
		printf(": %i ms", counterGet(counterId));
		if (argCount > 1) {
			putchar(' ');
			log(args + 1, argCount - 1);
		}
	}
	jerry_release_value(counterVal);

	jerry_release_value(label);
	return undefined;
}

static jerry_value_t consoleTimeEndHandler(CALL_INFO) {
	jerry_value_t label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		label = jerry_value_to_string(args[0]);
	}
	else label = createString("default");

	logIndent();
	jerry_value_t counterVal = jerry_get_property(ref_consoleTimers, label);
	if (jerry_value_is_undefined(counterVal)) {
		printf("Timer '");
		printString(label);
		printf("' does not exist\n");
	}
	else {
		int counterId = jerry_value_as_int32(counterVal);
		printString(label);
		printf(": %i ms\n", counterGet(counterId));
		counterRemove(counterId);
		jerry_delete_property(ref_consoleTimers, label);
	}
	jerry_release_value(counterVal);

	jerry_release_value(label);
	return undefined;
}

static jerry_value_t consoleClearHandler(CALL_INFO) {
	consoleClear();
	return undefined;
}

static jerry_value_t TextEncodeHandler(CALL_INFO) {
	REQUIRE_1();
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) {
		EXPECT(jerry_value_is_typedarray(args[1]), ArrayBufferView);
		jerry_value_t text = jerry_value_to_string(args[0]);
		jerry_length_t size = jerry_get_utf8_string_size(text);
		jerry_length_t byteOffset, bufSize;
		jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[1], &byteOffset, &bufSize);
		u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
		jerry_release_value(arrayBuffer);
		if (size > bufSize) {
			jerry_release_value(text);
			return throwTypeError("Text size is too big to encode into the given array.");
		}
		jerry_string_to_utf8_char_buffer(text, data, size);
		jerry_release_value(text);
		return jerry_acquire_value(args[1]);
	}
	jerry_value_t text = jerry_value_to_string(args[0]);
	jerry_length_t size = jerry_get_utf8_string_size(text);
	jerry_value_t arrayBuffer = jerry_create_arraybuffer(size);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer);
	jerry_string_to_utf8_char_buffer(text, data, size);
	jerry_value_t u8Array = jerry_create_typedarray_for_arraybuffer(JERRY_TYPEDARRAY_UINT8, arrayBuffer);
	jerry_release_value(text);
	jerry_release_value(arrayBuffer);
	return u8Array;
}

static jerry_value_t TextDecodeHandler(CALL_INFO) {
	REQUIRE_1(); EXPECT(jerry_value_is_typedarray(args[0]), ArrayBufferView);
	u32 byteOffset = 0, dataLen = 0;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &byteOffset, &dataLen);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
	jerry_release_value(arrayBuffer);
	if (!jerry_is_valid_utf8_string(data, dataLen)) return throwTypeError("Invalid UTF-8");
	return jerry_create_string_sz_from_utf8(data, dataLen);
}

static jerry_value_t EventConstructor(CALL_INFO) {
	CONSTRUCTOR(Event); REQUIRE_1();

	setInternalProperty(thisValue, "initialized", True);               // initialized flag
	setInternalProperty(thisValue, "dispatch", False);                 // dispatch flag
	setInternalProperty(thisValue, "stopImmediatePropagation", False); // stop immediate propagation flag
	setReadonly(thisValue, "target", null);
	setReadonly(thisValue, "currentTarget", null);
	setReadonly(thisValue, "cancelable", False);
	setReadonly(thisValue, "defaultPrevented", False);                 // canceled flag
	jerry_value_t currentTime = jerry_create_number(time(NULL));
	setReadonly(thisValue, "timeStamp", currentTime);
	jerry_release_value(currentTime);

	jerry_value_t typeAsString = jerry_value_to_string(args[0]);	
	setReadonly(thisValue, "type", typeAsString);
	jerry_release_value(typeAsString);

	if (argCount > 1 && jerry_value_is_object(args[1])) {
		jerry_value_t cancelableVal = getProperty(args[1], "cancelable");
		jerry_value_t cancelableBool = jerry_create_boolean(jerry_value_to_boolean(cancelableVal));
		setInternalProperty(thisValue, "cancelable", cancelableBool);
		jerry_release_value(cancelableBool);
		jerry_release_value(cancelableVal);
	}

	return undefined;
}

static jerry_value_t EventStopImmediatePropagationHandler(CALL_INFO) {
	setInternalProperty(thisValue, "stopImmediatePropagation", True);
	return undefined;
}

static jerry_value_t EventPreventDefaultHandler(CALL_INFO) {
	jerry_value_t cancelable = getInternalProperty(thisValue, "cancelable");
	if (jerry_value_to_boolean(cancelable)) {
		setInternalProperty(thisValue, "defaultPrevented", True);
	}
	jerry_release_value(cancelable);
	return undefined;
}

static jerry_value_t EventTargetConstructor(CALL_INFO) {
	CONSTRUCTOR(EventTarget);

	jerry_value_t eventListenerList = jerry_create_object();
	setInternalProperty(thisValue, "eventListeners", eventListenerList);
	jerry_release_value(eventListenerList);

	return undefined;
}

static jerry_value_t EventTargetAddEventListenerHandler(CALL_INFO) {
	REQUIRE(2);
	if (jerry_value_is_null(args[1])) return undefined;
	jerry_value_t target = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	
	jerry_value_t typeStr = createString("type");
	jerry_value_t callbackStr = createString("callback");
	jerry_value_t onceStr = createString("once");

	jerry_value_t typeVal = jerry_value_to_string(args[0]);
	jerry_value_t callbackVal = args[1];
	bool once = false;
	if (argCount > 2 && jerry_value_is_object(args[2])) {
		jerry_value_t onceVal = jerry_get_property(args[2], onceStr);
		once = jerry_value_to_boolean(onceVal);
		jerry_release_value(onceVal);
	}

	jerry_value_t eventListeners = getInternalProperty(target, "eventListeners");
	jerry_value_t listenersOfType = jerry_get_property(eventListeners, typeVal);
	if (jerry_value_is_undefined(listenersOfType)) {
		jerry_release_value(listenersOfType);
		listenersOfType = jerry_create_array(0);
		jerry_release_value(jerry_set_property(eventListeners, typeVal, listenersOfType));
	}
	u32 length = jerry_get_array_length(listenersOfType);
	bool shouldAppend = true;
	for (u32 i = 0; shouldAppend && i < length; i++) {
		jerry_value_t storedListener = jerry_get_property_by_index(listenersOfType, i);
		jerry_value_t storedCallback = jerry_get_property(storedListener, callbackStr);
		jerry_value_t callbackEquality = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, callbackVal, storedCallback);
		if (jerry_get_boolean_value(callbackEquality)) shouldAppend = false;
		jerry_release_value(callbackEquality);
		jerry_release_value(storedCallback);
		jerry_release_value(storedListener);
	}

	if (shouldAppend) {
		jerry_value_t listener = jerry_create_object();

		jerry_release_value(jerry_set_property(listener, typeStr, typeVal));
		jerry_release_value(jerry_set_property(listener, callbackStr, callbackVal));
		jerry_value_t onceVal = jerry_create_boolean(once);
		jerry_release_value(jerry_set_property(listener, onceStr, onceVal));
		jerry_release_value(onceVal);
		setProperty(listener, "removed", False);

		jerry_value_t pushFunc = getProperty(listenersOfType, "push");
		jerry_release_value(jerry_call_function(pushFunc, listenersOfType, &listener, 1));
		jerry_release_value(pushFunc);

		jerry_value_t isGlobal = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, target, ref_global);
		if (jerry_get_boolean_value(isGlobal)) {
			char *type = getString(typeVal);
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
		jerry_release_value(isGlobal);

		jerry_release_value(listener);
	}

	jerry_release_value(listenersOfType);
	jerry_release_value(eventListeners);
	jerry_release_value(typeVal);
	jerry_release_value(onceStr);
	jerry_release_value(callbackStr);
	jerry_release_value(typeStr);

	return undefined;
}

static jerry_value_t EventTargetRemoveEventListenerHandler(CALL_INFO) {
	REQUIRE(2);
	jerry_value_t target = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	
	jerry_value_t typeStr = createString("type");
	jerry_value_t callbackStr = createString("callback");

	jerry_value_t typeVal = jerry_value_to_string(args[0]);
	jerry_value_t callbackVal = args[1];

	jerry_value_t eventListeners = getInternalProperty(target, "eventListeners");
	jerry_value_t listenersOfType = jerry_get_property(eventListeners, typeVal);
	if (jerry_value_is_array(listenersOfType)) {
		u32 length = jerry_get_array_length(listenersOfType);
		bool removed = false;
		for (u32 i = 0; !removed && i < length; i++) {
			jerry_value_t storedListener = jerry_get_property_by_index(listenersOfType, i);
			jerry_value_t storedCallback = jerry_get_property(storedListener, callbackStr);
			jerry_value_t callbackEquality = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, callbackVal, storedCallback);
			if (jerry_get_boolean_value(callbackEquality)) {
				jerry_value_t spliceFunc = getProperty(listenersOfType, "splice");
				jerry_value_t spliceArgs[2] = {jerry_create_number(i), jerry_create_number(1)};
				jerry_release_value(jerry_call_function(spliceFunc, listenersOfType, spliceArgs, 2));
				jerry_release_value(spliceArgs[1]);
				jerry_release_value(spliceArgs[0]);
				jerry_release_value(spliceFunc);
				setProperty(storedListener, "removed", True);
				removed = true;
				if (jerry_get_array_length(listenersOfType) == 0) {
					jerry_value_t isGlobal = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, target, ref_global);
					if (jerry_get_boolean_value(isGlobal)) {
						char *type = getString(typeVal);
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
					jerry_release_value(isGlobal);
				}
			}
			jerry_release_value(callbackEquality);
			jerry_release_value(storedCallback);
			jerry_release_value(storedListener);
		}
	}
	jerry_release_value(listenersOfType);
	jerry_release_value(eventListeners);
	jerry_release_value(typeVal);
	jerry_release_value(callbackStr);
	jerry_release_value(typeStr);

	return undefined;
}

static jerry_value_t EventTargetDispatchEventHandler(CALL_INFO) {
	REQUIRE_1();
	jerry_value_t isInstanceVal = jerry_binary_operation(JERRY_BIN_OP_INSTANCEOF, args[0], ref_Event);
	bool isInstance = jerry_get_boolean_value(isInstanceVal);
	jerry_release_value(isInstanceVal);
	EXPECT(isInstance, Event);
	jerry_value_t target = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;

	jerry_value_t dispatchVal = getProperty(args[0], "dispatch");
	bool dispatched = jerry_value_to_boolean(dispatchVal);
	jerry_release_value(dispatchVal);
	if (dispatched) return throwError("Invalid event state");

	bool canceled = dispatchEvent(target, args[0], true);
	return jerry_create_boolean(!canceled);
}

static jerry_value_t ErrorEventConstructor(CALL_INFO) {
	CONSTRUCTOR(ErrorEvent); REQUIRE_1();
	if (argCount > 1) EXPECT(jerry_value_is_object(args[1]), ErrorEventInit);
	EventConstructor(function, thisValue, args, argCount);

	jerry_value_t messageProp = createString("message");
	jerry_value_t filenameProp = createString("filename");
	jerry_value_t linenoProp = createString("lineno");
	jerry_value_t emptyStr = createString("");
	jerry_value_t zero = jerry_create_number(0);
	setReadonly(thisValue, "error", undefined);
	setReadonlyJV(thisValue, messageProp, emptyStr);
	setReadonlyJV(thisValue, filenameProp, emptyStr);
	setReadonlyJV(thisValue, linenoProp, zero);
	jerry_release_value(zero);
	jerry_release_value(emptyStr);

	if (argCount > 1) {
		jerry_value_t messageVal = jerry_get_property(args[1], messageProp);
		if (!jerry_value_is_undefined(messageVal)) {
			jerry_value_t messageStr = jerry_value_to_string(messageVal);
			jerry_set_internal_property(thisValue, messageProp, messageStr);
			jerry_release_value(messageStr);
		}
		jerry_release_value(messageVal);
		jerry_value_t filenameVal = jerry_get_property(args[1], filenameProp);
		if (!jerry_value_is_undefined(filenameVal)) {
			jerry_value_t filenameStr = jerry_value_to_string(filenameVal);
			jerry_set_internal_property(thisValue, filenameProp, filenameStr);
			jerry_release_value(filenameStr);
		}
		jerry_release_value(filenameVal);
		jerry_value_t linenoVal = jerry_get_property(args[1], linenoProp);
		if (!jerry_value_is_undefined(linenoVal)) {
			jerry_value_t linenoNum = jerry_value_to_number(linenoVal);
			jerry_set_internal_property(thisValue, linenoProp, linenoNum);
			jerry_release_value(linenoNum);
		}
		jerry_release_value(linenoVal);
		jerry_value_t errorProp = createString("error");
		jerry_value_t errorVal = jerry_get_property(args[1], errorProp);
		jerry_set_internal_property(thisValue, errorProp, errorVal);
		jerry_release_value(errorVal);
		jerry_release_value(errorProp);
	}

	jerry_release_value(linenoProp);
	jerry_release_value(filenameProp);
	jerry_release_value(messageProp);

	return undefined;
}

static jerry_value_t PromiseRejectionEventConstructor(CALL_INFO) {
	CONSTRUCTOR(PromiseRejectionEvent); REQUIRE(2);
	EXPECT(jerry_value_is_object(args[1]), PromiseRejectionEventInit);

	jerry_value_t promiseStr = createString("promise");
	jerry_value_t promiseVal = jerry_get_property(args[1], promiseStr);
	if (jerry_value_is_undefined(promiseVal)) {
		jerry_release_value(promiseStr);
		jerry_release_value(promiseVal);
		return throwTypeError("Failed to read the 'promise' property from options.");
	}
	EventConstructor(function, thisValue, args, argCount);

	if (jerry_value_is_promise(promiseVal)) {
		setReadonlyJV(thisValue, promiseStr, promiseVal);
		jerry_value_t reason = jerry_get_promise_result(promiseVal);
		setReadonly(thisValue, "reason", reason);
		jerry_release_value(reason);
	}
	else {
		jerry_value_t promise = jerry_create_promise();
		jerry_resolve_or_reject_promise(promise, promiseVal, true);
		setReadonlyJV(thisValue, promiseStr, promise);
		jerry_release_value(promise);
		setReadonly(thisValue, "reason", undefined);
	}

	jerry_release_value(promiseVal);
	jerry_release_value(promiseStr);

	return undefined;
}

static jerry_value_t KeyboardEventConstructor(CALL_INFO) {
	CONSTRUCTOR(KeyboardEvent); REQUIRE_1();
	if (argCount > 1) EXPECT(jerry_value_is_object(args[1]), KeyboardEventInit);
	EventConstructor(function, thisValue, args, argCount);

	jerry_value_t keyStr = createString("key");
	jerry_value_t codeStr = createString("code");
	jerry_value_t locationStr = createString("location");

	jerry_value_t empty = createString("");
	setReadonlyJV(thisValue, keyStr, empty);
	setReadonlyJV(thisValue, codeStr, empty);
	jerry_release_value(empty);
	jerry_value_t zero = jerry_create_number(0);
	setReadonlyJV(thisValue, locationStr, zero);
	jerry_release_value(zero);

	if (argCount > 1) {
		jerry_value_t keyVal = jerry_get_property(args[1], keyStr);
		if (!jerry_value_is_undefined(keyVal)) {
			jerry_value_t keyAsStr = jerry_value_to_string(keyVal);
			jerry_set_internal_property(thisValue, keyStr, keyAsStr);
			jerry_release_value(keyAsStr);
		}
		jerry_release_value(keyVal);
		jerry_value_t codeVal = jerry_get_property(args[1], codeStr);
		if (!jerry_value_is_undefined(codeVal)) {
			jerry_value_t codeAsStr = jerry_value_to_string(codeVal);
			jerry_set_internal_property(thisValue, codeStr, codeAsStr);
			jerry_release_value(codeAsStr);
		}
		jerry_release_value(codeVal);
		jerry_value_t locationVal = jerry_get_property(args[1], locationStr);
		if (!jerry_value_is_undefined(locationVal)) {
			jerry_value_t locationNum = jerry_create_number(jerry_value_as_uint32(locationVal));
			jerry_set_internal_property(thisValue, locationStr, locationNum);
			jerry_release_value(locationNum);
		}
		jerry_release_value(locationVal);
	}

	jerry_release_value(locationStr);
	jerry_release_value(codeStr);
	jerry_release_value(keyStr);

	char eventInitBooleanProperties[][12] = {"repeat", "isComposing", "shiftKey"};

	if (argCount > 1) {
		for (u8 i = 0; i < 3; i++) {
			jerry_value_t prop = createString(eventInitBooleanProperties[i]);
			jerry_value_t val = jerry_get_property(args[1], prop);
			bool setTrue = jerry_value_to_boolean(val);
			setReadonlyJV(thisValue, prop, jerry_create_boolean(setTrue));
			jerry_release_value(val);
			jerry_release_value(prop);
		}
	}
	else for (u8 i = 0; i < 3; i++) setReadonly(thisValue, eventInitBooleanProperties[i], False);

	return undefined;
}

static jerry_value_t CustomEventConstructor(CALL_INFO) {
	CONSTRUCTOR(CustomEvent); REQUIRE_1();
	if (argCount > 1) EXPECT(jerry_value_is_object(args[1]), CustomEventInit);
	EventConstructor(function, thisValue, args, argCount);

	jerry_value_t detailProp = createString("detail");
	setReadonlyJV(thisValue, detailProp, null);
	if (argCount > 1) {
		jerry_value_t detailVal = jerry_get_property(args[1], detailProp);
		if (!jerry_value_is_undefined(detailVal)) {
			jerry_set_internal_property(thisValue, detailProp, detailVal);
		}
		jerry_release_value(detailVal);
	}
	jerry_release_value(detailProp);

	return undefined;
}

static jerry_value_t ButtonEventConstructor(CALL_INFO) {
	CONSTRUCTOR(ButtonEvent); REQUIRE_1();
	if (argCount > 1) EXPECT(jerry_value_is_object(args[1]), ButtonEventInit);
	EventConstructor(function, thisValue, args, argCount);

	jerry_value_t buttonStr = createString("button");
	if (argCount > 1) {
		jerry_value_t buttonVal = jerry_get_property(args[1], buttonStr);
		jerry_value_t buttonAsStr = jerry_value_to_string(buttonVal);
		setReadonlyJV(thisValue, buttonStr, buttonAsStr);
		jerry_release_value(buttonAsStr);
		jerry_release_value(buttonVal);
	}
	else {
		jerry_value_t empty = createString("");
		setReadonlyJV(thisValue, buttonStr, empty);
		jerry_release_value(empty);
	}
	jerry_release_value(buttonStr);

	return undefined;
}

static jerry_value_t TouchEventConstructor(CALL_INFO) {
	CONSTRUCTOR(TouchEvent); REQUIRE_1();
	if (argCount > 1) EXPECT(jerry_value_is_object(args[1]), TouchEventInit);
	EventConstructor(function, thisValue, args, argCount);

	jerry_value_t xStr = createString("x");
	jerry_value_t yStr = createString("y");
	jerry_value_t dxStr = createString("dx");
	jerry_value_t dyStr = createString("dy");

	if (argCount > 1) {
		jerry_value_t xVal = jerry_get_property(args[1], xStr);
		jerry_value_t xNum = jerry_value_to_number(xVal);
		setReadonlyJV(thisValue, xStr, xNum);
		jerry_release_value(xNum);
		jerry_release_value(xVal);
		jerry_value_t yVal = jerry_get_property(args[1], yStr);
		jerry_value_t yNum = jerry_value_to_number(yVal);
		setReadonlyJV(thisValue, yStr, yNum);
		jerry_release_value(yNum);
		jerry_release_value(yVal);
		jerry_value_t dxVal = jerry_get_property(args[1], dxStr);
		jerry_value_t dxNum = jerry_value_to_number(dxVal);
		setReadonlyJV(thisValue, dxStr, dxNum);
		jerry_release_value(dxNum);
		jerry_release_value(dxVal);
		jerry_value_t dyVal = jerry_get_property(args[1], dyStr);
		jerry_value_t dyNum = jerry_value_to_number(dyVal);
		setReadonlyJV(thisValue, dyStr, dyNum);
		jerry_release_value(dyNum);
		jerry_release_value(dyVal);
	}
	else {
		jerry_value_t NaN = jerry_create_number_nan();
		setReadonlyJV(thisValue, xStr, NaN);
		setReadonlyJV(thisValue, yStr, NaN);
		setReadonlyJV(thisValue, dxStr, NaN);
		setReadonlyJV(thisValue, dyStr, NaN);
		jerry_release_value(NaN);
	}

	jerry_release_value(xStr);
	jerry_release_value(yStr);
	jerry_release_value(dxStr);
	jerry_release_value(dyStr);

	return undefined;
}

static jerry_value_t StorageLengthGetter(CALL_INFO) {
	jerry_value_t propNames = jerry_get_object_keys(thisValue);
	u32 length = jerry_get_array_length(propNames);
	jerry_release_value(propNames);
	return jerry_create_number(length);
}

static jerry_value_t StorageKeyHandler(CALL_INFO) {
	REQUIRE_1();
	jerry_value_t key = null;
	jerry_value_t propNames = jerry_get_object_keys(thisValue);
	jerry_value_t nVal = jerry_value_to_number(args[0]);
	u32 n = jerry_value_as_uint32(nVal);
	if (n < jerry_get_array_length(propNames)) {
		jerry_value_t prop = jerry_get_property_by_index(propNames, n);
		if (!jerry_value_is_undefined(prop)) key = prop;
		else jerry_release_value(prop);
	}
	jerry_release_value(nVal);
	jerry_release_value(propNames);
	return key;
}

static jerry_value_t StorageGetItemHandler(CALL_INFO) {
	REQUIRE_1();
	jerry_value_t hasOwnVal = jerry_has_own_property(thisValue, args[0]);
	bool hasOwn = jerry_get_boolean_value(hasOwnVal);
	jerry_release_value(hasOwnVal);
	if (hasOwn) return jerry_get_property(thisValue, args[0]);
	else return null;
}

static jerry_value_t StorageSetItemHandler(CALL_INFO) {
	REQUIRE(2);

	jerry_value_t storageSizeVal = getInternalProperty(thisValue, "size");
	u32 storageFilled = jerry_value_as_uint32(storageSizeVal);
	jerry_release_value(storageSizeVal);

	jerry_value_t propertyAsString = jerry_value_to_string(args[0]);
	jerry_value_t valAsString = jerry_value_to_string(args[1]);
	u32 propertyNameSize;
	char *propertyName = getString(propertyAsString, &propertyNameSize);

	jerry_value_t hasOwn = jerry_has_own_property(thisValue, propertyAsString);
	if (jerry_get_boolean_value(hasOwn)) {
		jerry_value_t currentValue = jerry_get_property(thisValue, propertyAsString);
		storageFilled -= propertyNameSize + jerry_get_string_size(currentValue) + STORAGE_API_LENGTH_BYTES;
		jerry_release_value(currentValue);
	}
	jerry_release_value(hasOwn);
	storageFilled += propertyNameSize + jerry_get_string_size(valAsString) + STORAGE_API_LENGTH_BYTES;

	jerry_value_t result = undefined;
	if (storageFilled > STORAGE_API_MAX_CAPACITY) {
		result = throwError("Exceeded the quota.");
	}
	else {
		if (strcmp(propertyName, "length") == 0) {
			jerry_property_descriptor_t lengthDesc = {
				.is_value_defined = true,
				.is_enumerable_defined = true,
				.is_enumerable = true,
				.is_configurable_defined = true,
				.is_configurable = true,
				.value = valAsString
			};
			jerry_release_value(jerry_define_own_property(thisValue, propertyAsString, &lengthDesc));
		}
		else jerry_set_property(thisValue, propertyAsString, valAsString);

		jerry_value_t newSize = jerry_create_number(storageFilled);
		setInternalProperty(thisValue, "size", newSize);
		jerry_release_value(newSize);
		jerry_value_t isLocal = getInternalProperty(thisValue, "isLocal");
		if (jerry_get_boolean_value(isLocal)) storageRequestSave();
		jerry_release_value(isLocal);
	}

	free(propertyName);
	jerry_release_value(valAsString);
	jerry_release_value(propertyAsString);
	return result;
}

static jerry_value_t StorageRemoveItemHandler(CALL_INFO) {
	REQUIRE_1();
	jerry_value_t propertyAsString = jerry_value_to_string(args[0]);
	jerry_value_t hasOwn = jerry_has_own_property(thisValue, propertyAsString);
	if (jerry_get_boolean_value(hasOwn)) {
		jerry_value_t storageSizeVal = getInternalProperty(thisValue, "size");
		u32 storageFilled = jerry_value_as_uint32(storageSizeVal);
		jerry_release_value(storageSizeVal);

		jerry_value_t currentValue = jerry_get_property(thisValue, propertyAsString);
		if (jerry_delete_property(thisValue, propertyAsString)) {
			storageFilled -= jerry_get_string_size(propertyAsString) + jerry_get_string_size(currentValue) + STORAGE_API_LENGTH_BYTES;
			jerry_value_t newSize = jerry_create_number(storageFilled);
			setInternalProperty(thisValue, "size", newSize);
			jerry_release_value(newSize);
			jerry_value_t isLocal = getInternalProperty(thisValue, "isLocal");
			if (jerry_get_boolean_value(isLocal)) storageRequestSave();
			jerry_release_value(isLocal);
		}
		jerry_release_value(currentValue);
	}
	jerry_release_value(hasOwn);
	jerry_release_value(propertyAsString);
	return undefined;
}

static jerry_value_t StorageClearHandler(CALL_INFO) {
	jerry_value_t props = jerry_get_object_keys(thisValue);
	u32 length = jerry_get_array_length(props);
	for (u32 i = 0; i < length; i++) {
		jerry_value_t prop = jerry_get_property_by_index(props, i);
		jerry_delete_property(thisValue, prop);
		jerry_release_value(prop);
	}
	jerry_release_value(props);
	jerry_value_t zero = jerry_create_number(0);
	setInternalProperty(thisValue, "size", zero);
	jerry_release_value(zero);
	jerry_value_t isLocal = getInternalProperty(thisValue, "isLocal");
	if (jerry_get_boolean_value(isLocal)) storageRequestSave();
	jerry_release_value(isLocal);
	return undefined;
}

static jerry_value_t StorageProxySetHandler(CALL_INFO) {
	jerry_value_t storageSizeVal = getInternalProperty(args[3], "size");
	u32 storageFilled = jerry_value_as_uint32(storageSizeVal);
	jerry_release_value(storageSizeVal);

	jerry_value_t propertyAsString = jerry_value_to_string(args[1]);
	jerry_value_t valAsString = jerry_value_to_string(args[2]);

	jerry_value_t hasOwn = jerry_has_own_property(args[0], propertyAsString);
	if (jerry_get_boolean_value(hasOwn)) {
		jerry_value_t currentValue = jerry_get_property(args[0], propertyAsString);
		storageFilled -= jerry_get_string_size(propertyAsString) + jerry_get_string_size(currentValue) + STORAGE_API_LENGTH_BYTES;
		jerry_release_value(currentValue);
	}
	jerry_release_value(hasOwn);
	storageFilled += jerry_get_string_size(propertyAsString) + jerry_get_string_size(valAsString) + STORAGE_API_LENGTH_BYTES;

	jerry_value_t result;
	if (storageFilled > STORAGE_API_MAX_CAPACITY) {
		result = throwError("Exceeded the quota.");
	}
	else {
		jerry_value_t assignment = jerry_set_property(args[0], propertyAsString, valAsString);
		if (jerry_value_is_error(assignment)) result = assignment;
		else {
			result = True;
			jerry_release_value(assignment);
			jerry_value_t newSize = jerry_create_number(storageFilled);
			setInternalProperty(args[3], "size", newSize);
			jerry_release_value(newSize);
			jerry_value_t isLocal = getInternalProperty(args[3], "isLocal");
			if (jerry_get_boolean_value(isLocal)) storageRequestSave();
			jerry_release_value(isLocal);
		}
	}

	jerry_release_value(propertyAsString);
	jerry_release_value(valAsString);
	return result;
}

static jerry_value_t StorageProxyDeletePropertyHandler(CALL_INFO) {
	jerry_value_t result = True;
	jerry_value_t propertyAsString = jerry_value_to_string(args[1]);
	jerry_value_t hasOwn = jerry_has_own_property(args[0], propertyAsString);
	if (jerry_get_boolean_value(hasOwn)) {
		jerry_value_t proxy = getInternalProperty(args[0], "proxy");
		jerry_value_t storageSizeVal = getInternalProperty(proxy, "size");
		u32 storageFilled = jerry_value_as_uint32(storageSizeVal);
		jerry_release_value(storageSizeVal);
		
		jerry_value_t currentValue = jerry_get_property(args[0], propertyAsString);
		if (jerry_delete_property(args[0], propertyAsString)) {
			storageFilled -= jerry_get_string_size(propertyAsString) + jerry_get_string_size(currentValue) + STORAGE_API_LENGTH_BYTES;
			jerry_value_t newSize = jerry_create_number(storageFilled);
			setInternalProperty(proxy, "size", newSize);
			jerry_release_value(newSize);
			jerry_value_t isLocal = getInternalProperty(proxy, "isLocal");
			if (jerry_get_boolean_value(isLocal)) storageRequestSave();
			jerry_release_value(isLocal);
		}
		else result = False;
		jerry_release_value(currentValue);
		jerry_release_value(proxy);
	}
	jerry_release_value(hasOwn);
	jerry_release_value(propertyAsString);
	return result;
}

static jerry_value_t DSGetBatteryLevelHandler(CALL_INFO) {
	u32 level = getBatteryLevel();
	if (level & BIT(7)) return createString("charging");
	level = level & 0xF;
	if (level == 0x1) return jerry_create_number(0);
	else if (level == 0x3) return jerry_create_number(1);
	else if (level == 0x7) return jerry_create_number(2);
	else if (level == 0xB) return jerry_create_number(3);
	else if (level == 0xF) return jerry_create_number(4);
	else return jerry_create_number_nan();
}

static jerry_value_t DSSetMainScreenHandler(CALL_INFO) {
	if (argCount > 0 && jerry_value_is_string(args[0])) {
		bool set = false;
		char *str = getString(args[0]);
		if (strcmp(str, "top") == 0) {
			lcdMainOnTop();
			set = true;
		}
		else if (strcmp(str, "bottom") == 0) {
			lcdMainOnBottom();
			set = true;
		}
		free(str);
		if (set) return undefined;
	}
	return throwTypeError("Invalid screen value");
}

static jerry_value_t DSSleepHandler(CALL_INFO) {
	jerry_value_t eventArgs[2] = {createString("sleep"), jerry_create_object()};
	setProperty(eventArgs[1], "cancelable", True);
	jerry_value_t event = jerry_construct_object(ref_Event, eventArgs, 2);
	bool canceled = dispatchEvent(ref_global, event, true);
	jerry_release_value(event);
	jerry_release_value(eventArgs[0]);
	jerry_release_value(eventArgs[1]);
	if (!canceled) {
		systemSleep();
		swiWaitForVBlank();
		swiWaitForVBlank(); // I know this is jank but it's the easiest solution to stop 'wake' from dispatching before the system sleeps
		eventArgs[0] = createString("wake");
		eventArgs[1] = jerry_create_object();
		event = jerry_construct_object(ref_Event, eventArgs, 2);
		dispatchEvent(ref_global, event, true);
		jerry_release_value(event);
		jerry_release_value(eventArgs[0]);
		jerry_release_value(eventArgs[1]);
	}
	return undefined;
}

static jerry_value_t DSTouchGetPositionHandler(CALL_INFO) {
	if ((keysHeld() & KEY_TOUCH) == 0) {
		jerry_value_t position = jerry_create_object();
		jerry_value_t NaN = jerry_create_number_nan();
		setProperty(position, "x", NaN);
		setProperty(position, "y", NaN);
		jerry_release_value(NaN);
		return position;
	}
	touchPosition pos; touchRead(&pos);
	jerry_value_t position = jerry_create_object();
	jerry_value_t x = jerry_create_number(pos.px);
	jerry_value_t y = jerry_create_number(pos.py);
	setProperty(position, "x", x);
	setProperty(position, "y", y);
	jerry_release_value(x);
	jerry_release_value(y);
	return position;
}

static jerry_value_t FileReadHandler(CALL_INFO) {
	REQUIRE_1();

	jerry_value_t modeStr = getInternalProperty(thisValue, "mode");
	char *mode = getString(modeStr);
	if (mode[0] != 'r' && mode[1] != '+') {
		free(mode);
		jerry_release_value(modeStr);
		return throwError("Unable to read in current file mode.");
	}
	free(mode);
	jerry_release_value(modeStr);
	
	u32 size = jerry_value_as_uint32(args[0]);
	jerry_value_t arrayBuffer = jerry_create_arraybuffer(size);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer);
	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	u32 bytesRead = fread(buf, 1, size, file);
	if (ferror(file)) {
		jerry_release_value(arrayBuffer);
		return throwError("File read failed.");
	}
	else if (feof(file) && bytesRead == 0) {
		jerry_release_value(arrayBuffer);
		return null;
	}
	else {
		jerry_value_t u8Array = jerry_create_typedarray_for_arraybuffer_sz(JERRY_TYPEDARRAY_UINT8, arrayBuffer, 0, bytesRead);
		jerry_release_value(arrayBuffer);
		return u8Array;
	}
}

static jerry_value_t FileWriteHandler(CALL_INFO) {
	REQUIRE_1();
	EXPECT(jerry_value_is_object(args[0]) && jerry_value_is_typedarray(args[0]) && jerry_get_typedarray_type(args[0]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);

	jerry_value_t modeStr = getInternalProperty(thisValue, "mode");
	char *mode = getString(modeStr);
	if (mode[0] != 'w' && mode[0] != 'a' && mode[1] != '+') {
		free(mode);
		jerry_release_value(modeStr);
		return throwError("Unable to write in current file mode.");
	}
	free(mode);
	jerry_release_value(modeStr);
	
	u32 offset, size;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &offset, &size);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer) + offset;
	jerry_release_value(arrayBuffer);
	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);

	u32 bytesWritten = fwrite(buf, 1, size, file);
	if (ferror(file)) {
		return throwError("File write failed.");
	}
	else return jerry_create_number(bytesWritten);
}

static jerry_value_t FileSeekHandler(CALL_INFO) {
	REQUIRE_1();

	int mode = 10;
	if (argCount > 1) {
		char *seekMode = getAsString(args[1]);
		if (strcmp(seekMode, "start") == 0) mode = SEEK_SET;
		else if (strcmp(seekMode, "current") == 0) mode = SEEK_CUR;
		else if (strcmp(seekMode, "end") == 0) mode = SEEK_END;
		free(seekMode);
	}
	else mode = SEEK_SET;
	if (mode == 10) return throwTypeError("Invalid seek mode");

	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	int success = fseek(file, jerry_value_as_int32(args[0]), mode);
	if (success != 0) return throwError("File seek failed.");
	return undefined;
}

static jerry_value_t FileCloseHandler(CALL_INFO) {
	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	if (fclose(file) != 0) return throwError("File close failed.");
	return undefined;
}

static jerry_value_t FileStaticOpenHandler(CALL_INFO) {
	REQUIRE_1();
	char *path = getAsString(args[0]);
	char defaultMode[2] = "r";
	char *mode = defaultMode;
	if (argCount > 1) {
		mode = getAsString(args[1]);
		if (strcmp(mode, "r") != 0 && strcmp(mode, "r+") != 0
		 && strcmp(mode, "w") != 0 && strcmp(mode, "w+") != 0
		 && strcmp(mode, "a") != 0 && strcmp(mode, "a+") != 0
		) {
			free(mode);
			free(path);
			return throwTypeError("Invalid file mode");
		}
	}

	FILE *file = fopen(path, mode);
	if (file == NULL) {
		if (mode != defaultMode) free(mode);
		free(path);
		return throwError("Unable to open file.");
	}
	else {
		jerry_value_t modeStr = createString(mode);
		jerry_value_t fileObj = newFile(file, modeStr);
		jerry_release_value(modeStr);
		if (mode != defaultMode) free(mode);
		free(path);
		return fileObj;
	}
}

static jerry_value_t FileStaticCopyHandler(CALL_INFO) {
	REQUIRE(2);
	char *sourcePath = getAsString(args[0]);
	FILE *source = fopen(sourcePath, "r");
	free(sourcePath);
	if (source == NULL) return throwError("Unable to open source file during copy.");
	
	fseek(source, 0, SEEK_END);
	u32 size = ftell(source);
	rewind(source);
	u8 *data = (u8 *) malloc(size);
	u32 bytesRead = fread(data, 1, size, source);
	if (ferror(source)) {
		free(data);
		fclose(source);
		return throwError("Failed to read source file during copy.");
	}
	fclose(source);

	char *destPath = getAsString(args[1]);
	FILE *dest = fopen(destPath, "w");
	free(destPath);
	if (dest == NULL) {
		free(data);
		return throwError("Unable to open destination file during copy.");
	}

	fwrite(data, 1, bytesRead, dest);
	free(data);
	if (ferror(dest)) {
		fclose(dest);
		return throwError("Failed to write destination file during copy.");
	}
	fclose(dest);
	return undefined;
}

static jerry_value_t FileStaticRenameHandler(CALL_INFO) {
	REQUIRE(2);
	char *sourcePath = getAsString(args[0]);
	char *destPath = getAsString(args[1]);
	int status = rename(sourcePath, destPath);
	free(sourcePath);
	free(destPath);
	if (status != 0) return throwError("Failed to rename file.");
	return undefined;
}

static jerry_value_t FileStaticRemoveHandler(CALL_INFO) {
	REQUIRE_1();
	char *path = getAsString(args[0]);
	if (remove(path) != 0) return throwError("Failed to delete file.");
	return undefined;
}

static jerry_value_t FileStaticReadHandler(CALL_INFO) {
	REQUIRE_1();
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "r");
	free(path);
	if (file == NULL) return throwError("Unable to open file.");

	fseek(file, 0, SEEK_END);
	u32 size = ftell(file);
	rewind(file);

	jerry_value_t arrayBuffer = jerry_create_arraybuffer(size);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer);
	fread(buf, 1, size, file);
	if (ferror(file)) {
		jerry_release_value(arrayBuffer);
		fclose(file);
		return throwError("File read failed.");
	}
	fclose(file);
	jerry_value_t u8Array = jerry_create_typedarray_for_arraybuffer_sz(JERRY_TYPEDARRAY_UINT8, arrayBuffer, 0, size);
	jerry_release_value(arrayBuffer);
	return u8Array;
}

static jerry_value_t FileStaticReadTextHandler(CALL_INFO) {
	REQUIRE_1();
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "r");
	free(path);
	if (file == NULL) return throwError("Unable to open file.");

	fseek(file, 0, SEEK_END);
	u32 size = ftell(file);
	rewind(file);

	char *buf = (char *) malloc(size);
	fread(buf, 1, size, file);
	if (ferror(file)) {
		free(buf);
		fclose(file);
		return throwError("File read failed.");
	}
	fclose(file);
	jerry_value_t str = jerry_create_string_sz_from_utf8((jerry_char_t *) buf, size);
	free(buf);
	return str;
}

static jerry_value_t FileStaticWriteHandler(CALL_INFO) {
	REQUIRE(2);
	EXPECT(jerry_value_is_object(args[1]) && jerry_value_is_typedarray(args[1]) && jerry_get_typedarray_type(args[1]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "w");
	free(path);
	if (file == NULL) return throwError("Unable to open file.");
	
	u32 offset, size;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[1], &offset, &size);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer) + offset;
	jerry_release_value(arrayBuffer);

	u32 bytesWritten = fwrite(buf, 1, size, file);
	if (ferror(file)) {
		fclose(file);
		return throwError("File write failed.");
	}
	fclose(file);
	return jerry_create_number(bytesWritten);
}

static jerry_value_t FileStaticWriteTextHandler(CALL_INFO) {
	REQUIRE(2);
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "w");
	free(path);
	if (file == NULL) return throwError("Unable to open file.");
	
	u32 size;
	char *text = getAsString(args[1], &size);
	u32 bytesWritten = fwrite(text, 1, size, file);
	if (ferror(file)) {
		fclose(file);
		free(text);
		return throwError("File write failed.");
	}
	fclose(file);
	free(text);
	return jerry_create_number(bytesWritten);
}

static jerry_value_t FileStaticMakeDirHandler(CALL_INFO) {
	REQUIRE_1();
	bool recursive = argCount > 1 ? jerry_value_to_boolean(args[1]) : false;
	char *path = getAsString(args[0]);
	int status = -1;
	if (recursive) {
		char *slash = strchr(path, '/');
		if (strchr(path, ':') != NULL || path == slash) slash = strchr(slash + 1, '/');
		while (slash != NULL) {
			slash[0] = '\0';
			mkdir(path, 0777);
			slash[0] = '/';
			slash = strchr(slash + 1, '/');
		}
		status = access(path, F_OK);
	}
	else status = mkdir(path, 0777);
	free(path);
	if (status != 0) return throwError("Failed to make directory.");
	return undefined;
}

static jerry_value_t FileStaticReadDirHandler(CALL_INFO) {
	REQUIRE_1();
	char *path = getAsString(args[0]);
	DIR *dir = opendir(path);
	free(path);
	if (dir == NULL) return throwError("Unable to open directory.");

	jerry_value_t arr = jerry_create_array(0);
	jerry_value_t push = getProperty(arr, "push");
	dirent *entry = readdir(dir);
	while (entry != NULL) {
		jerry_value_t dirEnt = jerry_create_object();
		setProperty(dirEnt, "isDirectory", jerry_create_boolean(entry->d_type == DT_DIR));
		setProperty(dirEnt, "isFile", jerry_create_boolean(entry->d_type == DT_REG));
		jerry_value_t name = createString(entry->d_name);
		setProperty(dirEnt, "name", name);
		jerry_release_value(name);
		jerry_call_function(push, arr, &dirEnt, 1);
		jerry_release_value(dirEnt);
		entry = readdir(dir);
	}
	closedir(dir);
	jerry_release_value(push);
	return arr;
}

static jerry_value_t BETA_gfxInit(CALL_INFO) {
	videoSetMode(MODE_3_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	return undefined;
}

static jerry_value_t BETA_gfxPixel(CALL_INFO) {
	REQUIRE(3);
	u8 x = jerry_value_as_uint32(args[0]);
	u8 y = jerry_value_as_uint32(args[1]);
	u16 color = jerry_value_as_uint32(args[2]);
	bgGetGfxPtr(3)[x + y*256] = color;
	return undefined;
}

static jerry_value_t BETA_gfxRect(CALL_INFO) {
	REQUIRE(5);
	u8 x = jerry_value_as_uint32(args[0]);
	u8 y = jerry_value_as_uint32(args[1]) % 192;
	u16 width = jerry_value_as_uint32(args[2]);
	u16 height = jerry_value_as_uint32(args[3]);
	u16 color = jerry_value_as_uint32(args[4]);
	u16 *gfx = bgGetGfxPtr(3);
	
	if (width + x > 256) width = 256 - x;
	if (height + y > 192) height = 192 - y;

	if (width == 0 || height == 0) return undefined;

	for (u8 i = 0; i < height; i++) {
		dmaFillHalfWords(color, gfx + x + ((y + i) * 256), width * 2);
	}
	return undefined;
}

void exposeBetaAPI() {
	jerry_value_t beta = jerry_create_object();
	setProperty(ref_global, "beta", beta);

	setMethod(beta, "gfxInit", BETA_gfxInit);
	setMethod(beta, "gfxPixel", BETA_gfxPixel);
	setMethod(beta, "gfxRect", BETA_gfxRect);

	jerry_release_value(beta);
}

void exposeAPI() {
	// hold some internal references
	ref_str_name = createString("name");
	ref_str_constructor = createString("constructor");
	ref_str_prototype = createString("prototype");
	ref_str_backtrace = createString("backtrace");
	ref_sym_toStringTag = jerry_get_well_known_symbol(JERRY_SYMBOL_TO_STRING_TAG);
	ref_consoleCounters = jerry_create_object();
	ref_consoleTimers = jerry_create_object();

	ref_global = jerry_get_global_object();
	setProperty(ref_global, "self", ref_global);
	ref_Error = getProperty(ref_global, "Error");

	setMethod(ref_global, "alert", alertHandler);
	setMethod(ref_global, "clearInterval", clearTimeoutHandler);
	setMethod(ref_global, "clearTimeout", clearTimeoutHandler);
	setMethod(ref_global, "close", closeHandler);
	setMethod(ref_global, "confirm", confirmHandler);
	setMethod(ref_global, "prompt", promptHandler);
	setMethod(ref_global, "setInterval", setIntervalHandler);
	setMethod(ref_global, "setTimeout", setTimeoutHandler);
	
	jerry_value_t console = createNamespace(ref_global, "console");
	setMethod(console, "assert", consoleAssertHandler);
	setMethod(console, "clear", consoleClearHandler);
	setMethod(console, "count", consoleCountHandler);
	setMethod(console, "countReset", consoleCountResetHandler);
	setMethod(console, "debug", consoleDebugHandler);
	setMethod(console, "dir", consoleDirHandler);
	setMethod(console, "error", consoleErrorHandler);
	setMethod(console, "group", consoleGroupHandler);
	setMethod(console, "groupEnd", consoleGroupEndHandler);
	setMethod(console, "info", consoleInfoHandler);
	setMethod(console, "log", consoleLogHandler);
	setMethod(console, "table", consoleTableHandler);
	setMethod(console, "time", consoleTimeHandler);
	setMethod(console, "timeLog", consoleTimeLogHandler);
	setMethod(console, "timeEnd", consoleTimeEndHandler);
	setMethod(console, "trace", consoleTraceHandler);
	setMethod(console, "warn", consoleWarnHandler);
	jerry_release_value(console);

	jerry_value_t keyboard = createNamespace(ref_global, "keyboard");
	setMethod(keyboard, "hide", [](CALL_INFO) -> jerry_value_t { keyboardHide(); return undefined; });
	setMethod(keyboard, "show", [](CALL_INFO) -> jerry_value_t { keyboardShow(); return undefined; });
	jerry_release_value(keyboard);

	jerry_value_t Text = createNamespace(ref_global, "Text");
	setMethod(Text, "encode", TextEncodeHandler);
	setMethod(Text, "decode", TextDecodeHandler);
	jerry_release_value(Text);

	jsClass EventTarget = createClass(ref_global, "EventTarget", EventTargetConstructor);
	setMethod(EventTarget.prototype, "addEventListener", EventTargetAddEventListenerHandler);
	setMethod(EventTarget.prototype, "removeEventListener", EventTargetRemoveEventListenerHandler);
	setMethod(EventTarget.prototype, "dispatchEvent", EventTargetDispatchEventHandler);
	// turn global into an EventTarget
	jerry_release_value(jerry_set_prototype(ref_global, EventTarget.prototype));
	jerry_value_t globalListeners = jerry_create_array(0);
	setInternalProperty(ref_global, "eventListeners", globalListeners);
	jerry_release_value(globalListeners);
	releaseClass(EventTarget);

	jsClass Event = createClass(ref_global, "Event", EventConstructor);
	setMethod(Event.prototype, "stopImmediatePropagation", EventStopImmediatePropagationHandler);
	setMethod(Event.prototype, "preventDefault", EventPreventDefaultHandler);
	releaseClass(extendClass(ref_global, "ErrorEvent", ErrorEventConstructor, Event.prototype));
	releaseClass(extendClass(ref_global, "PromiseRejectionEvent", PromiseRejectionEventConstructor, Event.prototype));
	releaseClass(extendClass(ref_global, "KeyboardEvent", KeyboardEventConstructor, Event.prototype));
	releaseClass(extendClass(ref_global, "CustomEvent", CustomEventConstructor, Event.prototype));
	releaseClass(extendClass(ref_global, "ButtonEvent", ButtonEventConstructor, Event.prototype));
	releaseClass(extendClass(ref_global, "TouchEvent", TouchEventConstructor, Event.prototype));
	ref_Event = Event.constructor;
	jerry_release_value(Event.prototype);

	jsClass Storage = createClass(ref_global, "Storage", IllegalConstructor);
	defGetter(Storage.prototype, "length", StorageLengthGetter);
	setMethod(Storage.prototype, "key", StorageKeyHandler);
	setMethod(Storage.prototype, "getItem", StorageGetItemHandler);
	setMethod(Storage.prototype, "setItem", StorageSetItemHandler);
	setMethod(Storage.prototype, "removeItem", StorageRemoveItemHandler);
	setMethod(Storage.prototype, "clear", StorageClearHandler);
	releaseClass(Storage);
	ref_proxyHandler_storage = jerry_create_object();
	setMethod(ref_proxyHandler_storage, "set", StorageProxySetHandler);
	setMethod(ref_proxyHandler_storage, "deleteProperty", StorageProxyDeletePropertyHandler);

	ref_localStorage = newStorage();
	setProperty(ref_global, "localStorage", ref_localStorage);
	setInternalProperty(ref_localStorage, "isLocal", True);

	defEventAttribute(ref_global, "onerror");
	defEventAttribute(ref_global, "onunhandledrejection");
	defEventAttribute(ref_global, "onkeydown");
	defEventAttribute(ref_global, "onkeyup");
	defEventAttribute(ref_global, "onbuttondown");
	defEventAttribute(ref_global, "onbuttonup");
	defEventAttribute(ref_global, "onsleep");
	defEventAttribute(ref_global, "ontouchstart");
	defEventAttribute(ref_global, "ontouchmove");
	defEventAttribute(ref_global, "ontouchend");
	defEventAttribute(ref_global, "onvblank");
	defEventAttribute(ref_global, "onwake");

	// DS namespace, where most DS-specific functionality lives
	ref_DS = createNamespace(ref_global, "DS");

	setMethod(ref_DS, "getBatteryLevel", DSGetBatteryLevelHandler);
	setMethod(ref_DS, "getMainScreen", [](CALL_INFO) -> jerry_value_t { return createString(REG_POWERCNT & POWER_SWAP_LCDS ? "top" : "bottom"); });
	setReadonly(ref_DS, "isDSiMode", jerry_create_boolean(isDSiMode()));
	setMethod(ref_DS, "setMainScreen", DSSetMainScreenHandler);
	setMethod(ref_DS, "shutdown", [](CALL_INFO) -> jerry_value_t { systemShutDown(); return undefined; });
	setMethod(ref_DS, "sleep", DSSleepHandler);
	setMethod(ref_DS, "swapScreens", [](CALL_INFO) -> jerry_value_t { lcdSwap(); return undefined; });

	jerry_value_t profile = jerry_create_object();
	setProperty(ref_DS, "profile", profile);
	setReadonlyNumber(profile, "alarmHour", PersonalData->alarmHour);
	setReadonlyNumber(profile, "alarmMinute", PersonalData->alarmMinute);
	setReadonlyNumber(profile, "birthDay", PersonalData->birthDay);
	setReadonlyNumber(profile, "birthMonth", PersonalData->birthMonth);
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
	jerry_value_t nameStr = createStringU16((u16 *) PersonalData->name, PersonalData->nameLen);
	setReadonly(profile, "name", nameStr);
	jerry_release_value(nameStr);
	jerry_value_t messageStr = createStringU16((u16 *) PersonalData->message, PersonalData->messageLen);
	setReadonly(profile, "message", messageStr);
	jerry_release_value(messageStr);
	#pragma GCC diagnostic pop
	u16 themeColors[16] = {0xCE0C, 0x8137, 0x8C1F, 0xFE3F, 0x825F, 0x839E, 0x83F5, 0x83E0, 0x9E80, 0xC769, 0xFAE6, 0xF960, 0xC800, 0xE811, 0xF41A, 0xC81F};
	setReadonlyNumber(profile, "color", themeColors[PersonalData->theme]);
	setReadonly(profile, "autoMode", jerry_create_boolean(PersonalData->autoMode));
	jerry_value_t gbaScreenStr = createString(PersonalData->gbaScreen ? "bottom" : "top");
	setReadonly(profile, "gbaScreen", gbaScreenStr);
	jerry_release_value(gbaScreenStr);
	u8 language = PersonalData->language;
	jerry_value_t languageStr = createString(
		language == 0 ? "日本語" :
		language == 1 ? "English" :
		language == 2 ? "Français" :
		language == 3 ? "Deutsch" :
		language == 4 ? "Italiano" :
		language == 5 ? "Español" :
		language == 6 ? "中文" :
		language == 7 ? "한국어" :
		""
	);
	setReadonly(profile, "language", languageStr);
	jerry_release_value(languageStr);
	jerry_release_value(profile);

	jerry_value_t buttons = jerry_create_object();
	setProperty(ref_DS, "buttons", buttons);
	jerry_value_t pressed = jerry_create_object();
	setProperty(buttons, "pressed", pressed);
	defGetter(pressed, "A",      [](CALL_INFO) { return jerry_create_boolean(keysDown() & KEY_A); });
	defGetter(pressed, "B",      [](CALL_INFO) { return jerry_create_boolean(keysDown() & KEY_B); });
	defGetter(pressed, "X",      [](CALL_INFO) { return jerry_create_boolean(keysDown() & KEY_X); });
	defGetter(pressed, "Y",      [](CALL_INFO) { return jerry_create_boolean(keysDown() & KEY_Y); });
	defGetter(pressed, "L",      [](CALL_INFO) { return jerry_create_boolean(keysDown() & KEY_L); });
	defGetter(pressed, "R",      [](CALL_INFO) { return jerry_create_boolean(keysDown() & KEY_R); });
	defGetter(pressed, "Up",     [](CALL_INFO) { return jerry_create_boolean(keysDown() & KEY_UP); });
	defGetter(pressed, "Down",   [](CALL_INFO) { return jerry_create_boolean(keysDown() & KEY_DOWN); });
	defGetter(pressed, "Left",   [](CALL_INFO) { return jerry_create_boolean(keysDown() & KEY_LEFT); });
	defGetter(pressed, "Right",  [](CALL_INFO) { return jerry_create_boolean(keysDown() & KEY_RIGHT); });
	defGetter(pressed, "START",  [](CALL_INFO) { return jerry_create_boolean(keysDown() & KEY_START); });
	defGetter(pressed, "SELECT", [](CALL_INFO) { return jerry_create_boolean(keysDown() & KEY_SELECT); });
	jerry_release_value(pressed);
	jerry_value_t held = jerry_create_object();
	setProperty(buttons, "held", held);
	defGetter(held, "A",      [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_A); });
	defGetter(held, "B",      [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_B); });
	defGetter(held, "X",      [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_X); });
	defGetter(held, "Y",      [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_Y); });
	defGetter(held, "L",      [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_L); });
	defGetter(held, "R",      [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_R); });
	defGetter(held, "Up",     [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_UP); });
	defGetter(held, "Down",   [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_DOWN); });
	defGetter(held, "Left",   [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_LEFT); });
	defGetter(held, "Right",  [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_RIGHT); });
	defGetter(held, "START",  [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_START); });
	defGetter(held, "SELECT", [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_SELECT); });
	jerry_release_value(held);
	jerry_value_t released = jerry_create_object();
	setProperty(buttons, "released", released);
	defGetter(released, "A",      [](CALL_INFO) { return jerry_create_boolean(keysUp() & KEY_A); });
	defGetter(released, "B",      [](CALL_INFO) { return jerry_create_boolean(keysUp() & KEY_B); });
	defGetter(released, "X",      [](CALL_INFO) { return jerry_create_boolean(keysUp() & KEY_X); });
	defGetter(released, "Y",      [](CALL_INFO) { return jerry_create_boolean(keysUp() & KEY_Y); });
	defGetter(released, "L",      [](CALL_INFO) { return jerry_create_boolean(keysUp() & KEY_L); });
	defGetter(released, "R",      [](CALL_INFO) { return jerry_create_boolean(keysUp() & KEY_R); });
	defGetter(released, "Up",     [](CALL_INFO) { return jerry_create_boolean(keysUp() & KEY_UP); });
	defGetter(released, "Down",   [](CALL_INFO) { return jerry_create_boolean(keysUp() & KEY_DOWN); });
	defGetter(released, "Left",   [](CALL_INFO) { return jerry_create_boolean(keysUp() & KEY_LEFT); });
	defGetter(released, "Right",  [](CALL_INFO) { return jerry_create_boolean(keysUp() & KEY_RIGHT); });
	defGetter(released, "START",  [](CALL_INFO) { return jerry_create_boolean(keysUp() & KEY_START); });
	defGetter(released, "SELECT", [](CALL_INFO) { return jerry_create_boolean(keysUp() & KEY_SELECT); });
	jerry_release_value(released);
	jerry_release_value(buttons);

	jerry_value_t touch = jerry_create_object();
	setProperty(ref_DS, "touch", touch);
	defGetter(touch, "start", [](CALL_INFO) { return jerry_create_boolean(keysDown() & KEY_TOUCH); });
	defGetter(touch, "active", [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_TOUCH); });
	defGetter(touch, "end", [](CALL_INFO) { return jerry_create_boolean(keysUp() & KEY_TOUCH); });
	setMethod(touch, "getPosition", DSTouchGetPositionHandler);
	jerry_release_value(touch);

	// Simple custom File class, nothing like the web version
	jsClass File = createClass(ref_global, "File", IllegalConstructor);
	setMethod(File.constructor, "open", FileStaticOpenHandler);
	setMethod(File.constructor, "copy", FileStaticCopyHandler);
	setMethod(File.constructor, "rename", FileStaticRenameHandler);
	setMethod(File.constructor, "remove", FileStaticRemoveHandler);
	setMethod(File.constructor, "read", FileStaticReadHandler);
	setMethod(File.constructor, "readText", FileStaticReadTextHandler);
	setMethod(File.constructor, "write", FileStaticWriteHandler);
	setMethod(File.constructor, "writeText", FileStaticWriteTextHandler);
	setMethod(File.constructor, "makeDir", FileStaticMakeDirHandler);
	setMethod(File.constructor, "readDir", FileStaticReadDirHandler);
	setMethod(File.prototype, "read", FileReadHandler);
	setMethod(File.prototype, "write", FileWriteHandler);
	setMethod(File.prototype, "seek", FileSeekHandler);
	setMethod(File.prototype, "close", FileCloseHandler);
	releaseClass(File);

	exposeBetaAPI();
}

void releaseReferences() {
	jerry_release_value(ref_global);
	jerry_release_value(ref_DS);
	jerry_release_value(ref_localStorage);
	jerry_release_value(ref_Event);
	jerry_release_value(ref_Error);
	jerry_release_value(ref_str_name);
	jerry_release_value(ref_str_constructor);
	jerry_release_value(ref_str_prototype);
	jerry_release_value(ref_str_backtrace);
	jerry_release_value(ref_sym_toStringTag);
	jerry_release_value(ref_proxyHandler_storage);
	jerry_release_value(ref_consoleCounters);
	jerry_release_value(ref_consoleTimers);
}