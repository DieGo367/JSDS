#include "api.h"

#include <nds/arm9/input.h>
#include <nds/interrupts.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <unordered_map>

#include "console.h"
#include "execute.h"
#include "keyboard.h"
#include "inline.h"
#include "jerry/jerryscript.h"
#include "timeouts.h"



#define CALL_INFO const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], u32 argCount

static jerry_value_t closeHandler(CALL_INFO) {
	exit(0);
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
	return jerry_create_undefined();
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
		if (keys & KEY_A) return consoleClear(), jerry_create_boolean(true);
		else if (keys & KEY_B) return consoleClear(), jerry_create_boolean(false);
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
	if (canceled) return jerry_create_undefined();
	else return jerry_create_string_from_utf8((jerry_char_t *) keyboardBuffer());
}

static jerry_value_t atobHandler(CALL_INFO) {
	if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'atob': 1 argument required.");
	jerry_char_t *errorMsg = (jerry_char_t *) "Failed to decode base64.";

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
			return jerry_create_error(JERRY_ERROR_COMMON, errorMsg);
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
		return jerry_create_error(JERRY_ERROR_COMMON, errorMsg);
	}

	// (5-8) output, buffer, position and loop
	char *output = (char *) malloc((strippedSize + 1) * 2);
	char *out = output;
	int buffer = 0, bits = 0;
	for (char *ch = data; ch != dataEnd; ch++) {
		if (*ch == 64) { // (4 again) fail on equal sign not at end of string
			free(data);
			return jerry_create_error(JERRY_ERROR_COMMON, errorMsg);
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
				return jerry_create_error(JERRY_ERROR_COMMON, (jerry_char_t *) "Failed to execute 'btoa': The string to be encoded contains characters outside of the Latin1 range.");
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

	jerry_value_t result = jerry_create_string((jerry_char_t *) output);
	free(output);
	return result;
}

static jerry_value_t setTimeoutHandler(CALL_INFO) {
	if (argCount >= 2) return addTimeout(args[0], args[1], (jerry_value_t *)(args) + 2, argCount - 2, false);
	else {
		jerry_value_t undefined = jerry_create_undefined();
		jerry_value_t zero = jerry_create_number(0);
		jerry_value_t result = addTimeout(argCount > 0 ? args[0] : undefined, zero, NULL, 0, false);
		jerry_release_value(zero);
		jerry_release_value(undefined);
		return result;
	}
}

static jerry_value_t setIntervalHandler(CALL_INFO) {
	if (argCount >= 2) return addTimeout(args[0], args[1], (jerry_value_t *)(args) + 2, argCount - 2, true);
	else {
		jerry_value_t undefined = jerry_create_undefined();
		jerry_value_t zero = jerry_create_number(0);
		jerry_value_t result = addTimeout(argCount > 0 ? args[0] : undefined, zero, NULL, 0, true);
		jerry_release_value(zero);
		jerry_release_value(undefined);
		return result;
	}
}

static jerry_value_t clearTimeoutHandler(CALL_INFO) {
	if (argCount > 0) clearTimeout(args[0]);
	else {
		jerry_value_t undefined = jerry_create_undefined();
		clearTimeout(undefined);
		jerry_release_value(undefined);
	}
	return jerry_create_undefined();
}

int consoleGroups = 0;

static jerry_value_t consoleLogHandler(CALL_INFO) {
	if (argCount > 0) {
		for (int i = 0; i < consoleGroups; i++) putchar(' ');
		consolePrint(args, argCount);
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleInfoHandler(CALL_INFO) {
	if (argCount > 0) {
		u16 pal = mainConsole->fontCurPal;
		mainConsole->fontCurPal = ConsolePalette::AQUA;
		for (int i = 0; i < consoleGroups; i++) putchar(' ');
		consolePrint(args, argCount);
		mainConsole->fontCurPal = pal;
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleWarnHandler(CALL_INFO) {
	if (argCount > 0) {
		u16 pal = mainConsole->fontCurPal;
		mainConsole->fontCurPal = ConsolePalette::YELLOW;
		for (int i = 0; i < consoleGroups; i++) putchar(' ');
		consolePrint(args, argCount);
		mainConsole->fontCurPal = pal;
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleErrorHandler(CALL_INFO) {
	if (argCount > 0) {
		u16 pal = mainConsole->fontCurPal;
		mainConsole->fontCurPal = ConsolePalette::RED;
		for (int i = 0; i < consoleGroups; i++) putchar(' ');
		consolePrint(args, argCount);
		mainConsole->fontCurPal = pal;
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleAssertHandler(CALL_INFO) {
	if (argCount == 0 || !jerry_value_to_boolean(args[0])) {
		u16 pal = mainConsole->fontCurPal;
		mainConsole->fontCurPal = ConsolePalette::RED;
		for (int i = 0; i < consoleGroups; i++) putchar(' ');
		printf("Assertion failed: ");
		consolePrint(args + 1, argCount - 1);
		mainConsole->fontCurPal = pal;
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleDebugHandler(CALL_INFO) {
	if (argCount > 0) {
		u16 pal = mainConsole->fontCurPal;
		mainConsole->fontCurPal = ConsolePalette::NAVY;
		for (int i = 0; i < consoleGroups; i++) putchar(' ');
		consolePrint(args, argCount);
		mainConsole->fontCurPal = pal;
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleTraceHandler(CALL_INFO) {
	for (int j = 0; j < consoleGroups; j++) putchar(' ');
	if (argCount == 0) printf("Trace\n");
	else consolePrint(args, argCount);
	jerry_value_t backtrace = jerry_get_backtrace(10);
	u32 length = jerry_get_array_length(backtrace);
	for (u32 i = 0; i < length; i++) {
		jerry_value_t traceLine = jerry_get_property_by_index(backtrace, i);
		char *step = getString(traceLine);
		for (int j = 0; j < consoleGroups; j++) putchar(' ');
		printf(" @ %s\n", step);
		free(step);
		jerry_release_value(traceLine);
	}
	jerry_release_value(backtrace);
	return jerry_create_undefined();
}

static jerry_value_t consoleDirHandler(CALL_INFO) {
	if (argCount > 0) {
		for (int i = 0; i < consoleGroups; i++) putchar(' ');
		if (jerry_value_is_object(args[0])) consolePrintObject(args[0]);
		else consolePrintLiteral(args[0]);
		putchar('\n');
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleDirxmlHandler(CALL_INFO) {
	if (argCount > 0) {
		for (int i = 0; i < consoleGroups; i++) putchar(' ');
		for (u32 i = 0; i < argCount; i++) {
			consolePrintLiteral(args[i]);
			if (i < argCount - 1) putchar(' ');
		}
		putchar('\n');
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleTableHandler(CALL_INFO) {
	if (argCount > 0) consolePrintTable(args, argCount, consoleGroups);
	return jerry_create_undefined();
}

static jerry_value_t consoleGroupHandler(CALL_INFO) {
	consoleGroups++;
	return jerry_create_undefined();
}

static jerry_value_t consoleGroupEndHandler(CALL_INFO) {
	if (--consoleGroups < 0) consoleGroups = 0;
	return jerry_create_undefined();
}

std::unordered_map<std::string, int> consoleCounters;
static jerry_value_t consoleCountHandler(CALL_INFO) {
	for (int i = 0; i < consoleGroups; i++) putchar(' ');
	std::string label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		char *lbl = getAsString(args[0]);
		label = std::string(lbl);
		free(lbl);
	}
	else label = "default";
	if (consoleCounters.count(label) == 0) {
		printf("%s: %i\n", label.c_str(), 1);
		consoleCounters[label] = 1;
	}
	else {
		printf("%s: %i\n", label.c_str(), ++consoleCounters[label]);
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleCountResetHandler(CALL_INFO) {
	std::string label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		char *lbl = getAsString(args[0]);
		label = std::string(lbl);
		free(lbl);
	}
	else label = "default";
	if (consoleCounters.count(label) == 0) {
		for (int i = 0; i < consoleGroups; i++) putchar(' ');
		printf("Count for '%s' does not exist\n", label.c_str());
	}
	else {
		consoleCounters[label] = 0;
	}
	return jerry_create_undefined();
}

std::unordered_map<std::string, time_t> consoleTimers;
static jerry_value_t consoleTimeHandler(CALL_INFO) {
	std::string label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		char *lbl = getAsString(args[0]);
		label = std::string(lbl);
		free(lbl);
	}
	else label = "default";
	if (consoleTimers.count(label) == 0) {
		consoleTimers[label] = time(NULL);
	}
	else {
		for (int i = 0; i < consoleGroups; i++) putchar(' ');
		printf("Timer '%s' already exists\n", label.c_str());
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleTimeLogHandler(CALL_INFO) {
	for (int i = 0; i < consoleGroups; i++) putchar(' ');
	std::string label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		char *lbl = getAsString(args[0]);
		label = std::string(lbl);
		free(lbl);
	}
	else label = "default";
	if (consoleTimers.count(label) == 0) {
		printf("Timer '%s' does not exist\n", label.c_str());
	}
	else {
		double elapsed = difftime(time(NULL), consoleTimers[label]);
		printf("%s: %lg s\n", label.c_str(), elapsed);
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleTimeEndHandler(CALL_INFO) {
	for (int i = 0; i < consoleGroups; i++) putchar(' ');
	std::string label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		char *lbl = getAsString(args[0]);
		label = std::string(lbl);
		free(lbl);
	}
	else label = "default";
	if (consoleTimers.count(label) == 0) {
		printf("Timer '%s' does not exist\n", label.c_str());
	}
	else {
		double elapsed = difftime(time(NULL), consoleTimers[label]);
		consoleTimers.erase(label);
		printf("%s: %lg s\n", label.c_str(), elapsed);
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleClearHandler(CALL_INFO) {
	consoleClear();
	return jerry_create_undefined();
}

static jerry_value_t EventConstructor(CALL_INFO) {
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor Event cannot be invoked without 'new'");
	else if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to construct 'Event': 1 argument required.");
	setPropertyNonEnumerable(thisValue, "constructor", function);

	jerry_value_t True = jerry_create_boolean(true);
	jerry_value_t False = jerry_create_boolean(false);
	jerry_value_t null = jerry_create_null();
	jerry_value_t zero = jerry_create_number(0);
	setInternalProperty(thisValue, "initialized", True);               // initialized flag
	setInternalProperty(thisValue, "dispatch", False);                 // dispatch flag
	setInternalProperty(thisValue, "inPassiveListener", False);        // in passive listener flag
	setInternalProperty(thisValue, "stopPropagation", False);          // stop propagation flag
	setInternalProperty(thisValue, "stopImmediatePropagation", False); // stop immediate propagation flag
	setInternalProperty(thisValue, "target", null);
	setInternalProperty(thisValue, "currentTarget", null);
	setInternalProperty(thisValue, "eventPhase", zero);
	setInternalProperty(thisValue, "bubbles", False);
	setInternalProperty(thisValue, "cancelable", False);
	setInternalProperty(thisValue, "defaultPrevented", False);         // canceled flag
	setInternalProperty(thisValue, "composed", False);                 // composed flag
	setInternalProperty(thisValue, "isTrusted", False);
	jerry_value_t currentTime = jerry_create_number(time(NULL));
	setInternalProperty(thisValue, "timeStamp", currentTime);
	jerry_release_value(currentTime);
	jerry_release_value(zero);
	jerry_release_value(null);
	jerry_release_value(False);
	jerry_release_value(True);

	jerry_value_t typeAsString = jerry_value_to_string(args[0]);	
	setInternalProperty(thisValue, "type", typeAsString);
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

	return jerry_create_undefined();
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
	jerry_value_t True = jerry_create_boolean(true);
	setInternalProperty(thisValue, "stopPropagation", True);
	jerry_release_value(True);
	return jerry_create_undefined();
}

static jerry_value_t EventStopImmediatePropagationHandler(CALL_INFO) {
	jerry_value_t True = jerry_create_boolean(true);
	setInternalProperty(thisValue, "stopPropagation", True);
	setInternalProperty(thisValue, "stopImmediatePropagation", True);
	jerry_release_value(True);
	return jerry_create_undefined();
}

static jerry_value_t EventPreventDefaultHandler(CALL_INFO) {
	jerry_value_t cancelable = getInternalProperty(thisValue, "cancelable");
	jerry_value_t inPassiveListener = getInternalProperty(thisValue, "inPassiveListener");
	if (jerry_value_to_boolean(cancelable) && !jerry_value_to_boolean(inPassiveListener)) {
		jerry_value_t True = jerry_create_boolean(true);
		setInternalProperty(thisValue, "defaultPrevented", True);
		jerry_release_value(True);
	}
	jerry_release_value(inPassiveListener);
	jerry_release_value(cancelable);
	return jerry_create_undefined();
}

static jerry_value_t EventTargetConstructor(CALL_INFO) {
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor EventTarget cannot be invoked without 'new'");
	setPropertyNonEnumerable(thisValue, "constructor", function);

	jerry_value_t eventListenerList = jerry_create_array(0);
	setInternalProperty(thisValue, "eventListeners", eventListenerList);
	jerry_release_value(eventListenerList);

	return jerry_create_undefined();
}

static jerry_value_t EventTargetAddEventListenerHandler(CALL_INFO) {
	if (argCount < 2) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'addEventListener': 2 arguments required.");
	if (jerry_value_is_null(args[1])) return jerry_create_undefined();
	jerry_value_t target = jerry_value_is_undefined(thisValue) ? jerry_get_global_object() : jerry_acquire_value(thisValue);
	
	jerry_value_t typeStr = jerry_create_string((jerry_char_t *) "type");
	jerry_value_t callbackStr = jerry_create_string((jerry_char_t *) "callback");
	jerry_value_t captureStr = jerry_create_string((jerry_char_t *) "capture");
	jerry_value_t onceStr = jerry_create_string((jerry_char_t *) "once");
	jerry_value_t passiveStr = jerry_create_string((jerry_char_t *) "passive");

	jerry_value_t typeVal = jerry_value_to_string(args[0]);
	jerry_value_t callbackVal = args[1];
	bool capture = false;
	bool once = false;
	bool passive = false;
	// TODO: signal
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
			
			// TODO: signal = AbortSignal | null
		}
		else capture = jerry_value_to_boolean(args[2]);
	}

	jerry_value_t eventListeners = getInternalProperty(target, "eventListeners");
	u32 length = jerry_get_array_length(eventListeners);
	bool shouldAppend = true;
	for (u32 i = 0; shouldAppend && i < length; i++) {
		jerry_value_t storedListener = jerry_get_property_by_index(eventListeners, i);
		jerry_value_t storedType = jerry_get_property(storedListener, typeStr);
		jerry_value_t storedCallback = jerry_get_property(storedListener, callbackStr);
		jerry_value_t storedCapture = jerry_get_property(storedListener, captureStr);
		jerry_value_t typeEquality = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, typeVal, storedType);
		jerry_value_t callbackEquality = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, callbackVal, storedCallback);
		bool captureEquality = capture == jerry_get_boolean_value(storedCapture);
		if (jerry_get_boolean_value(typeEquality) && jerry_get_boolean_value(callbackEquality) && captureEquality) {
			shouldAppend = false;
		}
		jerry_release_value(callbackEquality);
		jerry_release_value(typeEquality);
		jerry_release_value(storedCapture);
		jerry_release_value(storedCallback);
		jerry_release_value(storedType);
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
		jerry_value_t null = jerry_create_null();
		setProperty(listener, "signal", null);
		jerry_release_value(null);
		jerry_value_t False = jerry_create_boolean(false);
		setProperty(listener, "removed", False);
		jerry_release_value(False);

		jerry_value_t pushFunc = getProperty(eventListeners, "push");
		jerry_release_value(jerry_call_function(pushFunc, eventListeners, &listener, 1));
		jerry_release_value(pushFunc);

		jerry_release_value(listener);
	}

	jerry_release_value(eventListeners);
	jerry_release_value(typeVal);
	jerry_release_value(passiveStr);
	jerry_release_value(onceStr);
	jerry_release_value(captureStr);
	jerry_release_value(callbackStr);
	jerry_release_value(typeStr);
	jerry_release_value(target);

	return jerry_create_undefined();
}

static jerry_value_t EventTargetRemoveEventListenerHandler(CALL_INFO) {
	if (argCount < 2) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'removeEventListener': 2 arguments required.");
	jerry_value_t target = jerry_value_is_undefined(thisValue) ? jerry_get_global_object() : jerry_acquire_value(thisValue);
	
	jerry_value_t typeStr = jerry_create_string((jerry_char_t *) "type");
	jerry_value_t callbackStr = jerry_create_string((jerry_char_t *) "callback");
	jerry_value_t captureStr = jerry_create_string((jerry_char_t *) "capture");

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
	u32 length = jerry_get_array_length(eventListeners);
	bool removed = false;
	for (u32 i = 0; !removed && i < length; i++) {
		jerry_value_t storedListener = jerry_get_property_by_index(eventListeners, i);
		jerry_value_t storedType = jerry_get_property(storedListener, typeStr);
		jerry_value_t storedCallback = jerry_get_property(storedListener, callbackStr);
		jerry_value_t storedCapture = jerry_get_property(storedListener, captureStr);
		jerry_value_t typeEquality = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, typeVal, storedType);
		jerry_value_t callbackEquality = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, callbackVal, storedCallback);
		bool captureEquality = capture == jerry_get_boolean_value(storedCapture);
		if (jerry_get_boolean_value(typeEquality) && jerry_get_boolean_value(callbackEquality) && captureEquality) {
			jerry_value_t spliceFunc = getProperty(eventListeners, "splice");
			jerry_value_t spliceArgs[2] = {jerry_create_number(i), jerry_create_number(1)};
			jerry_release_value(jerry_call_function(spliceFunc, eventListeners, spliceArgs, 2));
			jerry_release_value(spliceArgs[1]);
			jerry_release_value(spliceArgs[0]);
			jerry_release_value(spliceFunc);
			jerry_value_t True = jerry_create_boolean(true);
			setProperty(storedListener, "removed", True);
			jerry_release_value(True);
			removed = true;
		}
		jerry_release_value(callbackEquality);
		jerry_release_value(typeEquality);
		jerry_release_value(storedCapture);
		jerry_release_value(storedCallback);
		jerry_release_value(storedType);
		jerry_release_value(storedListener);
	}
	
	jerry_release_value(eventListeners);
	jerry_release_value(typeVal);
	jerry_release_value(captureStr);
	jerry_release_value(callbackStr);
	jerry_release_value(typeStr);
	jerry_release_value(target);

	return jerry_create_undefined();
}

static jerry_value_t EventTargetDispatchEventHandler(CALL_INFO) {
	if (argCount < 1) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'dispatchEvent': 1 argument required.");
	jerry_value_t target = jerry_value_is_undefined(thisValue) ? jerry_get_global_object() : jerry_acquire_value(thisValue);
	jerry_value_t Event = jerry_eval((jerry_char_t *) "Event", 5, JERRY_PARSE_NO_OPTS);
	jerry_value_t isInstanceVal = jerry_binary_operation(JERRY_BIN_OP_INSTANCEOF, args[0], Event);
	bool isInstance = jerry_get_boolean_value(isInstanceVal);
	jerry_release_value(isInstanceVal);
	jerry_release_value(Event);
	if (!isInstance) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Not an instance of Event.");

	jerry_value_t dispatchStr = jerry_create_string((jerry_char_t *) "dispatch");
	jerry_value_t dispatchVal = jerry_get_internal_property(args[0], dispatchStr);
	bool dispatched = jerry_get_boolean_value(dispatchVal);
	jerry_release_value(dispatchVal);
	if (dispatched) {
		jerry_release_value(dispatchStr);
		return jerry_create_error(JERRY_ERROR_COMMON, (jerry_char_t *) "Invalid event state");
	}

	jerry_value_t eventPhaseStr = jerry_create_string((jerry_char_t *) "eventPhase");
	jerry_value_t targetStr = jerry_create_string((jerry_char_t *) "target");
	jerry_value_t currentTargetStr = jerry_create_string((jerry_char_t *) "currentTarget");
	jerry_value_t True = jerry_create_boolean(true);
	jerry_value_t False = jerry_create_boolean(false);

	jerry_set_internal_property(args[0], dispatchStr, True);

	jerry_value_t AT_TARGET = jerry_create_number(2);
	jerry_set_internal_property(args[0], eventPhaseStr, AT_TARGET);
	jerry_release_value(AT_TARGET);

	jerry_set_internal_property(args[0], targetStr, target);
	jerry_set_internal_property(args[0], currentTargetStr, target);

	jerry_value_t listeners = getInternalProperty(target, "eventListeners");
	jerry_value_t sliceFunc = getProperty(listeners, "slice");
	jerry_value_t listenersCopy = jerry_call_function(sliceFunc, listeners, NULL, 0);
	jerry_release_value(sliceFunc);

	u32 length = jerry_get_array_length(listenersCopy);
	jerry_value_t removedStr = jerry_create_string((jerry_char_t *) "removed");
	jerry_value_t typeStr = jerry_create_string((jerry_char_t *) "type");
	jerry_value_t onceStr = jerry_create_string((jerry_char_t *) "once");
	jerry_value_t passiveStr = jerry_create_string((jerry_char_t *) "passive");
	jerry_value_t callbackStr = jerry_create_string((jerry_char_t *) "callback");
	jerry_value_t eventType = jerry_get_internal_property(args[0], typeStr);
	jerry_value_t spliceFunc = getProperty(listeners, "splice");
	jerry_value_t inPassiveListenerStr = jerry_create_string((jerry_char_t *) "inPassiveListener");
	jerry_value_t handleEventStr = jerry_create_string((jerry_char_t *) "handleEvent");
	for (u32 i = 0; i < length; i++) {
		jerry_value_t listener = jerry_get_property_by_index(listenersCopy, i);
		jerry_value_t removedVal = jerry_get_property(listener, removedStr);
		bool removed = jerry_get_boolean_value(removedVal);
		jerry_release_value(removedVal);
		if (!removed) {
			jerry_value_t listenerType = jerry_get_property(listener, typeStr);
			jerry_value_t typesMatchVal = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, eventType, listenerType);
			bool typesMatch = jerry_get_boolean_value(typesMatchVal);
			jerry_release_value(typesMatchVal);
			jerry_release_value(listenerType);
			if (typesMatch) {
				jerry_value_t onceVal = jerry_get_property(listener, onceStr);
				bool once = jerry_get_boolean_value(onceVal);
				jerry_release_value(onceVal);
				if (once) {
					jerry_value_t spliceArgs[2] = {jerry_create_number(i), jerry_create_number(1)};
					jerry_release_value(jerry_call_function(spliceFunc, listeners, spliceArgs, 2));
					jerry_release_value(spliceArgs[1]);
					jerry_release_value(spliceArgs[0]);
					jerry_release_value(jerry_set_property(listener, removedStr, True));
				}
				jerry_value_t passiveVal = jerry_get_property(listener, passiveStr);
				bool passive = jerry_get_boolean_value(passiveVal);
				jerry_release_value(passiveVal);
				if (passive) jerry_set_internal_property(args[0], inPassiveListenerStr, True);
				
				jerry_value_t callbackVal = jerry_get_property(listener, callbackStr);
				if (jerry_value_is_function(callbackVal)) {
					jerry_value_t result = jerry_call_function(callbackVal, target, args, 1);
					if (jerry_value_is_error(result)) handleError(result);
					jerry_release_value(result);
				}
				else if (jerry_value_is_object(callbackVal)) {
					jerry_value_t handler = jerry_get_property(callbackVal, handleEventStr);
					if (jerry_value_is_function(handler)) {
						jerry_value_t result = jerry_call_function(handler, target, args, 1);
						if (jerry_value_is_error(result)) handleError(result);
						jerry_release_value(result);
					}
					else jerry_release_value(handler);
				}
				jerry_release_value(callbackVal);

				jerry_set_internal_property(args[0], inPassiveListenerStr, False);
			}
		}
		jerry_release_value(listener);
	}
	jerry_release_value(handleEventStr);
	jerry_release_value(inPassiveListenerStr);
	jerry_release_value(spliceFunc);
	jerry_release_value(eventType);
	jerry_release_value(callbackStr);
	jerry_release_value(passiveStr);
	jerry_release_value(onceStr);
	jerry_release_value(typeStr);
	jerry_release_value(removedStr);
	jerry_release_value(listenersCopy);
	jerry_release_value(listeners);

	jerry_value_t NONE = jerry_create_number(0);
	jerry_set_internal_property(args[0], eventPhaseStr, NONE);
	jerry_release_value(NONE);
	jerry_value_t null = jerry_create_null();
	jerry_set_internal_property(args[0], targetStr, null);
	jerry_set_internal_property(args[0], currentTargetStr, null);
	jerry_release_value(null);
	jerry_set_internal_property(args[0], dispatchStr, False);
	setInternalProperty(args[0], "stopPropagation", False);
	setInternalProperty(args[0], "stopImmediatePropagation", False);
	
	jerry_release_value(False);
	jerry_release_value(True);
	jerry_release_value(currentTargetStr);
	jerry_release_value(targetStr);
	jerry_release_value(eventPhaseStr);
	jerry_release_value(dispatchStr);
	jerry_release_value(target);
	
	jerry_value_t canceledVal = getInternalProperty(args[0], "defaultPrevented");
	bool canceled = jerry_get_boolean_value(canceledVal);
	jerry_release_value(canceledVal);
	return jerry_create_boolean(!canceled);
}

static jerry_value_t ErrorEventConstructor(CALL_INFO) {
	jerry_value_t newTarget = jerry_get_new_target();
	bool targetUndefined = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	if (targetUndefined) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Constructor ErrorEvent cannot be invoked without 'new'");
	else if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to construct 'ErrorEvent': 1 argument required.");
	jerry_value_t undefined = EventConstructor(function, thisValue, args, argCount);

	jerry_value_t messageProp = jerry_create_string((jerry_char_t *) "message");
	jerry_value_t filenameProp = jerry_create_string((jerry_char_t *) "filename");
	jerry_value_t linenoProp = jerry_create_string((jerry_char_t *) "lineno");
	jerry_value_t colnoProp = jerry_create_string((jerry_char_t *) "colno");
	jerry_value_t emptyStr = jerry_create_string((jerry_char_t *) "");
	jerry_value_t zero = jerry_create_number(0);
	jerry_set_internal_property(thisValue, messageProp, emptyStr);
	jerry_set_internal_property(thisValue, filenameProp, emptyStr);
	jerry_set_internal_property(thisValue, linenoProp, zero);
	jerry_set_internal_property(thisValue, colnoProp, zero);
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
		jerry_value_t errorProp = jerry_create_string((jerry_char_t *) "error");
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

void exposeAPI() {
	nameValue = jerry_create_string((jerry_char_t *) "name");
	jerry_value_t global = jerry_get_global_object();

	setProperty(global, "self", global);
	jerry_value_t null = jerry_create_null();
	setProperty(global, "onload", null);
	jerry_release_value(null);

	setMethod(global, "alert", alertHandler);
	setMethod(global, "atob", atobHandler);
	setMethod(global, "btoa", btoaHandler);
	setMethod(global, "clearInterval", clearTimeoutHandler);
	setMethod(global, "clearTimeout", clearTimeoutHandler);
	setMethod(global, "close", closeHandler);
	setMethod(global, "confirm", confirmHandler);
	setMethod(global, "prompt", promptHandler);
	setMethod(global, "setInterval", setIntervalHandler);
	setMethod(global, "setTimeout", setTimeoutHandler);
	
	jerry_value_t console = jerry_create_object();
	setProperty(global, "console", console);
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

	jerry_value_t Event = newMethod(global, "Event", EventConstructor);
	defGetter(Event, "NONE",            EventNONEGetter);
	defGetter(Event, "CAPTURING_PHASE", EventCAPTURING_PHASEGetter);
	defGetter(Event, "AT_TARGET",       EventAT_TARGETGetter);
	defGetter(Event, "BUBBLING_PHASE",  EventBUBBLING_PHASEGetter);
	jerry_value_t EventProto = jerry_create_object();
	setProperty(Event, "prototype", EventProto);
	defReadonly(EventProto, "type");
	defReadonly(EventProto, "target");
	defReadonly(EventProto, "currentTarget");
	setMethod(EventProto, "composedPath", EventComposedPathHandler);
	defReadonly(EventProto, "eventPhase");
	defGetter(EventProto, "NONE",            EventNONEGetter);
	defGetter(EventProto, "CAPTURING_PHASE", EventCAPTURING_PHASEGetter);
	defGetter(EventProto, "AT_TARGET",       EventAT_TARGETGetter);
	defGetter(EventProto, "BUBBLING_PHASE",  EventBUBBLING_PHASEGetter);
	setMethod(EventProto, "stopPropagation", EventStopPropagationHandler);
	setMethod(EventProto, "stopImmediatePropagation", EventStopImmediatePropagationHandler);
	defReadonly(EventProto, "bubbles");
	defReadonly(EventProto, "cancelable");
	setMethod(EventProto, "preventDefault", EventPreventDefaultHandler);
	defReadonly(EventProto, "defaultPrevented");
	defReadonly(EventProto, "composed");
	defReadonly(EventProto, "isTrusted");
	defReadonly(EventProto, "timeStamp");
	jerry_release_value(Event);

	jerry_value_t EventTarget = newMethod(global, "EventTarget", EventTargetConstructor);
	jerry_value_t EventTargetProto = jerry_create_object();
	setProperty(EventTarget, "prototype", EventTargetProto);
	jerry_release_value(jerry_set_prototype(global, EventTargetProto));
	jerry_value_t globalListeners = jerry_create_array(0);
	setInternalProperty(global, "eventListeners", globalListeners);
	jerry_release_value(globalListeners);
	setMethod(EventTargetProto, "addEventListener", EventTargetAddEventListenerHandler);
	setMethod(EventTargetProto, "removeEventListener", EventTargetRemoveEventListenerHandler);
	setMethod(EventTargetProto, "dispatchEvent", EventTargetDispatchEventHandler);
	jerry_release_value(EventTargetProto);
	jerry_release_value(EventTarget);

	jerry_value_t ErrorEvent = newMethod(global, "ErrorEvent", ErrorEventConstructor);
	jerry_value_t ErrorEventProto = jerry_create_object();
	setProperty(ErrorEvent, "prototype", ErrorEventProto);
	jerry_release_value(jerry_set_prototype(ErrorEventProto, EventProto));
	defReadonly(ErrorEventProto, "message");
	defReadonly(ErrorEventProto, "filename");
	defReadonly(ErrorEventProto, "lineno");
	defReadonly(ErrorEventProto, "colno");
	defReadonly(ErrorEventProto, "error");
	jerry_release_value(ErrorEventProto);
	jerry_release_value(ErrorEvent);
	jerry_release_value(EventProto);

	jerry_release_value(global);
	jerry_release_value(nameValue);
}