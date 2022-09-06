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
	keyboardDemoInit();
	jerry_init(JERRY_INIT_EMPTY);
	exposeAPI();

	// try to read main.js file from root
	FILE *file = fopen("/main.js", "r");
	if (file == NULL) {
		printf("File read error!");
		while(true) {
			swiWaitForVBlank();
			scanKeys();
			if (keysDown() & KEY_START) break;
		}
	}
	else execFile(file, true);

	// cleanup and exit
	jerry_cleanup();
	return 0;
}