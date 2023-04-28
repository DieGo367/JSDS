#include <fat.h>
#include <nds/arm9/input.h>
#include <nds/arm9/video.h>
#include <nds/fifocommon.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "api.hpp"
#include "error.hpp"
#include "event.hpp"
#include "file.hpp"
#include "io.hpp"
#include "io/console.hpp"
#include "io/keyboard.hpp"
#include "timeouts.hpp"

#include "font_nftr.h"



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
	storageLoad(filename);
	eventLoop();
}

void repl() {
	inREPL = true;
	keyboardShow();
	storageLoad("/REPL");
	eventLoop();
}

int main(int argc, char **argv) {
	// startup
	srand(time(NULL));
	fifoSendValue32(FIFO_PM, PM_REQ_SLEEP_DISABLE);
	NitroFont font = fontLoad(font_nftr);
	consoleInit(font);
	keyboardInit(font);
	keyboardSetPressHandler(onKeyDown);
	keyboardSetReleaseHandler(onKeyUp);
	fatInitDefault();
	jerry_init(JERRY_INIT_EMPTY);
	setErrorHandlers();
	exposeAPI();

	// run
	if (argc > 1) runFile(argv[1]);
	else {
		char *filePath = fileBrowse(font, "Select a script to run.", ".", {(char *) "js"}, true);
		if (filePath != NULL) runFile(filePath);
		else repl();
	}

	// cleanup
	keyboardHide();
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