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
#include "jerry/jerryscript-port-default.h"
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
		queueEventName("load");
	}
	while (workExists()) {
		swiWaitForVBlank();
		timeoutUpdate();
		runTasks();
	}
	if (!abortFlag) {
		queueEventName("unload");
		runTasks();
	}
}

void repl() {
	consoleSetWindow(NULL, 0, 0, 32, 14);
	keyboardShow();
	while (!abortFlag) {
		keyboardClearBuffer();
		while (!abortFlag) {
			swiWaitForVBlank();
			timeoutUpdate();
			runTasks();
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
		if (!abortFlag) {
			printf("-> ");
			consolePrintLiteral(result);
			putchar('\n');
		}
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
	jerry_jsds_set_promise_rejection_op_callback(onPromiseRejectionOp);
	exposeAPI();

	if (inREPL) repl();
	else tempLoadMain();

	clearTasks();
	clearTimeouts();
	releaseReferences();
	jerry_cleanup();
	if (!inREPL) while (true) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_START) break;
	}
	return 0;
}