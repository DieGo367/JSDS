#include "input.hpp"

#include <nds/arm9/input.h>
#include <stdlib.h>

#include "event.hpp"
#include "inline.hpp"



u32 canceledButtons = 0;
// Args: EventTarget, Event
void dispatchButtonEventTask(const jerry_value_t *args, u32 argCount) {
	if (dispatchEvent(args[0], args[1], false)) {
		char *button = getStringProperty(args[1], "button");
		if (button[0] == 'A') canceledButtons |= KEY_A;
		else if (button[0] == 'B') canceledButtons |= KEY_B;
		else if (button[0] == 'X') canceledButtons |= KEY_X;
		else if (button[0] == 'Y') canceledButtons |= KEY_Y;
		else if (button[0] == 'L') canceledButtons |= button[1] == 0 ? KEY_L : KEY_LEFT;
		else if (button[0] == 'R') canceledButtons |= button[1] == 0 ? KEY_R : KEY_RIGHT;
		else if (button[0] == 'U') canceledButtons |= KEY_UP;
		else if (button[0] == 'D') canceledButtons |= KEY_DOWN;
		else if (button[0] == 'S') canceledButtons |= button[1] == 'T' ? KEY_START : KEY_SELECT;
		free(button);
	}
}

void buttonEvents(bool down) {
	canceledButtons = 0;
	u32 set = down ? keysDown() : keysUp();
	if (set) {
		jerry_value_t buttonStr = createString("button");
		jerry_value_t args[2] = {createString(down ? "buttondown" : "buttonup"), jerry_create_object()};
		jerry_release_value(jerry_set_property(args[1], buttonStr, JS_NULL));
		if (down) setProperty(args[1], "cancelable", JS_TRUE);
		jerry_value_t event = jerry_construct_object(ref_Event, args, 2);
		bool valueWritten = true;
		#define TEST_VALUE(name, value) else if (set & value) {\
			jerry_value_t value = createString(name); \
			jerry_set_internal_property(event, buttonStr, value); \
			jerry_release_value(value); \
		}
		if (false);
		FOR_BUTTONS(TEST_VALUE)
		else valueWritten = false;
		if (valueWritten) {
			if (down) {
				jerry_value_t taskArgs[2] = {ref_global, event};
				queueTask(dispatchButtonEventTask, taskArgs, 2);
			}
			else queueEvent(ref_global, event);
		}
		jerry_release_value(event);
		jerry_release_value(args[0]);
		jerry_release_value(args[1]);
		jerry_release_value(buttonStr);
	}
}

u32 getCanceledButtons() { return canceledButtons; }

u16 prevX = 0, prevY = 0;
void queueTouchEvent(const char *name, int curX, int curY, bool usePrev) {
	jerry_value_t args[2] = {createString(name), jerry_create_object()};
	jerry_value_t x = jerry_create_number(curX);
	jerry_value_t y = jerry_create_number(curY);
	jerry_value_t dx = usePrev ? jerry_create_number(curX - (int) prevX) : jerry_create_number_nan();
	jerry_value_t dy = usePrev ? jerry_create_number(curY - (int) prevY) : jerry_create_number_nan();
	setProperty(args[1], "x", x);
	setProperty(args[1], "y", y);
	setProperty(args[1], "dx", dx);
	setProperty(args[1], "dy", dy);
	jerry_release_value(x);
	jerry_release_value(y);
	jerry_release_value(dx);
	jerry_release_value(dy);
	jerry_value_t event = jerry_construct_object(ref_Event, args, 2);
	queueEvent(ref_global, event);
	jerry_release_value(args[0]);
	jerry_release_value(args[1]);
	jerry_release_value(event);
}

void touchEvents() {
	touchPosition pos;
	touchRead(&pos);
	if (keysDown() & KEY_TOUCH) queueTouchEvent("touchstart", pos.px, pos.py, false);
	else if (keysHeld() & KEY_TOUCH) {
		if (prevX != pos.px || prevY != pos.py) queueTouchEvent("touchmove", pos.px, pos.py, true);
	}
	else if (keysUp() & KEY_TOUCH) queueTouchEvent("touchend", prevX, prevY, false);
	prevX = pos.px;
	prevY = pos.py;
}

bool dispatchKeyboardEvent(bool down, const char16_t codepoint, const char *name, u8 location, bool shift, int layout, bool repeat) {
	jerry_value_t kbdEventArgs[2] = {createString(down ? "keydown" : "keyup"), jerry_create_object()};
	setProperty(kbdEventArgs[1], "cancelable", JS_TRUE);

	jerry_value_t keyStr;
	if (codepoint == 2) keyStr = createString("Shift"); // hardcoded override to remove Left/Right variants of Shift
	else if (codepoint < ' ') keyStr = createString(name);
	else if (codepoint < 0x80) keyStr = createString((char *) &codepoint);
	else if (codepoint < 0x800) {
		char converted[3] = {(char) (0xC0 | codepoint >> 6), (char) (BIT(7) | (codepoint & 0x3F)), 0};
		keyStr = createString(converted);
	}
	else {
		char converted[4] = {(char) (0xE0 | codepoint >> 12), (char) (BIT(7) | (codepoint >> 6 & 0x3F)), (char) (BIT(7) | (codepoint & 0x3F)), 0};
		keyStr = createString(converted);
	}
	jerry_value_t codeStr = createString(name);
	jerry_value_t layoutStr = createString(
		layout == 0 ? "AlphaNumeric" : 
		layout == 1 ? "LatinAccented" :
		layout == 2 ? "Kana" :
		layout == 3 ? "Symbol" :
		layout == 4 ? "Pictogram"
	: "");
	setProperty(kbdEventArgs[1], "key", keyStr);
	setProperty(kbdEventArgs[1], "code", codeStr);
	setProperty(kbdEventArgs[1], "layout", layoutStr);
	setProperty(kbdEventArgs[1], "repeat", jerry_create_boolean(repeat));
	jerry_release_value(keyStr);
	jerry_release_value(codeStr);
	jerry_release_value(layoutStr);

	setProperty(kbdEventArgs[1], "shifted", jerry_create_boolean(shift));

	jerry_value_t kbdEvent = jerry_construct_object(ref_Event, kbdEventArgs, 2);
	bool canceled = dispatchEvent(ref_global, kbdEvent, false);
	jerry_release_value(kbdEvent);
	jerry_release_value(kbdEventArgs[0]);
	jerry_release_value(kbdEventArgs[1]);
	return canceled;
}

bool pauseKeyEvents = false;
bool onKeyDown(const char16_t codepoint, const char *name, bool shift, int layout, bool repeat) {
	if (!pauseKeyEvents && dependentEvents & keydown) {
		return dispatchKeyboardEvent(true, codepoint, name, 0, shift, layout, repeat);
	}
	return false;
}
bool onKeyUp(const char16_t codepoint, const char *name, bool shift, int layout) {
	if (!pauseKeyEvents && dependentEvents & keyup) {
		return dispatchKeyboardEvent(false, codepoint, name, 0, shift, layout, false);
	}
	return false;
}