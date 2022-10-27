#include "keyboard.h"

#include <nds/arm9/console.h>
#include <string.h>

#include "jerry/jerryscript.h"



Keyboard *keyboard;

const int keyboardBufferSize = 256;
char buf[keyboardBufferSize] = {0};
int idx = 0;
bool keyboardEnterPressed = false;
bool keyboardEscapePressed = false;
bool kbdPrintInput = true;
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
	if (!kbdPrintInput) return;
	Keyboard *kbd = keyboardGetDefault();
	keyboardEnterPressed = false;
	keyboardEscapePressed = false;
	if (key == DVK_FOLD) keyboardEscapePressed = true;
	else if (key == DVK_BACKSPACE && idx > 0) {
		buf[--idx] = '\0';
		consoleClear();
		printf(buf);
	}
	else if (key == DVK_ENTER && !kbd->shifted) keyboardEnterPressed = true;
	else if (idx < keyboardBufferSize - 1 && (key == DVK_TAB || key == DVK_ENTER || (key >= 32 && key <= 126))) {
		buf[idx++] = key;
		putchar(key);
	}
}

bool kbdIsOpen = false;
void keyboardOpen(bool printInput) {
	if (kbdIsOpen) return;
	keyboardClearBuffer();
	consoleClear();
	consoleSetWindow(NULL, 0, 0, 32, 12);
	keyboardShow();
	kbdIsOpen = true;
	kbdPrintInput = printInput;
}
void keyboardClose() {
	if (!kbdIsOpen) return;
	keyboardHide();
	consoleClear();
	consoleSetWindow(NULL, 0, 0, 32, 24);
	kbdIsOpen = false;
}
bool isKeyboardOpen() {
	return kbdIsOpen;
}