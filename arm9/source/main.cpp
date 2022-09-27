#include <fat.h>
#include <nds/arm9/input.h>
#include <nds/interrupts.h>
#include <stdio.h>
#include <stdlib.h>

#include "api.h"
#include "console.h"
#include "inline.h"
#include "jerry/jerryscript.h"
#include "keyboard.h"
#include "timeouts.h"



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
			consolePrintLiteral(jobResult);
			jerry_release_value(jobResult);
		}
		else break;
	}
	jerry_release_value(jobResult);
	return result;
}

void tempLoadMain() {
	// try to read main.js file from root
	FILE *file = fopen("/main.js", "r");
	if (file == NULL) {
		BG_PALETTE_SUB[0] = 0x001F;
		printf("\n\n\tFile read error!\n\nCouldn't open \"/main.js\".");
	}
	else {
		fseek(file, 0, SEEK_END);
		long size = ftell(file);
		rewind(file);
		char *script = (char *) malloc(size);
		fread(script, 1, size, file);
		fclose(file);
		jerry_value_t parsedCode = jerry_parse(
			(const jerry_char_t *) "main", 4,
			(const jerry_char_t *) script, size,
			JERRY_PARSE_STRICT_MODE & JERRY_PARSE_MODULE
		);
		free(script);
		jerry_value_t result = execute(parsedCode);
		if (jerry_value_is_error(result)) {
			consolePrintLiteral(result);
			putchar('\n');
		}
		jerry_release_value(result);
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
		jerry_value_t result2 = jerry_call_function(dispatchFunc, global, &loadEvent, 1);
		if (jerry_value_is_error(result2)) {
			consolePrintLiteral(result2);
			putchar('\n');
		}
		jerry_release_value(result2);
		jerry_release_value(dispatchFunc);
		jerry_value_t handler = getProperty(global, "onload");
		if (jerry_value_is_function(handler)) {
			jerry_value_t result = jerry_call_function(handler, global, &loadEvent, 1);
			if (jerry_value_is_error(result)) {
				consolePrintLiteral(result);
				putchar('\n');
			}
			jerry_release_value(result);
		}
		jerry_release_value(handler);
		jerry_release_value(loadEvent);
		jerry_release_value(global);
	}
	while (true) {
		swiWaitForVBlank();
		checkTimeouts();
		scanKeys();
		if (keysDown() & KEY_START) break;
	}
}

void repl() {
	consoleSetWindow(NULL, 0, 0, 32, 14);
	keyboardShow();
	while (true) {
		keyboardClearBuffer();
		while (true) {
			swiWaitForVBlank();
			checkTimeouts();
			if (keyboardEnterPressed) break;
			keyboardUpdate();
		}
		putchar('\n');

		jerry_value_t parsedCode = jerry_parse(
			(const jerry_char_t *) "line", 4,
			(const jerry_char_t *) keyboardBuffer(), keyboardBufferLen(),
			JERRY_PARSE_STRICT_MODE
		);
		jerry_value_t result = execute(parsedCode);

		printf("-> ");
		consolePrintLiteral(result);
		putchar('\n');
		jerry_release_value(result);
	}
}

int main(int argc, char **argv) {
	// startup
	mainConsole = consoleDemoInit();
	fatInitDefault();
	keyboard = keyboardDemoInit();
	keyboard->OnKeyPressed = onKeyboardKeyPress;
	jerry_init(JERRY_INIT_EMPTY);
	exposeAPI();

	// start repl loop
	// repl();
	tempLoadMain();

	jerry_cleanup();
	return 0;
}