#include "keyboard.h"

#include <nds/arm9/console.h>
#include <string.h>

#include "jerry/jerryscript.h"
#include "tasks.h"



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

char codeNamesA[13][12] = {
	"AltLeft",   "Escape",      "ArrowLeft", "ArrowDown", "ArrowRight",
	"ArrowUp",   "ControlLeft", "CapsLock",  "ShiftLeft", "MetaRight",
	"Backspace", "Tab",         "Enter"
};
char codeNamesB[14][13] = {
	"Space",       "Quote",     "Comma",        "Minus",     "Period",
	"Slash",       "Digit_",    "Semicolon",    "Equal",     "Key_",
	"BracketLeft", "Backslash", "BracketRight", "Backquote"
};
u8 codeMappingsA[26 + 11] = {
	0, 0, 0,  1,  0,  0, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0,
	0, 0, 0,  0,  0,  9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 10, 11, 12
};
u8 codeMappingsB[126 - 31] = {
	0,    0x16, 1,    0x36, 0x46, 0x56, 0x76, 1,
	0x96, 0x06, 0x86, 8,    2,    3,    4,    5,
	0x06, 0x16, 0x26, 0x36, 0x46, 0x56, 0x66, 0x76,
	0x86, 0x96, 7,    7,    2,    8,    4,    5,
	0x26, 9,    9,    9,    9,    9,    9,    9,
	9,    9,    9,    9,    9,    9,    9,    9,
	9,    9,    9,    9,    9,    9,    9,    9,
	9,    9,    9,    10,   11,   12,   0x66, 3,
	13,   9,    9,    9,    9,    9,    9,    9,
	9,    9,    9,    9,    9,    9,    9,    9,
	9,    9,    9,    9,    9,    9,    9,    9,
	9,    9,    9,    10,   11,   12,   13
};
char keyNames[5][8] = {
	"_", "Alt", "Control", "Shift", "Meta"
};

char *getCodeName(int key) {
	if (key < ' ') return codeNamesA[codeMappingsA[key + 26]];
	else {
		u8 mapping = codeMappingsB[key - 32];
		u8 index = mapping & 0xF;
		char *code = codeNamesB[index];
		if (index == 6) code[5] = '0' + ((mapping & 0xF0) >> 4);
		else if (index == 9) code[4] = (key < 'a' ? key : key - 32);
		return code;
	}
}
char *getKeyName(int key) {
	if (key >= 32 && key <= 126) { // ASCII printable
		keyNames[0][0] = key;
		return keyNames[0];
	}
	else if (key == DVK_ALT) return keyNames[1];
	else if (key == DVK_CTRL) return keyNames[2];
	else if (key == DVK_SHIFT) return keyNames[3];
	else if (key == DVK_MENU) return keyNames[4];
	else return getCodeName(key);
}
u8 getLocation(int key) {
	if (key == DVK_ALT || key == DVK_CTRL || key == DVK_SHIFT) return 1; // LEFT
	else if (key == DVK_MENU) return 2; // RIGHT
	else return 0; // STANDARD
}

bool shiftToggle = false, ctrlToggle = false, altToggle = false, metaToggle = false, capsToggle = false;

void onKeyboardKeyPress(int key) {
	if (key == DVK_SHIFT) shiftToggle = !shiftToggle;
	else if (key == DVK_CTRL) ctrlToggle = !ctrlToggle;
	else if (key == DVK_ALT) altToggle = !altToggle;
	else if (key == DVK_MENU) metaToggle = !metaToggle;
	else if (key == DVK_CAPS) capsToggle = !capsToggle;
	
	if (dependentEvents & keydown) {
		if (dispatchKeyboardEvent(true, getKeyName(key), getCodeName(key), getLocation(key), shiftToggle, ctrlToggle, altToggle, metaToggle, capsToggle)) return;
	}

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
void onKeyboardKeyRelease(int key) {
	if (dependentEvents & keyup) {
		dispatchKeyboardEvent(false, getKeyName(key), getCodeName(key), getLocation(key), shiftToggle, ctrlToggle, altToggle, metaToggle, capsToggle);
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