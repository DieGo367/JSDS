#include <nds/ndstypes.h>
#include <nds/arm9/input.h>
#include <nds/interrupts.h>
#include <stdlib.h>

#include "event.hpp"
#include "io/console.hpp"
#include "io/keyboard.hpp"
#include "logging.hpp"
#include "util/color.hpp"
#include "util/helpers.hpp"
#include "util/timing.hpp"



jerry_value_t ref_consoleCounters;
jerry_value_t ref_consoleTimers;
bool pauseKeyEvents = false;

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



FUNCTION(alert) {
	if (argCount > 0) printValue(args[0]);
	else printf("Alert");
	printf(" [ OK]\n");
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_A) break;
	}
	return JS_UNDEFINED;
}

FUNCTION(confirm) {
	if (argCount > 0) printValue(args[0]);
	else printf("Confirm");
	printf(" [ OK,  Cancel]\n");
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		u32 keys = keysDown();
		if (keys & KEY_A) return JS_TRUE;
		else if (keys & KEY_B) return JS_FALSE;
	}
}

FUNCTION(prompt) {
	if (argCount > 0) printValue(args[0]);
	else printf("Prompt");
	putchar(' ');
	pauseKeyEvents = true;
	bool hadButtonControls = keyboardButtonControls(true);
	keyboardCompose(true);
	ComposeStatus status = keyboardComposeStatus();
	while (status == KEYBOARD_COMPOSING) {
		swiWaitForVBlank();
		scanKeys();
		keyboardUpdate();
		status = keyboardComposeStatus();
	}
	if (status == KEYBOARD_FINISHED) {
		char *response;
		u32 responseSize;
		keyboardComposeAccept(&response, &responseSize);
		printf(response); putchar('\n');
		jerry_value_t responseStr = StringSized(response, responseSize);
		free(response);
		keyboardUpdate();
		pauseKeyEvents = false;
		keyboardButtonControls(hadButtonControls);
		return responseStr;
	}
	else {
		putchar('\n');
		keyboardUpdate();
		pauseKeyEvents = false;
		keyboardButtonControls(hadButtonControls);
		if (argCount > 1) return jerry_value_to_string(args[1]);
		else return JS_NULL;
	}
}

FUNCTION(console_log) {
	if (argCount > 0) {
		logIndent();
		log(args, argCount);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_info) {
	if (argCount > 0) {
		u16 previousColor = consoleSetColor(LOGCOLOR_INFO);
		logIndent();
		log(args, argCount);
		consoleSetColor(previousColor);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_warn) {
	if (argCount > 0) {
		u16 previousColor = consoleSetColor(LOGCOLOR_WARN);
		u16 previousBG = consoleSetBackground(LOGCOLOR_WARN_BG);
		logIndent();
		log(args, argCount);
		consoleSetColor(previousColor);
		consoleSetBackground(previousBG);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_error) {
	if (argCount > 0) {
		u16 previousColor = consoleSetColor(LOGCOLOR_ERROR);
		u16 previousBG = consoleSetBackground(LOGCOLOR_ERROR_BG);
		logIndent();
		log(args, argCount);
		consoleSetColor(previousColor);
		consoleSetBackground(previousBG);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_assert) {
	if (argCount == 0 || !jerry_value_to_boolean(args[0])) {
		u16 previousColor = consoleSetColor(LOGCOLOR_ERROR);
		u16 previousBG = consoleSetBackground(LOGCOLOR_ERROR_BG);
		logIndent();
		printf("Assertion failed: ");
		log(args + 1, argCount - 1);
		consoleSetColor(previousColor);
		consoleSetBackground(previousBG);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_debug) {
	if (argCount > 0) {
		u16 previousColor = consoleSetColor(LOGCOLOR_DEBUG);
		logIndent();
		log(args, argCount);
		consoleSetColor(previousColor);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_trace) {
	logIndent();
	if (argCount == 0) printf("Trace\n");
	else log(args, argCount);
	jerry_value_t backtraceArr = jerry_get_backtrace(10);
	u32 length = jerry_get_array_length(backtraceArr);
	for (u32 i = 0; i < length; i++) {
		jerry_value_t lineStr = jerry_get_property_by_index(backtraceArr, i);
		char *line = rawString(lineStr);
		logIndent();
		printf(" @ %s\n", line);
		free(line);
		jerry_release_value(lineStr);
	}
	jerry_release_value(backtraceArr);
	return JS_UNDEFINED;
}

FUNCTION(console_dir) {
	consolePause();
	if (argCount > 0) {
		logIndent();
		if (jerry_value_is_object(args[0])) logObject(args[0]);
		else logLiteral(args[0]);
		putchar('\n');
	}
	consoleResume();
	return JS_UNDEFINED;
}

FUNCTION(console_table) {
	if (argCount > 0) logTable(args, argCount);
	return JS_UNDEFINED;
}

FUNCTION(console_group) {
	loggingAddIndent();
	if (argCount > 0) {
		logIndent();
		log(args, argCount);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_groupEnd) {
	loggingRemoveIndent();
	return JS_UNDEFINED;
}

FUNCTION(console_count) {
	jerry_value_t labelStr;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		labelStr = jerry_value_to_string(args[0]);
	}
	else labelStr = String("default");
	
	jerry_value_t countNum = jerry_get_property(ref_consoleCounters, labelStr);
	u32 count;
	if (jerry_value_is_undefined(labelStr)) count = 1;
	else count = jerry_value_as_uint32(countNum) + 1;
	jerry_release_value(countNum);

	logIndent();
	printString(labelStr);
	printf(": %lu\n", count);
	
	countNum = jerry_create_number(count);
	jerry_release_value(jerry_set_property(ref_consoleCounters, labelStr, countNum));
	jerry_release_value(countNum);

	jerry_release_value(labelStr);
	return JS_UNDEFINED;
}

FUNCTION(console_countReset) {
	jerry_value_t labelStr;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		labelStr = jerry_value_to_string(args[0]);
	}
	else labelStr = String("default");

	jerry_value_t hasLabelBool = jerry_has_own_property(ref_consoleCounters, labelStr);
	if (jerry_get_boolean_value(hasLabelBool)) {
		jerry_value_t zeroNum = jerry_create_number(0);
		jerry_set_property(ref_consoleCounters, labelStr, zeroNum);
		jerry_release_value(zeroNum);
	}
	else {
		logIndent();
		printf("Count for '");
		printString(labelStr);
		printf("' does not exist\n");
	}
	jerry_release_value(hasLabelBool);

	jerry_release_value(labelStr);
	return JS_UNDEFINED;
}

FUNCTION(console_time) {
	jerry_value_t labelStr;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		labelStr = jerry_value_to_string(args[0]);
	}
	else labelStr = String("default");

	jerry_value_t hasLabelBool = jerry_has_own_property(ref_consoleTimers, labelStr);
	if (jerry_get_boolean_value(hasLabelBool)) {
		logIndent();
		printf("Timer '");
		printString(labelStr);
		printf("' already exists\n");
	}
	else {
		jerry_value_t counterIdNum = jerry_create_number(counterAdd());
		jerry_release_value(jerry_set_property(ref_consoleTimers, labelStr, counterIdNum));
		jerry_release_value(counterIdNum);
	}
	jerry_release_value(hasLabelBool);
	
	jerry_release_value(labelStr);
	return JS_UNDEFINED;
}

FUNCTION(console_timeLog) {
	jerry_value_t labelStr;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		labelStr = jerry_value_to_string(args[0]);
	}
	else labelStr = String("default");

	logIndent();
	jerry_value_t counterIdNum = jerry_get_property(ref_consoleTimers, labelStr);
	if (jerry_value_is_undefined(counterIdNum)) {
		printf("Timer '");
		printString(labelStr);
		printf("' does not exist\n");
	}
	else {
		int counterId = jerry_value_as_int32(counterIdNum);
		printString(labelStr);
		printf(": %i ms", counterGet(counterId));
		if (argCount > 1) {
			putchar(' ');
			log(args + 1, argCount - 1);
		}
	}
	jerry_release_value(counterIdNum);

	jerry_release_value(labelStr);
	return JS_UNDEFINED;
}

FUNCTION(console_timeEnd) {
	jerry_value_t labelStr;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		labelStr = jerry_value_to_string(args[0]);
	}
	else labelStr = String("default");

	logIndent();
	jerry_value_t counterIdNum = jerry_get_property(ref_consoleTimers, labelStr);
	if (jerry_value_is_undefined(counterIdNum)) {
		printf("Timer '");
		printString(labelStr);
		printf("' does not exist\n");
	}
	else {
		int counterId = jerry_value_as_int32(counterIdNum);
		printString(labelStr);
		printf(": %i ms\n", counterGet(counterId));
		counterRemove(counterId);
		jerry_delete_property(ref_consoleTimers, labelStr);
	}
	jerry_release_value(counterIdNum);

	jerry_release_value(labelStr);
	return JS_UNDEFINED;
}

FUNCTION(console_set_textColor) {
	char *colorDesc = toRawString(args[0]);
	u16 color = colorParse(colorDesc, consoleGetColor());
	free(colorDesc);
	consoleSetColor(color);
	return JS_UNDEFINED;
}

FUNCTION(console_set_textBackground) {
	char *colorDesc = toRawString(args[0]);
	u16 color = colorParse(colorDesc, consoleGetBackground());
	free(colorDesc);
	consoleSetBackground(color);
	return JS_UNDEFINED;
}

void exposeIOAPI(jerry_value_t global) {
	ref_consoleCounters = jerry_create_object();
	ref_consoleTimers = jerry_create_object();

	setMethod(ref_global, "alert", alert);
	setMethod(ref_global, "confirm", confirm);
	setMethod(ref_global, "prompt", prompt);

	jerry_value_t console = createObject(global, "console");
	setMethod(console, "assert", console_assert);
	setMethod(console, "clear", VOID(consoleClean()));
	setMethod(console, "count", console_count);
	setMethod(console, "countReset", console_countReset);
	setMethod(console, "debug", console_debug);
	setMethod(console, "dir", console_dir);
	setMethod(console, "error", console_error);
	setMethod(console, "group", console_group);
	setMethod(console, "groupEnd", console_groupEnd);
	setMethod(console, "info", console_info);
	setMethod(console, "log", console_log);
	setMethod(console, "table", console_table);
	setMethod(console, "time", console_time);
	setMethod(console, "timeLog", console_timeLog);
	setMethod(console, "timeEnd", console_timeEnd);
	setMethod(console, "trace", console_trace);
	setMethod(console, "warn", console_warn);
	defGetterSetter(console, "textColor", RETURN(jerry_create_number(consoleGetColor())), console_set_textColor);
	defGetterSetter(console, "textBackground", RETURN(jerry_create_number(consoleGetBackground())), console_set_textBackground);
	jerry_release_value(console);

	jerry_value_t keyboard = createObject(global, "keyboard");
	setMethod(keyboard, "hide", VOID(keyboardHide()));
	setMethod(keyboard, "show", VOID(keyboardShow()));
	setMethod(keyboard, "watchButtons", VOID(keyboardButtonControls(true)));
	setMethod(keyboard, "ignoreButtons", VOID(keyboardButtonControls(false)));
	jerry_release_value(keyboard);
}

void releaseIOReferences() {
	jerry_release_value(ref_consoleCounters);
	jerry_release_value(ref_consoleTimers);
}