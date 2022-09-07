#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include "jerry/jerryscript.h"
#include "api.h"
#include "util.h"


void tempLoadMain() {
	// try to read main.js file from root
	FILE *file = fopen("/main.js", "r");
	if (file == NULL) {
		BG_PALETTE_SUB[0] = 0x001F;
		printf("\n\n\tFile read error!\n\nCouldn't open \"/main.js\".\n\n\n\n\tPress START to exit.");
		while(true) {
			swiWaitForVBlank();
			scanKeys();
			if (keysDown() & KEY_START) break;
		}
	}
	else {
		jerry_value_t result = execFile(file, true);
		if (jerry_value_is_error(result)) {
			BG_PALETTE_SUB[0] = 0x8010;
			consoleClear();

			jerry_error_t errorCode = jerry_get_error_type(result);
			jerry_value_t errorThrown = jerry_get_value_from_error(result, false);
			if (errorCode == JERRY_ERROR_NONE) {
				printf("\n\n\tUncaught value\n\n\t");
				printValue(errorThrown);
				printf("\n\n\n\tPress START to exit.");
			}
			else {
				char *message = getString(getProperty(errorThrown, "message"), true);
				char *name = getString(getProperty(errorThrown, "name"), true);
				printf("\n\n\t%s\n\n\t%s\n\n\n\n\tPress START to exit.", name, message);
				free(message);
				free(name);
			}
			jerry_release_value(errorThrown);
		}
		jerry_release_value(result);
	}
}

void repl() {
	consoleSetWindow(NULL, 0, 0, 32, 14);
	keyboardShow();
	while(true) {
		keyboardClearBuffer();
		while(true) {
			swiWaitForVBlank();
			if (keyboardEnterPressed) break;
			keyboardUpdate();
		}
		printf("\n-> ");

		jerry_value_t result;
		jerry_value_t parsedLine = jerry_parse(
			(jerry_char_t *) "line", 4,
			(jerry_char_t *) keyboardBuffer(), strlen(keyboardBuffer()),
			JERRY_PARSE_STRICT_MODE
		);
		if (jerry_value_is_error(parsedLine)) result = parsedLine;
		else {
			result = jerry_run(parsedLine);
			jerry_release_value(parsedLine);
		}
		if (jerry_value_is_error(result)) {
			jerry_error_t errorCode = jerry_get_error_type(result);
			jerry_value_t errorThrown = jerry_get_value_from_error(result, false);
			if (errorCode == JERRY_ERROR_NONE) {
				printf("Uncaught value: ");
				printValue(errorThrown);
			}
			else {
				char *message = getString(getProperty(errorThrown, "message"), true);
				char *name = getString(getProperty(errorThrown, "name"), true);
				printf("%s: %s\n", name, message);
				free(message);
				free(name);
			}
			jerry_release_value(errorThrown);
		}
		else printValue(result);
		jerry_release_value(result);
	}
}

int main(int argc, char **argv) {
	// startup
	consoleDemoInit();
	consoleDebugInit(DebugDevice_CONSOLE);
	fatInitDefault();
	Keyboard* kbd = keyboardDemoInit();
	kbd->OnKeyPressed = onKeyboardKeyPress;
	jerry_init(JERRY_INIT_EMPTY);
	exposeAPI();

	// start repl loop
	repl();

	// wait, then cleanup and exit
	while(true) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_START) break;
	}
	jerry_cleanup();
	return 0;
}