#include "input.hpp"

#include <nds/arm9/input.h>
#include <stdlib.h>

#include "event.hpp"
#include "helpers.hpp"



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
		jerry_value_t buttonProp = String("button");
		jerry_value_t eventArgs[2] = {String(down ? "buttondown" : "buttonup"), jerry_create_object()};
		jerry_release_value(jerry_set_property(eventArgs[1], buttonProp, JS_NULL));
		if (down) setProperty(eventArgs[1], "cancelable", JS_TRUE);
		jerry_value_t eventObj = jerry_construct_object(ref_Event, eventArgs, 2);
		bool valueWritten = true;
		if (false);
		#define TEST_VALUE(buttonName, keyCode) else if (set & keyCode) {\
			jerry_value_t value = String(buttonName); \
			jerry_set_internal_property(eventObj, buttonProp, value); \
			jerry_release_value(value); \
		}
		FOR_BUTTONS(TEST_VALUE)
		else valueWritten = false;
		if (valueWritten) {
			if (down) {
				jerry_value_t taskArgs[2] = {ref_global, eventObj};
				queueTask(dispatchButtonEventTask, taskArgs, 2);
			}
			else queueEvent(ref_global, eventObj);
		}
		jerry_release_value(eventObj);
		jerry_release_value(eventArgs[0]);
		jerry_release_value(eventArgs[1]);
		jerry_release_value(buttonProp);
	}
}

u32 getCanceledButtons() { return canceledButtons; }

u16 prevX = 0, prevY = 0;
void queueTouchEvent(const char *name, int curX, int curY, bool usePrev) {
	jerry_value_t eventArgs[2] = {String(name), jerry_create_object()};
	jerry_value_t xNum = jerry_create_number(curX);
	jerry_value_t yNum = jerry_create_number(curY);
	jerry_value_t dxNum = usePrev ? jerry_create_number(curX - (int) prevX) : jerry_create_number_nan();
	jerry_value_t dyNum = usePrev ? jerry_create_number(curY - (int) prevY) : jerry_create_number_nan();
	setProperty(eventArgs[1], "x", xNum);
	setProperty(eventArgs[1], "y", yNum);
	setProperty(eventArgs[1], "dx", dxNum);
	setProperty(eventArgs[1], "dy", dyNum);
	jerry_release_value(xNum);
	jerry_release_value(yNum);
	jerry_release_value(dxNum);
	jerry_release_value(dyNum);
	jerry_value_t eventObj = jerry_construct_object(ref_Event, eventArgs, 2);
	queueEvent(ref_global, eventObj);
	jerry_release_value(eventArgs[0]);
	jerry_release_value(eventArgs[1]);
	jerry_release_value(eventObj);
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
	jerry_value_t eventArgs[2] = {String(down ? "keydown" : "keyup"), jerry_create_object()};
	setProperty(eventArgs[1], "cancelable", JS_TRUE);

	jerry_value_t keyStr;
	if (codepoint == 2) keyStr = String("Shift"); // hardcoded override to remove Left/Right variants of Shift
	else if (codepoint < ' ') keyStr = String(name);
	else if (codepoint < 0x80) keyStr = String((char *) &codepoint);
	else if (codepoint < 0x800) {
		char converted[3] = {(char) (0xC0 | codepoint >> 6), (char) (BIT(7) | (codepoint & 0x3F)), 0};
		keyStr = String(converted);
	}
	else {
		char converted[4] = {(char) (0xE0 | codepoint >> 12), (char) (BIT(7) | (codepoint >> 6 & 0x3F)), (char) (BIT(7) | (codepoint & 0x3F)), 0};
		keyStr = String(converted);
	}
	jerry_value_t codeStr = String(name);
	jerry_value_t layoutStr = String(
		layout == 0 ? "AlphaNumeric" : 
		layout == 1 ? "LatinAccented" :
		layout == 2 ? "Kana" :
		layout == 3 ? "Symbol" :
		layout == 4 ? "Pictogram"
	: "");
	setProperty(eventArgs[1], "key", keyStr);
	setProperty(eventArgs[1], "code", codeStr);
	setProperty(eventArgs[1], "layout", layoutStr);
	setProperty(eventArgs[1], "repeat", jerry_create_boolean(repeat));
	jerry_release_value(keyStr);
	jerry_release_value(codeStr);
	jerry_release_value(layoutStr);

	setProperty(eventArgs[1], "shifted", jerry_create_boolean(shift));

	jerry_value_t kbdEventObj = jerry_construct_object(ref_Event, eventArgs, 2);
	bool canceled = dispatchEvent(ref_global, kbdEventObj, false);
	jerry_release_value(kbdEventObj);
	jerry_release_value(eventArgs[0]);
	jerry_release_value(eventArgs[1]);
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