#include <fat.h>
#include <nds/arm9/input.h>
#include <nds/fifocommon.h>
#include <nds/interrupts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

void runFile(const char *filename) {
	if (access(filename, F_OK) != 0) {
		BG_PALETTE_SUB[0] = 0x001F;
		printf("\n\n\tCouldn't find \"%s\".", filename);
		return;
	}
	FILE *file = fopen(filename, "r");
	if (file == NULL) {
		BG_PALETTE_SUB[0] = 0x001F;
		printf("\n\n\tCouldn't open \"%s\".", filename);
		return;
	}
	
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);
	char *script = (char *) malloc(size);
	fread(script, 1, size, file);
	fclose(file);
	jerry_value_t parsedCode = jerry_parse(
		(const jerry_char_t *) filename, strlen(filename),
		(const jerry_char_t *) script, size,
		JERRY_PARSE_STRICT_MODE & JERRY_PARSE_MODULE
	);
	free(script);
	queueTask(runParsedCodeTask, &parsedCode, 1);
	jerry_release_value(parsedCode);
	queueEventName("load");
	loadStorage(filename);
	eventLoop();
	if (!abortFlag) {
		queueEventName("unload");
		runTasks();
	}
}

void repl() {
	inREPL = true;
	keyboardOpen(true);
	loadStorage("/REPL");
	eventLoop();
}

int main(int argc, char **argv) {
	// startup
	srand(time(NULL));
	fifoSendValue32(FIFO_PM, PM_REQ_SLEEP_DISABLE);
	mainConsole = consoleDemoInit();
	fatInitSuccess = fatInitDefault();
	keyboard = keyboardDemoInit();
	keyboard->OnKeyPressed = onKeyboardKeyPress;
	keyboard->OnKeyReleased = onKeyboardKeyRelease;
	jerry_init(JERRY_INIT_EMPTY);
	jerry_set_error_object_created_callback(onErrorCreated, NULL);
	jerry_jsds_set_promise_rejection_op_callback(onPromiseRejectionOp);
	exposeAPI();

	// run
	if (argc > 1) runFile(argv[1]);
	else if (access("/_boot_.js", F_OK) == 0) runFile("/_boot_.js");
	else repl();

	// cleanup
	keyboardClose();
	clearTasks();
	clearTimeouts();
	releaseReferences();
	jerry_cleanup();

	// exit
	if (!userClosed) while (true) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_START) break;
	}
	return 0;
}