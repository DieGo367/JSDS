#include <fat.h>
#include <nds/arm9/input.h>
#include <nds/interrupts.h>
#include <stdio.h>
#include <stdlib.h>

#include "api.h"
#include "execute.h"
#include "console.h"
#include "inline.h"
#include "jerry/jerryscript.h"
#include "keyboard.h"
#include "timeouts.h"



void onErrorCreated(jerry_value_t errorObject, void *userPtr) {
	jerry_value_t backtrace = jerry_get_backtrace(10);
	jerry_set_internal_property(errorObject, ref_str_backtrace, backtrace);
	jerry_release_value(backtrace);
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
			(const jerry_char_t *) "/main.js", 8,
			(const jerry_char_t *) script, size,
			JERRY_PARSE_STRICT_MODE & JERRY_PARSE_MODULE
		);
		free(script);
		jerry_release_value(execute(parsedCode));
		fireLoadEvent();
	}
	while (true) {
		swiWaitForVBlank();
		checkTimeouts();
		eventLoop();
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
			eventLoop();
			if (keyboardEnterPressed) break;
			keyboardUpdate();
		}
		putchar('\n');

		jerry_value_t parsedCode = jerry_parse(
			(const jerry_char_t *) "<REPL>", 6,
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
	jerry_set_error_object_created_callback(onErrorCreated, NULL);
	ref_str_name = jerry_create_string((jerry_char_t *) "name");
	ref_str_constructor = jerry_create_string((jerry_char_t *) "constructor");
	ref_str_prototype = jerry_create_string((jerry_char_t *) "prototype");
	ref_str_backtrace = jerry_create_string((jerry_char_t *) "backtrace");
	ref_task_runTimeout = jerry_create_external_function(runTimeoutTask);
	exposeAPI();

	if (inREPL) repl();
	else tempLoadMain();

	jerry_release_value(ref_Event);
	jerry_release_value(ref_Error);
	jerry_release_value(ref_DOMException);
	jerry_release_value(ref_task_runTimeout);
	jerry_release_value(ref_task_dispatchEvent);
	jerry_release_value(ref_str_name);
	jerry_release_value(ref_str_constructor);
	jerry_release_value(ref_str_prototype);
	jerry_release_value(ref_str_backtrace);
	clearTasks();
	clearTimeouts();
	jerry_cleanup();
	return 0;
}