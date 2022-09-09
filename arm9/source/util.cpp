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

	jerry_value_t parsedCode = jerry_parse(
		(const jerry_char_t *) "main", 4,
		(const jerry_char_t *) script, size,
		JERRY_PARSE_STRICT_MODE & JERRY_PARSE_MODULE
	);
	free(script);
	if (jerry_value_is_error(parsedCode)) return parsedCode;
	else {
		jerry_value_t result = jerry_run(parsedCode);
		jerry_release_value(parsedCode);
		return result;
	}
}

const int keyboardBufferSize = 256;
char buf[keyboardBufferSize] = {0};
int idx = 0;
bool keyboardEnterPressed = false;
bool keyboardEscapePressed = false;
const char *keyboardBuffer() {
	return buf;
}
u8 keyboardBufferLen() {
	return idx;
}
void keyboardClearBuffer() {
	memset(buf, 0, keyboardBufferSize);
	idx = 0;
	keyboardEnterPressed = false;
	keyboardEscapePressed = false;
}
void onKeyboardKeyPress(int key) {
	Keyboard *kbd = keyboardGetDefault();
	keyboardEnterPressed = false;
	keyboardEscapePressed = false;
	if (key == DVK_FOLD) keyboardEscapePressed = true;
	else if (key == DVK_BACKSPACE && idx > 0) {
		buf[--idx] = '\0';
		consoleClear();
		printf("%s", buf);
	}
	else if (key == DVK_ENTER && !kbd->shifted) keyboardEnterPressed = true;
	else if (idx < keyboardBufferSize - 1 && (key == DVK_TAB || key == DVK_ENTER || (key >= 32 && key <= 126))) {
		buf[idx++] = key;
		putchar(key);
	}
}