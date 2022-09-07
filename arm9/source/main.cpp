#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include "jerry/jerryscript.h"
#include "api.h"
#include "util.h"


int main(int argc, char **argv) {
	// startup
	consoleDemoInit();
	fatInitDefault();
	Keyboard* kbd = keyboardDemoInit();
	kbd->OnKeyPressed = onKeyboardKeyPress;
	jerry_init(JERRY_INIT_EMPTY);
	exposeAPI();

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
			}
			jerry_release_value(errorThrown);

			while(true) {
				swiWaitForVBlank();
				scanKeys();
				if (keysDown() & KEY_START) break;
			}
		}
		jerry_release_value(result);
	}

	// cleanup and exit
	jerry_cleanup();
	return 0;
}