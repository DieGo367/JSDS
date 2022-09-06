#include "api.h"

#include <nds.h>
#include <stdio.h>
#include "jerry/jerryscript.h"
#include "util.h"


static jerry_value_t alertHandler(const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], u32 argCount) {
	consoleClear();
	printf("============= Alert ============");
	if (argCount > 0) printValue(args[0]);
	printf("===================== (A) OK ===");
	while(true) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_A) break;
	}
	consoleClear();
	return jerry_create_undefined();
}

static jerry_value_t closeHandler(const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], u32 argCount) {
	exit(0);
}

static jerry_value_t confirmHandler(const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], u32 argCount) {
	consoleClear();
	printf("============ Confirm ===========");
	if (argCount > 0) printValue(args[0]);
	printf("========= (A) OK  (B) Cancel ===");
	while(true) {
		swiWaitForVBlank();
		scanKeys();
		u32 keys = keysDown();
		if (keys & KEY_A) return consoleClear(), jerry_create_boolean(true);
		else if (keys & KEY_B) return consoleClear(), jerry_create_boolean(false);
	}
}

static jerry_value_t promptHandler(const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], u32 argCount) {
	consoleClear();
	printf("============ Prompt ============");
	if (argCount > 0) printValue(args[0]);
	printf("========= (A) OK  (B) Cancel ===");
	consoleSetWindow(NULL, 0, 3, 32, 11);

	keyboardClearBuffer();
	keyboardShow();
	bool canceled = false;
	while(true) {
		swiWaitForVBlank();
		scanKeys();
		u32 keys = keysDown();
		if (keys & KEY_A) break;
		else if (keys & KEY_B) {
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


static jerry_value_t consoleLogHandler(const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], const u32 argCount) {
	for (u32 i = 0; i < argCount; i++) printValue(args[i]);
	return jerry_create_undefined();
}

void exposeAPI() {
	jerry_value_t global = jerry_get_global_object();

	setMethod(global, "alert", alertHandler);
	setMethod(global, "close", closeHandler);
	setMethod(global, "confirm", confirmHandler);
	setMethod(global, "prompt", promptHandler);
	
	jerry_value_t console = jerry_create_object();
	setProperty(global, "console", console);
	setMethod(console, "log", consoleLogHandler);
	jerry_release_value(console);

	jerry_release_value(global);
}