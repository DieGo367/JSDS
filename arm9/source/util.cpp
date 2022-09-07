#include "util.h"

#include <nds.h>
#include "jerry/jerryscript.h"


jerry_value_t execFile(FILE *file, bool closeFile) {
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);
	u8 *script = (u8 *) malloc(size);
	fread(script, 1, size, file);
	if (closeFile) fclose(file);

	jerry_value_t parsedCode = jerry_parse((const jerry_char_t *) "main", 4, (const jerry_char_t *) script, size, 0);
	free(script);
	if (jerry_value_is_error(parsedCode)) return parsedCode;
	else {
		jerry_value_t result = jerry_run(parsedCode);
		jerry_release_value(parsedCode);
		return result;
	}
}

void printValue(jerry_value_t value) {
	char *string = getString(jerry_value_to_string(value), true);
	printf("%s\n", string);
	free(string);
}

const int keyboardBufferSize = 256;
char buf[keyboardBufferSize] = {0};
int idx = 0;
const char *keyboardBuffer() {
	return buf;
}
void keyboardClearBuffer() {
	memset(buf, 0, keyboardBufferSize);
	idx = 0;
}
void onKeyboardKeyPress(int key) {
	// backspace
	if (key == 8 && idx > 0) {
		buf[--idx] = '\0';
		consoleClear();
		printf("%s", buf);

	}
	// tab, return, and other printable chars
	else if (idx < keyboardBufferSize - 1 && (key == 9 || key == 10 || (key >= 32 && key <= 126))) {
		buf[idx++] = key;
		putchar(key);
	}
}