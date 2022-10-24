#include "api.h"

#include <nds/arm9/input.h>
#include <nds/interrupts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

#include "console.h"
#include "keyboard.h"
#include "inline.h"
#include "jerry/jerryscript.h"
#include "tasks.h"
#include "timeouts.h"



jerry_value_t ref_global;
jerry_value_t ref_localStorage;
jerry_value_t ref_Event;
jerry_value_t ref_Error;
jerry_value_t ref_DOMException;
jerry_value_t ref_task_reportError;
jerry_value_t ref_task_abortSignalTimeout;
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

#define CALL_INFO const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], u32 argCount

static jerry_value_t closeHandler(CALL_INFO) {
	abortFlag = true;
	return jerry_create_abort_from_value(createString("close() was called."), true);
}

static jerry_value_t alertHandler(CALL_INFO) {
	consoleClear();
	printf("============= Alert ============");
	if (argCount > 0) printValue(args[0]);
	printf("===================== (A) OK ===");
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_A) break;
	}
	consoleClear();
	return undefined;
}

static jerry_value_t confirmHandler(CALL_INFO) {
	consoleClear();
	printf("============ Confirm ===========");
	if (argCount > 0) printValue(args[0]);
	printf("========= (A) OK  (B) Cancel ===");
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		u32 keys = keysDown();
		if (keys & KEY_A) return consoleClear(), True;
		else if (keys & KEY_B) return consoleClear(), False;
	}
}

static jerry_value_t promptHandler(CALL_INFO) {
	consoleClear();
	printf("============ Prompt ============");
	if (argCount > 0) printValue(args[0]);
	printf("========= (A) OK  (B) Cancel ===");
	consoleSetWindow(NULL, 0, 3, 32, 11);

	keyboardClearBuffer();
	keyboardShow();
	bool canceled = false;
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		u32 keys = keysDown();
		if (keys & KEY_A || keyboardEnterPressed) break;
		else if (keys & KEY_B || keyboardEscapePressed) {
			canceled = true;
			break;
		}
		keyboardUpdate();
	}
	keyboardHide();

	consoleSetWindow(NULL, 0, 0, 32, 24);
	consoleClear();
	if (canceled) return undefined;
	else return jerry_create_string_from_utf8((jerry_char_t *) keyboardBuffer());
}

static jerry_value_t atobHandler(CALL_INFO) {
	if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'atob': 1 argument required.");
	char errorName[22] = "InvalidCharacterError";
	char errorMsg[25] = "Failed to decode base64.";

	jerry_length_t dataSize;
	char *data = getAsString(args[0], &dataSize);
	char *dataEnd = data + dataSize;

	// (1) strip ASCII whitespace
	jerry_length_t strippedSize = 0;
	for (char *ch = data; ch != dataEnd; ch++) {
		u8 n; // (8.1) find codepoint associated to character
		if (*ch == ' ' || *ch == '\t' || *ch == '\n' || *ch == '\r' || *ch == '\f') continue;
		else if (*ch >= 'A' && *ch <= 'Z') n = *ch - 'A';
		else if (*ch >= 'a' && *ch <= 'z') n = *ch - 'a' + 26;
		else if (*ch >= '0' && *ch <= '9') n = *ch - '0' + 52;
		else if (*ch == '+') n = 62;
		else if (*ch == '/') n = 63;
		else if (*ch == '=') n = 64; // equal sign will be handled later
		else { // (4) error on invalid character
			free(data);
			return throwDOMException(errorMsg, errorName);
		}
		*ch = n;
		strippedSize++;
	}
	dataEnd = data + strippedSize;
	*dataEnd = '\0';

	// (2) handle ending equal signs
	if (strippedSize > 0 && strippedSize % 4 == 0) {
		if (data[strippedSize - 1] == 64) data[--strippedSize] = '\0';
		if (data[strippedSize - 1] == 64) data[--strippedSize] = '\0';
		dataEnd = data + strippedSize;
	}
	// (3) fail on %4==1
	else if (strippedSize % 4 == 1) {
		free(data);
		return throwDOMException(errorMsg, errorName);
	}

	// (5-8) output, buffer, position and loop
	char *output = (char *) malloc((strippedSize + 1) * 2);
	char *out = output;
	int buffer = 0, bits = 0;
	for (char *ch = data; ch != dataEnd; ch++) {
		if (*ch == 64) { // (4 again) fail on equal sign not at end of string
			free(data);
			return throwDOMException(errorMsg, errorName);
		}
		buffer = buffer << 6 | *ch; // (8.2) append 6 bits to buffer
		bits += 6;
		if (bits == 24) { // (8.3) output 3 8-bit numbers when buffer reaches 24 bits
			out = writeBinByteToUTF8(buffer >> 16, out);
			out = writeBinByteToUTF8(buffer >> 8 & 0xFF, out);
			out = writeBinByteToUTF8(buffer & 0xFF, out);
			buffer = bits = 0;
		}
	}
	// (9) handle remaining bits in buffer
	if (bits == 12) out = writeBinByteToUTF8(buffer >> 4, out);
	else if (bits == 18) {
		out = writeBinByteToUTF8(buffer >> 10, out);
		out = writeBinByteToUTF8(buffer >> 2 & 0xFF, out);
	}
	*out = '\0';
	free(data);
	
	jerry_value_t result = jerry_create_string_from_utf8((jerry_char_t *) output);
	free(output);
	return result;
}

const char b64Chars[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static jerry_value_t btoaHandler(CALL_INFO) {
	if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'btoa': 1 argument required.");

	jerry_length_t dataSize;
	char *data = getAsString(args[0], &dataSize);
	char *dataEnd = data + dataSize;

	// convert UTF8 representation to binary, and count size
	jerry_length_t binSize = 0;
	for (char *ch = data; ch != dataEnd; ch++) {
		if (*ch & BIT(7)) { // greater than U+007F
			if (*ch & 0b00111100) { // greater than U+00FF, is out of range and therefore invalid
				free(data);
				return throwDOMException("Failed to execute 'btoa': The string to be encoded contains characters outside of the Latin1 range.", "InvalidCharacterError");
			}
			*ch = (*ch << 6 & 0b11000000) | (*ch & 0b00111111);
			ch++;
			binSize += 2;
		}
		else binSize++;
	}
	dataEnd = data + binSize;

	// allocate just enough memory, then finish the conversion to ASCII
	char *output = (char *) malloc(((binSize + 2) / 3) * 4);
	char *out = output;
	int buffer = 0, bits = 0;
	for (char *ch = data; ch != dataEnd; ch++) {
		buffer = buffer << 8 | *ch;
		bits += 8;
		if (bits == 24) {
			*(out++) = b64Chars[buffer >> 18];
			*(out++) = b64Chars[buffer >> 12 & 0b00111111];
			*(out++) = b64Chars[buffer >> 6 & 0b00111111];
			*(out++) = b64Chars[buffer & 0b00111111];
			buffer = bits = 0;
		}
	}
	if (bits == 8) {
		*(out++) = b64Chars[buffer >> 2];
		*(out++) = b64Chars[buffer << 4 & 0b00110000];
		*(out++) = '=';
		*(out++) = '=';
	}
	else if (bits == 16) {
		*(out++) = b64Chars[buffer >> 10];
		*(out++) = b64Chars[buffer >> 4 & 0b00111111];
		*(out++) = b64Chars[buffer << 2 & 0b00111100];
		*(out++) = '=';
	}
	*out = '\0';
	free(data);

	jerry_value_t result = createString(output);
	free(output);
	return result;
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

static jerry_value_t reportErrorHandler(CALL_INFO) {
	if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'reportError': 1 argument required.");
	jerry_value_t error = jerry_create_error_from_value(args[0], false);
	handleError(error, true);
	jerry_release_value(error);
	return undefined;
}

static jerry_value_t queueMicrotaskHandler(CALL_INFO) {
	if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'queueMicrotask': 1 argument required.");
	if (!jerry_value_is_function(args[0])) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'queueMicrotask': parameter is not of type 'Function'.");

	jerry_value_t promise = jerry_create_promise();
	jerry_value_t thenFunc = getProperty(promise, "then");
	jerry_value_t thenPromise = jerry_call_function(thenFunc, promise, args, 1);
	jerry_value_t catchFunc = getProperty(thenPromise, "catch");
	jerry_release_value(jerry_call_function(catchFunc, thenPromise, &ref_task_reportError, 1));
	jerry_release_value(catchFunc);
	jerry_release_value(thenPromise);
	jerry_release_value(thenFunc);

	jerry_release_value(jerry_resolve_or_reject_promise(promise, undefined, true));
	jerry_release_value(promise);
	return undefined;
}

static jerry_value_t consoleLogHandler(CALL_INFO) {
	if (argCount > 0) {
		consoleIndent();
		consolePrint(args, argCount);
	}
	return undefined;
}

static jerry_value_t consoleInfoHandler(CALL_INFO) {
	if (argCount > 0) {
		u16 pal = mainConsole->fontCurPal;
		mainConsole->fontCurPal = ConsolePalette::AQUA;
		consoleIndent();
		consolePrint(args, argCount);
		mainConsole->fontCurPal = pal;
	}
	return undefined;
}

static jerry_value_t consoleWarnHandler(CALL_INFO) {
	if (argCount > 0) {
		u16 pal = mainConsole->fontCurPal;
		mainConsole->fontCurPal = ConsolePalette::YELLOW;
		consoleIndent();
		consolePrint(args, argCount);
		mainConsole->fontCurPal = pal;
	}
	return undefined;
}

static jerry_value_t consoleErrorHandler(CALL_INFO) {
	if (argCount > 0) {
		u16 pal = mainConsole->fontCurPal;
		mainConsole->fontCurPal = ConsolePalette::RED;
		consoleIndent();
		consolePrint(args, argCount);
		mainConsole->fontCurPal = pal;
	}
	return undefined;
}

static jerry_value_t consoleAssertHandler(CALL_INFO) {
	if (argCount == 0 || !jerry_value_to_boolean(args[0])) {
		u16 pal = mainConsole->fontCurPal;
		mainConsole->fontCurPal = ConsolePalette::RED;
		consoleIndent();
		printf("Assertion failed: ");
		consolePrint(args + 1, argCount - 1);
		mainConsole->fontCurPal = pal;
	}
	return undefined;
}

static jerry_value_t consoleDebugHandler(CALL_INFO) {
	if (argCount > 0) {
		u16 pal = mainConsole->fontCurPal;
		mainConsole->fontCurPal = ConsolePalette::NAVY;
		consoleIndent();
		consolePrint(args, argCount);
		mainConsole->fontCurPal = pal;
	}
	return undefined;
}

static jerry_value_t consoleTraceHandler(CALL_INFO) {
	consoleIndent();
	if (argCount == 0) printf("Trace\n");
	else consolePrint(args, argCount);
	jerry_value_t backtrace = jerry_get_backtrace(10);
	u32 length = jerry_get_array_length(backtrace);
	for (u32 i = 0; i < length; i++) {
		jerry_value_t traceLine = jerry_get_property_by_index(backtrace, i);
		char *step = getString(traceLine);
		consoleIndent();
		printf(" @ %s\n", step);
		free(step);
		jerry_release_value(traceLine);
	}
	jerry_release_value(backtrace);
	return undefined;
}

static jerry_value_t consoleDirHandler(CALL_INFO) {
	if (argCount > 0) {
		consoleIndent();
		if (jerry_value_is_object(args[0])) consolePrintObject(args[0]);
		else consolePrintLiteral(args[0]);
		putchar('\n');
	}
	return undefined;
}

static jerry_value_t consoleDirxmlHandler(CALL_INFO) {
	if (argCount > 0) {
		consoleIndent();
		for (u32 i = 0; i < argCount; i++) {
			consolePrintLiteral(args[i]);
			if (i < argCount - 1) putchar(' ');
		}
		putchar('\n');
	}
	return undefined;
}

static jerry_value_t consoleTableHandler(CALL_INFO) {
	if (argCount > 0) consolePrintTable(args, argCount);
	return undefined;
}

static jerry_value_t consoleGroupHandler(CALL_INFO) {
	consoleIndentAdd();
	return undefined;
}

static jerry_value_t consoleGroupEndHandler(CALL_INFO) {
	consoleIndentRemove();
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

	consoleIndent();
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
		consoleIndent();
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
		consoleIndent();
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

	consoleIndent();
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

	consoleIndent();
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

static jerry_value_t DOMExceptionConstructor(CALL_INFO) {
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor DOMException cannot be invoked without 'new'");

	jerry_value_t messageVal = argCount > 0 ? jerry_value_to_string(args[0]) : createString("");
	setReadonly(thisValue, "message", messageVal);
	jerry_release_value(messageVal);
	jerry_value_t nameVal = argCount > 1 ? jerry_value_to_string(args[1]) : createString("Error");
	setReadonly(thisValue, "name", nameVal);
	jerry_release_value(nameVal);
	
	return undefined;
}

static jerry_value_t EventConstructor(CALL_INFO) {
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor Event cannot be invoked without 'new'");
	else if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to construct 'Event': 1 argument required.");

	jerry_value_t zero = jerry_create_number(0);
	setInternalProperty(thisValue, "initialized", True);               // initialized flag
	setInternalProperty(thisValue, "dispatch", False);                 // dispatch flag
	setInternalProperty(thisValue, "inPassiveListener", False);        // in passive listener flag
	setInternalProperty(thisValue, "stopPropagation", False);          // stop propagation flag
	setInternalProperty(thisValue, "stopImmediatePropagation", False); // stop immediate propagation flag
	setReadonly(thisValue, "target", null);
	setReadonly(thisValue, "currentTarget", null);
	setReadonly(thisValue, "eventPhase", zero);
	setReadonly(thisValue, "bubbles", False);
	setReadonly(thisValue, "cancelable", False);
	setReadonly(thisValue, "defaultPrevented", False);                 // canceled flag
	setReadonly(thisValue, "composed", False);                         // composed flag
	setReadonly(thisValue, "isTrusted", False);
	jerry_value_t currentTime = jerry_create_number(time(NULL));
	setReadonly(thisValue, "timeStamp", currentTime);
	jerry_release_value(currentTime);
	jerry_release_value(zero);

	jerry_value_t typeAsString = jerry_value_to_string(args[0]);	
	setReadonly(thisValue, "type", typeAsString);
	jerry_release_value(typeAsString);

	if (argCount > 1 && jerry_value_is_object(args[1])) {
		jerry_value_t bubblesVal = getProperty(args[1], "bubbles");
		jerry_value_t bubblesBool = jerry_create_boolean(jerry_value_to_boolean(bubblesVal));
		setInternalProperty(thisValue, "bubbles", bubblesBool);
		jerry_release_value(bubblesBool);
		jerry_release_value(bubblesVal);
		jerry_value_t cancelableVal = getProperty(args[1], "cancelable");
		jerry_value_t cancelableBool = jerry_create_boolean(jerry_value_to_boolean(cancelableVal));
		setInternalProperty(thisValue, "cancelable", cancelableBool);
		jerry_release_value(cancelableBool);
		jerry_release_value(cancelableVal);
		jerry_value_t composedVal = getProperty(args[1], "composed");
		jerry_value_t composedBool = jerry_create_boolean(jerry_value_to_boolean(composedVal));
		setInternalProperty(thisValue, "composed", composedBool);
		jerry_release_value(composedBool);
		jerry_release_value(composedVal);
	}

	return undefined;
}

static jerry_value_t EventNONEGetter(CALL_INFO)            { return jerry_create_number(0); }
static jerry_value_t EventCAPTURING_PHASEGetter(CALL_INFO) { return jerry_create_number(1); }
static jerry_value_t EventAT_TARGETGetter(CALL_INFO)       { return jerry_create_number(2); }
static jerry_value_t EventBUBBLING_PHASEGetter(CALL_INFO)  { return jerry_create_number(3); }

static jerry_value_t EventComposedPathHandler(CALL_INFO) {
	jerry_value_t dispatchVal = getInternalProperty(thisValue, "dispatch");
	bool dispatching = jerry_get_boolean_value(dispatchVal);
	jerry_release_value(dispatchVal);
	if (dispatching) {
		jerry_value_t arr = jerry_create_array(1);
		jerry_release_value(jerry_set_property_by_index(arr, 0, thisValue));
		return arr;
	}
	return jerry_create_array(0);
}

static jerry_value_t EventStopPropagationHandler(CALL_INFO) {
	setInternalProperty(thisValue, "stopPropagation", True);
	return undefined;
}

static jerry_value_t EventStopImmediatePropagationHandler(CALL_INFO) {
	setInternalProperty(thisValue, "stopPropagation", True);
	setInternalProperty(thisValue, "stopImmediatePropagation", True);
	return undefined;
}

static jerry_value_t EventPreventDefaultHandler(CALL_INFO) {
	jerry_value_t cancelable = getInternalProperty(thisValue, "cancelable");
	jerry_value_t inPassiveListener = getInternalProperty(thisValue, "inPassiveListener");
	if (jerry_value_to_boolean(cancelable) && !jerry_value_to_boolean(inPassiveListener)) {
		setInternalProperty(thisValue, "defaultPrevented", True);
	}
	jerry_release_value(inPassiveListener);
	jerry_release_value(cancelable);
	return undefined;
}

static jerry_value_t EventTargetConstructor(CALL_INFO) {
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor EventTarget cannot be invoked without 'new'");

	jerry_value_t eventListenerList = jerry_create_object();
	setInternalProperty(thisValue, "eventListeners", eventListenerList);
	jerry_release_value(eventListenerList);

	return undefined;
}

void abortSignalAddAlgorithm(jerry_value_t signal, jerry_value_t handler, jerry_value_t thisValue, const jerry_value_t *args, u32 argCount);

static jerry_value_t EventTargetAddEventListenerHandler(CALL_INFO) {
	if (argCount < 2) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'addEventListener': 2 arguments required.");
	if (jerry_value_is_null(args[1])) return undefined;
	jerry_value_t target = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	
	jerry_value_t typeStr = createString("type");
	jerry_value_t callbackStr = createString("callback");
	jerry_value_t captureStr = createString("capture");
	jerry_value_t onceStr = createString("once");
	jerry_value_t passiveStr = createString("passive");

	jerry_value_t typeVal = jerry_value_to_string(args[0]);
	jerry_value_t callbackVal = args[1];
	bool capture = false;
	bool once = false;
	bool passive = false;
	jerry_value_t signal = null;
	if (argCount > 2) {
		if (jerry_value_is_object(args[2])) {
			jerry_value_t captureVal = jerry_get_property(args[2], captureStr);
			capture = jerry_value_to_boolean(captureVal);
			jerry_release_value(captureVal);
			
			jerry_value_t onceVal = jerry_get_property(args[2], onceStr);
			once = jerry_value_to_boolean(onceVal);
			jerry_release_value(onceVal);
			
			jerry_value_t passiveVal = jerry_get_property(args[2], passiveStr);
			passive = jerry_value_to_boolean(passiveVal);
			jerry_release_value(passiveVal);
			
			jerry_value_t signalVal = getProperty(args[2], "signal");
			if (!jerry_value_is_undefined(signalVal)) {
				jerry_value_t AbortSignal = getProperty(ref_global, "AbortSignal");
				jerry_value_t isAbortSignalVal = jerry_binary_operation(JERRY_BIN_OP_INSTANCEOF, signalVal, AbortSignal);
				bool isAbortSignal = jerry_get_boolean_value(isAbortSignalVal);
				jerry_release_value(isAbortSignalVal);
				jerry_release_value(AbortSignal);
				if (!isAbortSignal) {
					jerry_release_value(signalVal);
					jerry_release_value(typeVal);
					jerry_release_value(passiveStr);
					jerry_release_value(onceStr);
					jerry_release_value(captureStr);
					jerry_release_value(callbackStr);
					jerry_release_value(typeStr);
					jerry_release_value(target);
					return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'addEventListener': signal was not an AbortSignal.");
				}
				else {
					jerry_value_t abortedVal = getInternalProperty(signalVal, "aborted");
					bool isAborted = jerry_get_boolean_value(abortedVal);
					jerry_release_value(abortedVal);
					if (isAborted) {
						jerry_release_value(signalVal);
						jerry_release_value(typeVal);
						jerry_release_value(passiveStr);
						jerry_release_value(onceStr);
						jerry_release_value(captureStr);
						jerry_release_value(callbackStr);
						jerry_release_value(typeStr);
						jerry_release_value(target);
						return undefined;
					}
					else signal = signalVal;
				}
			}
			else jerry_release_value(signalVal);
		}
		else capture = jerry_value_to_boolean(args[2]);
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
		jerry_value_t storedCapture = jerry_get_property(storedListener, captureStr);
		jerry_value_t callbackEquality = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, callbackVal, storedCallback);
		bool captureEquality = capture == jerry_get_boolean_value(storedCapture);
		if (jerry_get_boolean_value(callbackEquality) && captureEquality) {
			shouldAppend = false;
		}
		jerry_release_value(callbackEquality);
		jerry_release_value(storedCapture);
		jerry_release_value(storedCallback);
		jerry_release_value(storedListener);
	}

	if (shouldAppend) {
		jerry_value_t listener = jerry_create_object();

		jerry_release_value(jerry_set_property(listener, typeStr, typeVal));
		jerry_release_value(jerry_set_property(listener, callbackStr, callbackVal));
		jerry_value_t captureVal = jerry_create_boolean(capture);
		jerry_release_value(jerry_set_property(listener, captureStr, captureVal));
		jerry_release_value(captureVal);
		jerry_value_t onceVal = jerry_create_boolean(once);
		jerry_release_value(jerry_set_property(listener, onceStr, onceVal));
		jerry_release_value(onceVal);
		jerry_value_t passiveVal = jerry_create_boolean(passive);
		jerry_release_value(jerry_set_property(listener, passiveStr, passiveVal));
		jerry_release_value(passiveVal);
		setProperty(listener, "signal", signal);
		if (!jerry_value_is_null(signal)) {
			jerry_value_t removeEventListener = getProperty(target, "removeEventListener");
			abortSignalAddAlgorithm(signal, removeEventListener, target, args, argCount);
			jerry_release_value(removeEventListener);
		}
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
			else if (strcmp(type, "stylusdown") == 0) dependentEvents |= stylusdown;
			else if (strcmp(type, "stylusmove") == 0) dependentEvents |= stylusmove;
			else if (strcmp(type, "stylusup") == 0) dependentEvents |= stylusup;
			free(type);
		}
		jerry_release_value(isGlobal);

		jerry_release_value(listener);
	}

	jerry_release_value(listenersOfType);
	jerry_release_value(eventListeners);
	jerry_release_value(signal);
	jerry_release_value(typeVal);
	jerry_release_value(passiveStr);
	jerry_release_value(onceStr);
	jerry_release_value(captureStr);
	jerry_release_value(callbackStr);
	jerry_release_value(typeStr);

	return undefined;
}

static jerry_value_t EventTargetRemoveEventListenerHandler(CALL_INFO) {
	if (argCount < 2) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'removeEventListener': 2 arguments required.");
	jerry_value_t target = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	
	jerry_value_t typeStr = createString("type");
	jerry_value_t callbackStr = createString("callback");
	jerry_value_t captureStr = createString("capture");

	jerry_value_t typeVal = jerry_value_to_string(args[0]);
	jerry_value_t callbackVal = args[1];
	bool capture = false;
	if (argCount > 2) {
		if (jerry_value_is_object(args[2])) {
			jerry_value_t captureVal = jerry_get_property(args[2], captureStr);
			capture = jerry_value_to_boolean(captureVal);
			jerry_release_value(captureVal);
		}
		else capture = jerry_value_to_boolean(args[2]);
	}

	jerry_value_t eventListeners = getInternalProperty(target, "eventListeners");
	jerry_value_t listenersOfType = jerry_get_property(eventListeners, typeVal);
	if (jerry_value_is_array(listenersOfType)) {
		u32 length = jerry_get_array_length(listenersOfType);
		bool removed = false;
		for (u32 i = 0; !removed && i < length; i++) {
			jerry_value_t storedListener = jerry_get_property_by_index(listenersOfType, i);
			jerry_value_t storedCallback = jerry_get_property(storedListener, callbackStr);
			jerry_value_t storedCapture = jerry_get_property(storedListener, captureStr);
			jerry_value_t callbackEquality = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, callbackVal, storedCallback);
			bool captureEquality = capture == jerry_get_boolean_value(storedCapture);
			if (jerry_get_boolean_value(callbackEquality) && captureEquality) {
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
						else if (strcmp(type, "stylusdown") == 0) dependentEvents &= ~(stylusdown);
						else if (strcmp(type, "stylusmove") == 0) dependentEvents &= ~(stylusmove);
						else if (strcmp(type, "stylusup") == 0) dependentEvents &= ~(stylusup);
						free(type);
					}
					jerry_release_value(isGlobal);
				}
			}
			jerry_release_value(callbackEquality);
			jerry_release_value(storedCapture);
			jerry_release_value(storedCallback);
			jerry_release_value(storedListener);
		}
	}
	jerry_release_value(listenersOfType);
	jerry_release_value(eventListeners);
	jerry_release_value(typeVal);
	jerry_release_value(captureStr);
	jerry_release_value(callbackStr);
	jerry_release_value(typeStr);

	return undefined;
}

static jerry_value_t EventTargetDispatchEventHandler(CALL_INFO) {
	if (argCount < 1) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'dispatchEvent': 1 argument required.");
	jerry_value_t target = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	jerry_value_t isInstanceVal = jerry_binary_operation(JERRY_BIN_OP_INSTANCEOF, args[0], ref_Event);
	bool isInstance = jerry_get_boolean_value(isInstanceVal);
	jerry_release_value(isInstanceVal);
	if (!isInstance) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Not an instance of Event.");

	jerry_value_t dispatchVal = getProperty(args[0], "dispatch");
	bool dispatched = jerry_get_boolean_value(dispatchVal);
	jerry_release_value(dispatchVal);
	if (dispatched) return throwDOMException("Invalid event state", "InvalidStateError");

	bool canceled = dispatchEvent(target, args[0], true);
	return jerry_create_boolean(!canceled);
}

static jerry_value_t ErrorEventConstructor(CALL_INFO) {
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor ErrorEvent cannot be invoked without 'new'");
	else if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to construct 'ErrorEvent': 1 argument required.");
	EventConstructor(function, thisValue, args, argCount);

	jerry_value_t messageProp = createString("message");
	jerry_value_t filenameProp = createString("filename");
	jerry_value_t linenoProp = createString("lineno");
	jerry_value_t colnoProp = createString("colno");
	jerry_value_t emptyStr = createString("");
	jerry_value_t zero = jerry_create_number(0);
	setReadonly(thisValue, "error", undefined);
	setReadonlyJV(thisValue, messageProp, emptyStr);
	setReadonlyJV(thisValue, filenameProp, emptyStr);
	setReadonlyJV(thisValue, linenoProp, zero);
	setReadonlyJV(thisValue, colnoProp, zero);
	jerry_release_value(zero);
	jerry_release_value(emptyStr);

	if (argCount > 1 && jerry_value_is_object(args[1])) {
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
		jerry_value_t colnoVal = jerry_get_property(args[1], colnoProp);
		if (!jerry_value_is_undefined(colnoVal)) {
			jerry_value_t colnoNum = jerry_value_to_number(colnoVal);
			jerry_set_internal_property(thisValue, colnoProp, colnoNum);
			jerry_release_value(colnoNum);
		}
		jerry_release_value(colnoVal);
		jerry_value_t errorProp = createString("error");
		jerry_value_t errorVal = jerry_get_property(args[1], errorProp);
		jerry_set_internal_property(thisValue, errorProp, errorVal);
		jerry_release_value(errorVal);
		jerry_release_value(errorProp);
	}

	jerry_release_value(colnoProp);
	jerry_release_value(linenoProp);
	jerry_release_value(filenameProp);
	jerry_release_value(messageProp);

	return undefined;
}

static jerry_value_t PromiseRejectionEventConstructor(CALL_INFO) {
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor PromiseRejectionEvent cannot be invoked without 'new'");
	else if (argCount < 2) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to construct 'PromiseRejectionEvent': 2 arguments required.");
	else if (!jerry_value_is_object(args[1])) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to construct 'PromiseRejectionEvent': The provided value is not of type 'PromiseRejectionEventInit'");

	jerry_value_t promiseStr = createString("promise");
	jerry_value_t promiseVal = jerry_get_property(args[1], promiseStr);
	if (jerry_value_is_undefined(promiseVal)) {
		jerry_release_value(promiseStr);
		jerry_release_value(promiseVal);
		return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to construct 'PromiseRejectionEvent': Failed to read the 'promise' property from options.");
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

static jerry_value_t CustomEventConstructor(CALL_INFO) {
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor CustomEvent cannot be invoked without 'new'");
	else if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to construct 'CustomEvent': 1 argument required.");
	EventConstructor(function, thisValue, args, argCount);

	jerry_value_t detailProp = createString("detail");
	setReadonlyJV(thisValue, detailProp, null);
	if (argCount > 1 && jerry_value_is_object(args[1])) {
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
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor ButtonEvent cannot be invoked without 'new'");
	else if (argCount < 2) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to construct 'ButtonEvent': 2 arguments required.");
	else if (!jerry_value_is_object(args[1])) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to construct 'ButtonEvent': The provided value is not of type 'ButtonEventInit'");

	jerry_value_t buttonStr = createString("button");
	jerry_value_t buttonVal = jerry_get_property(args[1], buttonStr);
	if (jerry_value_is_undefined(buttonVal)) {
		jerry_release_value(buttonStr);
		jerry_release_value(buttonVal);
		return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to construct 'ButtonEvent': Failed to read the 'button' property from options.");
	}
	EventConstructor(function, thisValue, args, argCount);

	jerry_value_t buttonAsStr = jerry_value_to_string(buttonVal);
	setReadonlyJV(thisValue, buttonStr, buttonAsStr);
	jerry_release_value(buttonAsStr);
	jerry_release_value(buttonVal);
	jerry_release_value(buttonStr);

	return undefined;
}

static jerry_value_t StylusEventConstructor(CALL_INFO) {
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor StylusEvent cannot be invoked without 'new'");
	else if (argCount < 2) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to construct 'StylusEvent': 2 arguments required.");
	else if (!jerry_value_is_object(args[1])) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to construct 'StylusEvent': The provided value is not of type 'StylusEventInit'");
	EventConstructor(function, thisValue, args, argCount);

	jerry_value_t xStr = createString("x");
	jerry_value_t xVal = jerry_get_property(args[1], xStr);
	jerry_value_t xNum = jerry_value_to_number(xVal);
	setReadonlyJV(thisValue, xStr, xNum);
	jerry_release_value(xNum);
	jerry_release_value(xVal);
	jerry_release_value(xStr);
	jerry_value_t yStr = createString("y");
	jerry_value_t yVal = jerry_get_property(args[1], yStr);
	jerry_value_t yNum = jerry_value_to_number(yVal);
	setReadonlyJV(thisValue, yStr, yNum);
	jerry_release_value(yNum);
	jerry_release_value(yVal);
	jerry_release_value(yStr);
	jerry_value_t dxStr = createString("dx");
	jerry_value_t dxVal = jerry_get_property(args[1], dxStr);
	jerry_value_t dxNum = jerry_value_to_number(dxVal);
	setReadonlyJV(thisValue, dxStr, dxNum);
	jerry_release_value(dxNum);
	jerry_release_value(dxVal);
	jerry_release_value(dxStr);
	jerry_value_t dyStr = createString("dy");
	jerry_value_t dyVal = jerry_get_property(args[1], dyStr);
	jerry_value_t dyNum = jerry_value_to_number(dyVal);
	setReadonlyJV(thisValue, dyStr, dyNum);
	jerry_release_value(dyNum);
	jerry_release_value(dyVal);
	jerry_release_value(dyStr);

	return undefined;
}

jerry_value_t createAbortSignal(bool aborted) {
	jerry_value_t signal = jerry_create_object();
	jerry_value_t AbortSignal = getProperty(ref_global, "AbortSignal");
	jerry_value_t AbortSignalPrototype = jerry_get_property(AbortSignal, ref_str_prototype);
	jerry_release_value(jerry_set_prototype(signal, AbortSignalPrototype));
	jerry_release_value(AbortSignalPrototype);
	jerry_release_value(AbortSignal);

	jerry_value_t eventListeners = jerry_create_object();
	setInternalProperty(signal, "eventListeners", eventListeners);
	jerry_release_value(eventListeners);
	jerry_value_t abortAlgorithms = jerry_create_array(0);
	setInternalProperty(signal, "abortAlgorithms", abortAlgorithms);
	jerry_release_value(abortAlgorithms);
	jerry_value_t abortedVal = jerry_create_boolean(aborted);
	setReadonly(signal, "aborted", abortedVal);
	jerry_release_value(abortedVal);
	defEventAttribute(signal, "onabort");
	return signal;
}

void abortSignalAddAlgorithm(jerry_value_t signal, jerry_value_t handler, jerry_value_t thisValue, const jerry_value_t *args, u32 argCount) {
	jerry_value_t alg = jerry_create_object();
	setProperty(alg, "handler", handler);
	setProperty(alg, "thisValue", thisValue);
	jerry_value_t argsArray = jerry_create_array(argCount);
	for (u32 i = 0; i < argCount; i++) jerry_set_property_by_index(argsArray, i, args[i]);
	setProperty(alg, "args", argsArray);
	jerry_release_value(argsArray);
	
	jerry_value_t algs = getInternalProperty(signal, "abortAlgorithms");
	jerry_value_t pushFunc = getProperty(algs, "push");
	jerry_release_value(jerry_call_function(pushFunc, algs, &alg, 1));
	jerry_release_value(pushFunc);
	jerry_release_value(algs);
	jerry_release_value(alg);
}

void abortSignalRunAlgorithms(jerry_value_t signal) {
	jerry_value_t algs = getInternalProperty(signal, "abortAlgorithms");
	u32 length = jerry_get_array_length(algs);
	for (u32 i = 0; i < length; i++) {
		jerry_value_t alg = jerry_get_property_by_index(algs, i);
		jerry_value_t handler = getProperty(alg, "handler");
		jerry_value_t thisValue = getProperty(alg, "thisValue");
		jerry_value_t argArray = getProperty(alg, "args");
		u32 argCount = jerry_get_array_length(argArray);
		jerry_value_t args[argCount];
		for (u32 j = 0; j < argCount; j++) {
			args[j] = jerry_get_property_by_index(argArray, j);
		}
		jerry_release_value(jerry_call_function(handler, thisValue, args, argCount));
		for (u32 j = 0; j < argCount; j++) jerry_release_value(args[j]);
		jerry_release_value(argArray);
		jerry_release_value(thisValue);
		jerry_release_value(handler);
		jerry_release_value(alg);
	}
	jerry_release_value(algs);

	algs = jerry_create_array(0);
	setInternalProperty(signal, "abortAlgorithms", algs);
	jerry_release_value(algs);
}

static jerry_value_t AbortControllerConstructor(CALL_INFO) {
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor AbortController cannot be invoked without 'new'");

	jerry_value_t signal = createAbortSignal(false);
	setReadonly(thisValue, "signal", signal);
	setReadonly(signal, "reason", undefined);
	jerry_release_value(signal);

	return undefined;
}

static jerry_value_t AbortControllerAbortHandler(CALL_INFO) {
	jerry_value_t signal = getInternalProperty(thisValue, "signal");
	jerry_value_t abortedStr = createString("aborted");
	jerry_value_t abortedVal = jerry_get_internal_property(signal, abortedStr);
	if (!jerry_get_boolean_value(abortedVal)) {
		abortSignalRunAlgorithms(signal);

		if (argCount > 0 && !jerry_value_is_undefined(args[0])) setInternalProperty(signal, "reason", args[0]);
		else {
			jerry_value_t exception = createDOMException("signal is aborted without reason", "AbortError");
			setInternalProperty(signal, "reason", exception);
			jerry_release_value(exception);
		}
		jerry_value_t abortStr = createString("abort");
		jerry_value_t abortEvent = jerry_construct_object(ref_Event, &abortStr, 1);
		jerry_release_value(abortStr);

		setInternalProperty(abortEvent, "isTrusted", True);

		dispatchEvent(signal, abortEvent, true);
		jerry_release_value(abortEvent);

		jerry_set_internal_property(signal, abortedStr, True);
	}
	jerry_release_value(abortedVal);
	jerry_release_value(abortedStr);
	jerry_release_value(signal);
	return undefined;
}

static jerry_value_t AbortSignalStaticAbortHandler(CALL_INFO) {
	jerry_value_t signal = createAbortSignal(true);
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		setReadonly(signal, "reason", args[0]);
	}
	else {
		jerry_value_t exception = createDOMException("signal is aborted without reason", "AbortError");
		setReadonly(signal, "reason", exception);
		jerry_release_value(exception);
	}
	return signal;
}

static jerry_value_t AbortSignalStaticTimeoutHandler(CALL_INFO) {
	if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'timeout': 1 argument required.");
	jerry_value_t signal = createAbortSignal(false);
	setReadonly(signal, "reason", undefined);
	jerry_value_t ms = jerry_value_to_number(args[0]);
	jerry_release_value(addTimeout(ref_task_abortSignalTimeout, ms, &signal, 1, false, true));
	jerry_release_value(ms);
	return signal;
}

static jerry_value_t abortSignalTimeoutTask(CALL_INFO) {
	jerry_value_t abortedStr = createString("aborted");
	jerry_value_t abortedVal = jerry_get_internal_property(args[0], abortedStr);
	if (!jerry_get_boolean_value(abortedVal)) {
		abortSignalRunAlgorithms(args[0]);

		jerry_value_t exception = createDOMException("signal timed out", "TimeoutError");
		setInternalProperty(args[0], "reason", exception);
		jerry_release_value(exception);

		jerry_value_t abortStr = createString("abort");
		jerry_value_t abortEvent = jerry_construct_object(ref_Event, &abortStr, 1);
		jerry_release_value(abortStr);

		setInternalProperty(abortEvent, "isTrusted", True);

		dispatchEvent(args[0], abortEvent, true);
		jerry_release_value(abortEvent);

		jerry_set_internal_property(args[0], abortedStr, True);
	}
	jerry_release_value(abortedVal);
	jerry_release_value(abortedStr);
	return undefined;
}

static jerry_value_t AbortSignalThrowIfAbortedHandler(CALL_INFO) {
	jerry_value_t abortedVal = getInternalProperty(thisValue, "aborted");
	bool aborted = jerry_get_boolean_value(abortedVal);
	jerry_release_value(abortedVal);
	if (aborted) return jerry_create_error_from_value(getInternalProperty(thisValue, "reason"), true);
	else return undefined;
}

jerry_value_t createStorage() {
	jerry_value_t storage = jerry_create_object();
	jerry_value_t Storage = getProperty(ref_global, "Storage");
	jerry_value_t StoragePrototype = jerry_get_property(Storage, ref_str_prototype);
	jerry_release_value(jerry_set_prototype(storage, StoragePrototype));
	jerry_release_value(StoragePrototype);
	jerry_release_value(Storage);
	jerry_value_t proxy = jerry_create_proxy(storage, ref_proxyHandler_storage);
	setInternalProperty(storage, "proxy", proxy);
	jerry_release_value(storage);
	jerry_value_t zero = jerry_create_number(0);
	setInternalProperty(proxy, "size", zero);
	jerry_release_value(zero);
	return proxy;
}

static jerry_value_t StorageLengthGetter(CALL_INFO) {
	jerry_value_t propNames = jerry_get_object_keys(thisValue);
	u32 length = jerry_get_array_length(propNames);
	jerry_release_value(propNames);
	return jerry_create_number(length);
}

static jerry_value_t StorageKeyHandler(CALL_INFO) {
	if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'key': 1 argument required.");
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
	if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'getItem': 1 argument required.");
	jerry_value_t hasOwnVal = jerry_has_own_property(thisValue, args[0]);
	bool hasOwn = jerry_get_boolean_value(hasOwnVal);
	jerry_release_value(hasOwnVal);
	if (hasOwn) return jerry_get_property(thisValue, args[0]);
	else return null;
}

static jerry_value_t StorageSetItemHandler(CALL_INFO) {
	if (argCount < 2) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'setItem': 2 arguments required.");

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
		result = throwDOMException("Failed to execute 'setItem': Exceeded the quota.", "QuotaExceededError");
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
		if (jerry_get_boolean_value(isLocal)) localStorageShouldSave = true;
		jerry_release_value(isLocal);
	}

	free(propertyName);
	jerry_release_value(valAsString);
	jerry_release_value(propertyAsString);
	return result;
}

static jerry_value_t StorageRemoveItemHandler(CALL_INFO) {
	if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'removeItem': 1 argument required.");
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
			if (jerry_get_boolean_value(isLocal)) localStorageShouldSave = true;
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
	if (jerry_get_boolean_value(isLocal)) localStorageShouldSave = true;
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
		result = throwDOMException("Failed to execute 'setItem': Exceeded the quota.", "QuotaExceededError");
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
			if (jerry_get_boolean_value(isLocal)) localStorageShouldSave = true;
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
			if (jerry_get_boolean_value(isLocal)) localStorageShouldSave = true;
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

static jerry_value_t TextDecoderConstructor(CALL_INFO) {
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor TextDecoder cannot be invoked without 'new'");

	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		char *str = getAsString(args[0]);
		if (strcmp(str, "utf-8") != 0 && strcmp(str, "utf8") != 0) {
			free(str);
			return jerry_create_error(JERRY_ERROR_RANGE, (jerry_char_t *) "Failed to construct 'TextDecoder': The encoding label provided is invalid.");
		}
		free(str);
	}

	jerry_value_t encoding = createString("utf-8");
	setReadonly(thisValue, "encoding", encoding);
	jerry_release_value(encoding);

	jerry_value_t fatalStr = createString("fatal");
	jerry_value_t ignoreBOMStr = createString("ignoreBOM");
	setReadonlyJV(thisValue, fatalStr, False);
	setReadonlyJV(thisValue, ignoreBOMStr, False);

	setInternalProperty(thisValue, "BOMSeen", False);
	setInternalProperty(thisValue, "doNotFlush", False);

	if (argCount > 1 && !jerry_value_is_undefined(args[1])) {
		if (!jerry_value_is_object(args[1])) {
			jerry_release_value(fatalStr);
			jerry_release_value(ignoreBOMStr);
			return jerry_create_error(JERRY_ERROR_RANGE, (jerry_char_t *) "Failed to construct 'TextDecoder': The provided value is not of type 'TextDecoderOptions'.");
		}
		jerry_value_t fatalVal = jerry_get_property(args[1], fatalStr);
		jerry_set_internal_property(thisValue, fatalStr, jerry_value_to_boolean(fatalVal) ? True : False);
		jerry_release_value(fatalVal);
		jerry_value_t ignoreBOMVal = jerry_get_property(args[1], ignoreBOMStr);
		jerry_set_internal_property(thisValue, ignoreBOMStr, jerry_value_to_boolean(ignoreBOMVal) ? True : False);
		jerry_release_value(ignoreBOMVal);
	}

	jerry_release_value(fatalStr);
	jerry_release_value(ignoreBOMStr);

	return undefined;
}

static jerry_value_t TextDecoderDecodeHandler(CALL_INFO) {
	jerry_value_t doNotFlushStr = createString("doNotFlush");
	jerry_value_t doNotFlushVal = jerry_get_internal_property(thisValue, doNotFlushStr);
	if (jerry_get_boolean_value(doNotFlushVal) == false) {
		setInternalProperty(thisValue, "hasPrevIO", False);
		setInternalProperty(thisValue, "BOMSeen", False);
	}
	jerry_release_value(doNotFlushVal);

	bool doNotFlush = false;
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) {
		if (jerry_value_is_object(args[1])) {
			jerry_value_t streamVal = getProperty(args[1], "stream");
			doNotFlush = jerry_get_boolean_value(streamVal);
			jerry_set_internal_property(thisValue, doNotFlushStr, doNotFlush ? True : False);
			jerry_release_value(streamVal);
		}
		else {
			jerry_release_value(doNotFlushStr);
			return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'decode': The provided value is not of type 'TextDecodeOptions'.");
		}
	}
	jerry_release_value(doNotFlushStr);

	u8 *source = NULL;
	u32 sourceLen = 0;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		if (jerry_value_is_typedarray(args[0])) {
			u32 byteOffset = 0;
			jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &byteOffset, &sourceLen);
			source = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
			jerry_release_value(arrayBuffer);
		}
		else if (jerry_value_is_arraybuffer(args[0])) {
			source = jerry_get_arraybuffer_pointer(args[0]);
			sourceLen = jerry_get_arraybuffer_byte_length(args[0]);
		}
		else return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'decode': The provided value is not of type '(ArrayBuffer or ArrayBufferView)'.");
	}

	jerry_value_t fatal = getInternalProperty(thisValue, "fatal");
	bool isFatal = jerry_get_boolean_value(fatal);
	jerry_release_value(fatal);

	/*
	Time to decode UTF-8 into... UTF-8.
	Reason for this is that the methods for creating strings provided by
	JerryScript accept two formats: CESU-8 and UTF-8. There's no reason to
	convert to raw Unicode when I'd just have to convert it to one of those
	two anyway. I believe JerryScript is using CESU-8 internally. I have
	the option to either convert to CESU-8 myself or just let JerryScript's
	internal function do it for me. In any case, I still have to do my own
	"decode" to handle error behavior to match the spec (and since Jerry
	does its own string validation, this unfortunately means that they are
	being checked twice).
	*/
	
	u32 i = 0;
	u32 sequence = 0;
	u8 bytesNeeded = 0;
	u8 bytesSeen = 0;
	u32 lowerBound = 0x80;
	u32 upperBound = 0xBF;

	jerry_value_t hasPrevIO = getInternalProperty(thisValue, "hasPrevIO");
	if (jerry_get_boolean_value(hasPrevIO)) {
		jerry_value_t sequenceNum = getInternalProperty(thisValue, "seq");
		sequence = jerry_value_as_uint32(sequenceNum);
		jerry_release_value(sequenceNum);
		jerry_value_t needed_seen = getInternalProperty(thisValue, "ns");
		u32 needed_seen_u32 = jerry_value_as_uint32(needed_seen);
		bytesNeeded = needed_seen_u32 >> 8;
		bytesSeen = needed_seen_u32 & 0xFF;
		jerry_release_value(needed_seen);
		jerry_value_t lowerBoundNum = getInternalProperty(thisValue, "lb");
		lowerBound = jerry_value_as_uint32(lowerBoundNum);
		jerry_release_value(lowerBoundNum);
		jerry_value_t upperBoundNum = getInternalProperty(thisValue, "ub");
		upperBound = jerry_value_as_uint32(upperBoundNum);
		jerry_release_value(upperBoundNum);
	}
	jerry_release_value(hasPrevIO);

	std::vector<u8> out;
	char errorMsg[31] = "The encoded data is not valid.";

	while (true) {
		if (i >= sourceLen) {
			if (bytesNeeded > 0 && !doNotFlush) {
				bytesNeeded = 0;
				if (isFatal) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) errorMsg);
				else {
					out.emplace_back(0xEF);
					out.emplace_back(0xBF);
					out.emplace_back(0xBD);
				}
			}
			break;
		}
		u8 byte = source[i];
		if (bytesNeeded == 0) {
			if (byte <= 0x7F) {
				out.emplace_back(byte);
			}
			else if (byte >= 0xC2 && byte <= 0xDF) {
				bytesNeeded = 1;
				sequence = sequence << 8 | byte;
			}
			else if (byte >= 0xE0 && byte <= 0xEF) {
				if (byte == 0xE0) lowerBound = 0xA0;
				if (byte == 0xED) upperBound = 0x9F;
				bytesNeeded = 2;
				sequence = sequence << 8 | byte;
			}
			else if (byte >= 0xF0 && byte <= 0xF4) {
				if (byte == 0xF0) lowerBound = 0x90;
				if (byte == 0xF4) upperBound = 0x8F;
				bytesNeeded = 3;
				sequence = sequence << 8 | byte;
			}
			else {
				if (isFatal) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) errorMsg);
				else {
					out.emplace_back(0xEF);
					out.emplace_back(0xBF);
					out.emplace_back(0xBD);
				}
			}
		}
		else if (byte < lowerBound || byte > upperBound) {
			sequence = bytesNeeded = bytesSeen = 0;
			lowerBound = 0x80;
			upperBound = 0xBF;
			if (isFatal) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) errorMsg);
			else {
				out.emplace_back(0xEF);
				out.emplace_back(0xBF);
				out.emplace_back(0xBD);
			}
		}
		else {
			lowerBound = 0x80;
			upperBound = 0xBF;
			sequence = sequence << 8 | byte;
			if (++bytesSeen == bytesNeeded) {
				if (bytesNeeded == 3) out.emplace_back(sequence >> 24);
				if (bytesNeeded >= 2) out.emplace_back(sequence >> 16 & 0xFF);
				out.emplace_back(sequence >> 8 & 0xFF);
				out.emplace_back(sequence & 0xFF);
				sequence = bytesNeeded = bytesSeen = 0;
			}
		}
		i++;
	}

	if (doNotFlush) {
		jerry_value_t sequenceNum = jerry_create_number(sequence);
		jerry_value_t needed_seen = jerry_create_number(bytesNeeded << 8 | bytesSeen);
		jerry_value_t lowerBoundNum = jerry_create_number(lowerBound);
		jerry_value_t upperBoundNum = jerry_create_number(upperBound);
		setInternalProperty(thisValue, "seq", sequenceNum);
		setInternalProperty(thisValue, "ns", needed_seen);
		setInternalProperty(thisValue, "lb", lowerBoundNum);
		setInternalProperty(thisValue, "ub", upperBoundNum);
		setInternalProperty(thisValue, "hasPrevIO", True);
		jerry_release_value(sequenceNum);
		jerry_release_value(needed_seen);
		jerry_release_value(lowerBoundNum);
		jerry_release_value(upperBoundNum);
	}

	u8 *data = out.data();
	u32 size = out.size();

	if (size > 2) {
		jerry_value_t ignoreBOM = getInternalProperty(thisValue, "ignoreBOM");
		if (jerry_get_boolean_value(ignoreBOM) == false) {
			jerry_value_t BOMSeen = getInternalProperty(thisValue, "BOMSeen");
			if (jerry_get_boolean_value(BOMSeen) == false) {
				if (data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
					data += 3;
					size -= 3;
					setInternalProperty(thisValue, "BOMSeen", True);
				}
			}
			jerry_release_value(BOMSeen);
		}
		jerry_release_value(ignoreBOM);
	}

	return jerry_create_string_sz_from_utf8(data, size);
}

static jerry_value_t TextEncoderConstructor(CALL_INFO) {
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor TextEncoder cannot be invoked without 'new'");

	jerry_value_t encoding = createString("utf-8");
	setReadonly(thisValue, "encoding", encoding);
	jerry_release_value(encoding);

	return undefined;
}

static jerry_value_t TextEncoderEncodeHandler(CALL_INFO) {
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		jerry_value_t stringVal = jerry_value_to_string(args[0]);
		jerry_length_t size = jerry_get_string_size(stringVal);
		jerry_value_t arrayBuffer = jerry_create_arraybuffer(size);
		u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer);
		jerry_string_to_utf8_char_buffer(stringVal, data, size);
		jerry_value_t typedArray = jerry_create_typedarray_for_arraybuffer(JERRY_TYPEDARRAY_UINT8, arrayBuffer);
		jerry_release_value(stringVal);
		jerry_release_value(arrayBuffer);
		return typedArray;
	}
	else return jerry_create_typedarray(JERRY_TYPEDARRAY_UINT8, 0);
}

static jerry_value_t TextEncoderEncodeIntoHandler(CALL_INFO) {
	if (argCount < 2) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'encodeInto': 2 arguments required.");
	if (jerry_value_is_object(args[1]) == false || jerry_value_is_typedarray(args[1]) == false || jerry_get_typedarray_type(args[1]) != JERRY_TYPEDARRAY_UINT8) {
		return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'encodeInto': parameter 2 is not of type 'Uint8Array'.");
	}
	jerry_value_t stringVal = jerry_value_to_string(args[0]);
	u32 stringSize = jerry_get_utf8_string_size(stringVal);
	u32 stringLen = jerry_get_string_length(stringVal);
	u32 offset = 0, bufferSize = 0;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[1], &offset, &bufferSize);
	u8 *dest = jerry_get_arraybuffer_pointer(arrayBuffer) + offset;
	u32 written = jerry_string_to_utf8_char_buffer(stringVal, dest, bufferSize);
	jerry_release_value(arrayBuffer);

	u8 *copy = NULL;
	// If the destination buffer was too small, the first string copy will fail.
	// In that case, dump a copy of the string to perform the writes manually
	if (written == 0 && stringLen > 0) {
		copy = (u8 *) malloc(stringSize);
		jerry_string_to_utf8_char_buffer(stringVal, copy, stringSize);
	}
	jerry_release_value(stringVal);

	u32 read = 0;
	u32 i = 0;
	u32 validSize = stringSize < bufferSize ? stringSize : bufferSize;
	u8 byte;
	u8 *source = copy == NULL ? dest : copy;
	while (i < validSize && read < stringLen) {
		byte = source[i];

		u8 byteCt = 0; // byte size of the incoming codepoint
		u8 lenCt = 0;  // number of UTF-16 codepoints used by incoming codepoint (1 or 2)

		if (byte >= 0xF0 && byte <= 0xF4) { // greater than U+FFFF
			lenCt = 2;
			byteCt = 4;
		}
		else {
			lenCt = 1;
			if (byte <= 0x7F) byteCt = 1;
			else if (byte >= 0xC2 && byte <= 0xDF) byteCt = 2;
			else if (byte >= 0xE0 && byte <= 0xEF) {
				byteCt = 3;
				// replace surrogates with the replacement char
				u16 codepoint = (byte & 0xF) << 12 | (source[i+1] & 0b111111) << 6 | (source[i+2] & 0b111111);
				if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
					source[i] = 0xEF;
					source[i+1] = 0xBF;
					source[i+2] = 0xBD;
				}
			}
			else break;
		}

		if (i + byteCt <= validSize && read + lenCt <= stringLen) {
			read += lenCt;
			if (copy != NULL) written += byteCt;
		}
		i += byteCt;
	}

	if (copy != NULL) {
		memcpy(dest, copy, written);
		free(copy);
	}

	jerry_value_t returnVal = jerry_create_object();
	jerry_value_t readVal = jerry_create_number(read);
	setProperty(returnVal, "read", readVal);
	jerry_release_value(readVal);
	jerry_value_t writtenVal = jerry_create_number(written);
	setProperty(returnVal, "written", writtenVal);
	jerry_release_value(writtenVal);
	return returnVal;
}

static jerry_value_t IllegalConstructor(CALL_INFO) {
	return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Illegal constructor");
}

void exposeAPI() {
	// hold some internal references
	ref_str_name = createString("name");
	ref_str_constructor = createString("constructor");
	ref_str_prototype = createString("prototype");
	ref_str_backtrace = createString("backtrace");
	ref_sym_toStringTag = jerry_get_well_known_symbol(JERRY_SYMBOL_TO_STRING_TAG);
	ref_task_abortSignalTimeout = jerry_create_external_function(abortSignalTimeoutTask);
	ref_consoleCounters = jerry_create_object();
	ref_consoleTimers = jerry_create_object();

	ref_global = jerry_get_global_object();
	setProperty(ref_global, "self", ref_global);

	setMethod(ref_global, "alert", alertHandler);
	setMethod(ref_global, "atob", atobHandler);
	setMethod(ref_global, "btoa", btoaHandler);
	setMethod(ref_global, "clearInterval", clearTimeoutHandler);
	setMethod(ref_global, "clearTimeout", clearTimeoutHandler);
	setMethod(ref_global, "close", closeHandler);
	setMethod(ref_global, "confirm", confirmHandler);
	setMethod(ref_global, "prompt", promptHandler);
	setMethod(ref_global, "queueMicrotask", queueMicrotaskHandler);
	ref_task_reportError = createMethod(ref_global, "reportError", reportErrorHandler);
	setMethod(ref_global, "setInterval", setIntervalHandler);
	setMethod(ref_global, "setTimeout", setTimeoutHandler);
	
	jerry_value_t console = jerry_create_object();
	setProperty(ref_global, "console", console);
	setMethod(console, "assert", consoleAssertHandler);
	setMethod(console, "clear", consoleClearHandler);
	setMethod(console, "count", consoleCountHandler);
	setMethod(console, "countReset", consoleCountResetHandler);
	setMethod(console, "debug", consoleDebugHandler);
	setMethod(console, "dir", consoleDirHandler);
	setMethod(console, "dirxml", consoleDirxmlHandler);
	setMethod(console, "error", consoleErrorHandler);
	setMethod(console, "group", consoleGroupHandler);
	setMethod(console, "groupCollapsed", consoleGroupHandler);
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

	jsClass EventTarget = createClass(ref_global, "EventTarget", EventTargetConstructor);
	setMethod(EventTarget.prototype, "addEventListener", EventTargetAddEventListenerHandler);
	setMethod(EventTarget.prototype, "removeEventListener", EventTargetRemoveEventListenerHandler);
	setMethod(EventTarget.prototype, "dispatchEvent", EventTargetDispatchEventHandler);
	// turn global into an EventTarget
	jerry_release_value(jerry_set_prototype(ref_global, EventTarget.prototype));
	jerry_value_t globalListeners = jerry_create_array(0);
	setInternalProperty(ref_global, "eventListeners", globalListeners);
	jerry_release_value(globalListeners);
	jerry_release_value(EventTarget.constructor);

	ref_Error = getProperty(ref_global, "Error");
	jerry_value_t ErrorPrototype = jerry_get_property(ref_Error, ref_str_prototype);
	jsClass DOMException = extendClass(ref_global, "DOMException", DOMExceptionConstructor, ErrorPrototype);
	ref_DOMException = DOMException.constructor;
	jerry_release_value(DOMException.prototype);
	jerry_release_value(ErrorPrototype);

	jsClass Event = createClass(ref_global, "Event", EventConstructor);
	classDefGetter(Event, "NONE",            EventNONEGetter);
	classDefGetter(Event, "CAPTURING_PHASE", EventCAPTURING_PHASEGetter);
	classDefGetter(Event, "AT_TARGET",       EventAT_TARGETGetter);
	classDefGetter(Event, "BUBBLING_PHASE",  EventBUBBLING_PHASEGetter);
	setMethod(Event.prototype, "composedPath", EventComposedPathHandler);
	setMethod(Event.prototype, "stopPropagation", EventStopPropagationHandler);
	setMethod(Event.prototype, "stopImmediatePropagation", EventStopImmediatePropagationHandler);
	setMethod(Event.prototype, "preventDefault", EventPreventDefaultHandler);
	ref_Event = Event.constructor;

	releaseClass(extendClass(ref_global, "ErrorEvent", ErrorEventConstructor, Event.prototype));
	releaseClass(extendClass(ref_global, "PromiseRejectionEvent", PromiseRejectionEventConstructor, Event.prototype));
	releaseClass(extendClass(ref_global, "CustomEvent", CustomEventConstructor, Event.prototype));
	// new DS-related event constructors
	releaseClass(extendClass(ref_global, "ButtonEvent", ButtonEventConstructor, Event.prototype));
	releaseClass(extendClass(ref_global, "StylusEvent", StylusEventConstructor, Event.prototype));
	jerry_release_value(Event.prototype);

	jsClass AbortController = createClass(ref_global, "AbortController", AbortControllerConstructor);
	setMethod(AbortController.prototype, "abort", AbortControllerAbortHandler);
	releaseClass(AbortController);

	jsClass AbortSignal = extendClass(ref_global, "AbortSignal", IllegalConstructor, EventTarget.prototype);
	setMethod(AbortSignal.constructor, "abort", AbortSignalStaticAbortHandler);
	setMethod(AbortSignal.constructor, "timeout", AbortSignalStaticTimeoutHandler);
	setMethod(AbortSignal.prototype, "throwIfAborted", AbortSignalThrowIfAbortedHandler);
	releaseClass(AbortSignal);
	jerry_release_value(EventTarget.prototype);

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

	ref_localStorage = createStorage();
	setProperty(ref_global, "localStorage", ref_localStorage);
	setInternalProperty(ref_localStorage, "isLocal", True);

	jsClass TextDecoder = createClass(ref_global, "TextDecoder", TextDecoderConstructor);
	setMethod(TextDecoder.prototype, "decode", TextDecoderDecodeHandler);
	releaseClass(TextDecoder);

	jsClass TextEncoder = createClass(ref_global, "TextEncoder", TextEncoderConstructor);
	setMethod(TextEncoder.prototype, "encode", TextEncoderEncodeHandler);
	setMethod(TextEncoder.prototype, "encodeInto", TextEncoderEncodeIntoHandler);
	releaseClass(TextEncoder);

	defEventAttribute(ref_global, "onerror");
	defEventAttribute(ref_global, "onload");
	defEventAttribute(ref_global, "onunhandledrejection");
	defEventAttribute(ref_global, "onunload");
	// new DS-related events
	defEventAttribute(ref_global, "onbuttondown");
	defEventAttribute(ref_global, "onbuttonup");
	defEventAttribute(ref_global, "onsleep");
	defEventAttribute(ref_global, "onstylusdown");
	defEventAttribute(ref_global, "onstylusmove");
	defEventAttribute(ref_global, "onstylusup");
	defEventAttribute(ref_global, "onvblank");
	defEventAttribute(ref_global, "onwake");

	// DS namespace, where most custom functionality lives
	jerry_value_t DS = jerry_create_object();
	setProperty(ref_global, "DS", DS);

	jerry_value_t buttons = jerry_create_object();
	setProperty(DS, "buttons", buttons);
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

	jerry_value_t stylus = jerry_create_object();
	setProperty(DS, "stylus", stylus);
	defGetter(stylus, "touching", [](CALL_INFO) { return jerry_create_boolean(keysHeld() & KEY_TOUCH); });
	setMethod(stylus, "getPosition", [](CALL_INFO) -> jerry_value_t {
		if ((keysHeld() & KEY_TOUCH) == 0) return null;
		touchPosition pos; touchRead(&pos);
		jerry_value_t position = jerry_create_object();
		jerry_value_t x = jerry_create_number(pos.px);
		jerry_value_t y = jerry_create_number(pos.py);
		setProperty(position, "x", x);
		setProperty(position, "y", y);
		jerry_release_value(x);
		jerry_release_value(y);
		return position;
	});
	jerry_release_value(stylus);
	
	jerry_release_value(DS);
}

void releaseReferences() {
	jerry_release_value(ref_global);
	jerry_release_value(ref_localStorage);
	jerry_release_value(ref_Event);
	jerry_release_value(ref_Error);
	jerry_release_value(ref_DOMException);
	jerry_release_value(ref_task_reportError);
	jerry_release_value(ref_task_abortSignalTimeout);
	jerry_release_value(ref_str_name);
	jerry_release_value(ref_str_constructor);
	jerry_release_value(ref_str_prototype);
	jerry_release_value(ref_str_backtrace);
	jerry_release_value(ref_sym_toStringTag);
	jerry_release_value(ref_proxyHandler_storage);
	jerry_release_value(ref_consoleCounters);
	jerry_release_value(ref_consoleTimers);
}