#include <fat.h>
#include <nds/arm9/input.h>
#include <nds/fifocommon.h>
#include <nds/interrupts.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "api.h"
#include "console.h"
#include "inline.h"
#include "jerry/jerryscript.h"
#include "jerry/jerryscript-port-default.h"
#include "keyboard.h"
#include "tasks.h"
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
		return;
	}
	
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
	queueTask(runParsedCodeTask, &parsedCode, 1);
	jerry_release_value(parsedCode);
	queueEventName("load");
	loadStorage("/main.js");
	eventLoop();
	if (!abortFlag) {
		queueEventName("unload");
		runTasks();
	}
}

void repl() {
	keyboardOpen(true);
	loadStorage("/REPL");
	eventLoop();
}

int main(int argc, char **argv) {
	// startup
	srand(time(NULL));
	fifoSendValue32(FIFO_PM, PM_REQ_SLEEP_DISABLE);
	mainConsole = consoleDemoInit();
	fatInitDefault();
	keyboard = keyboardDemoInit();
	keyboard->OnKeyPressed = onKeyboardKeyPress;
	jerry_init(JERRY_INIT_EMPTY);
	jerry_set_error_object_created_callback(onErrorCreated, NULL);
	jerry_jsds_set_promise_rejection_op_callback(onPromiseRejectionOp);
	exposeAPI();

	// run
	if (inREPL) repl();
	else tempLoadMain();

	// cleanup
	clearTasks();
	clearTimeouts();
	releaseReferences();
	jerry_cleanup();

	// exit
	if (!inREPL) while (true) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_START) break;
	}
	return 0;
}