#include "input.h"

#include <nds/arm9/input.h>
#include <stdlib.h>

#include "inline.h"
#include "tasks.h"



void buttonEvents(bool down) {
	u32 set = down ? keysDown() : keysUp();
	if (set) {
		jerry_value_t buttonEventConstructor = getProperty(ref_global, "ButtonEvent");
		jerry_value_t buttonStr = createString("button");
		jerry_value_t args[2] = {createString(down ? "buttondown" : "buttonup"), jerry_create_object()};
		jerry_release_value(jerry_set_property(args[1], buttonStr, null));
		jerry_value_t event = jerry_construct_object(buttonEventConstructor, args, 2);
		if (set & KEY_A) {
			jerry_value_t value = createString("A");
			jerry_set_internal_property(event, buttonStr, value);
			jerry_release_value(value);
			queueEvent(ref_global, event);
		}
		else if (set & KEY_B) {
			jerry_value_t value = createString("B");
			jerry_set_internal_property(event, buttonStr, value);
			jerry_release_value(value);
			queueEvent(ref_global, event);
		}
		else if (set & KEY_X) {
			jerry_value_t value = createString("X");
			jerry_set_internal_property(event, buttonStr, value);
			jerry_release_value(value);
			queueEvent(ref_global, event);
		}
		else if (set & KEY_Y) {
			jerry_value_t value = createString("Y");
			jerry_set_internal_property(event, buttonStr, value);
			jerry_release_value(value);
			queueEvent(ref_global, event);
		}
		else if (set & KEY_L) {
			jerry_value_t value = createString("L");
			jerry_set_internal_property(event, buttonStr, value);
			jerry_release_value(value);
			queueEvent(ref_global, event);
		}
		else if (set & KEY_R) {
			jerry_value_t value = createString("R");
			jerry_set_internal_property(event, buttonStr, value);
			jerry_release_value(value);
			queueEvent(ref_global, event);
		}
		else if (set & KEY_UP) {
			jerry_value_t value = createString("Up");
			jerry_set_internal_property(event, buttonStr, value);
			jerry_release_value(value);
			queueEvent(ref_global, event);
		}
		else if (set & KEY_DOWN) {
			jerry_value_t value = createString("Down");
			jerry_set_internal_property(event, buttonStr, value);
			jerry_release_value(value);
			queueEvent(ref_global, event);
		}
		else if (set & KEY_LEFT) {
			jerry_value_t value = createString("Left");
			jerry_set_internal_property(event, buttonStr, value);
			jerry_release_value(value);
			queueEvent(ref_global, event);
		}
		else if (set & KEY_RIGHT) {
			jerry_value_t value = createString("Right");
			jerry_set_internal_property(event, buttonStr, value);
			jerry_release_value(value);
			queueEvent(ref_global, event);
		}
		else if (set & KEY_START) {
			jerry_value_t value = createString("START");
			jerry_set_internal_property(event, buttonStr, value);
			jerry_release_value(value);
			queueEvent(ref_global, event);
		}
		else if (set & KEY_SELECT) {
			jerry_value_t value = createString("SELECT");
			jerry_set_internal_property(event, buttonStr, value);
			jerry_release_value(value);
			queueEvent(ref_global, event);
		}
		jerry_release_value(event);
		jerry_release_value(args[0]);
		jerry_release_value(args[1]);
		jerry_release_value(buttonStr);
		jerry_release_value(buttonEventConstructor);
	}
}

u16 prevX = 0, prevY = 0;
void queueStylusEvent(const char *name, int curX, int curY, bool usePrev) {
	jerry_value_t args[2] = {createString(name), jerry_create_object()};
	jerry_value_t x = jerry_create_number(curX);
	jerry_value_t y = jerry_create_number(curY);
	setProperty(args[1], "x", x);
	setProperty(args[1], "y", y);
	jerry_release_value(x);
	jerry_release_value(y);
	if (usePrev) {
		jerry_value_t dx = jerry_create_number(curX - (int) prevX);
		jerry_value_t dy = jerry_create_number(curY - (int) prevY);
		setProperty(args[1], "dx", dx);
		setProperty(args[1], "dy", dy);
		jerry_release_value(dx);
		jerry_release_value(dy);
	}
	jerry_value_t stylusEventConstructor = getProperty(ref_global, "StylusEvent");
	jerry_value_t event = jerry_construct_object(stylusEventConstructor, args, 2);
	queueEvent(ref_global, event);
	jerry_release_value(args[0]);
	jerry_release_value(args[1]);
	jerry_release_value(event);
	jerry_release_value(stylusEventConstructor);
}

void stylusEvents() {
	touchPosition pos;
	touchRead(&pos);
	if (keysDown() & KEY_TOUCH) queueStylusEvent("stylusdown", pos.px, pos.py, false);
	else if (keysHeld() & KEY_TOUCH) {
		if (prevX != pos.px || prevY != pos.py) queueStylusEvent("stylusmove", pos.px, pos.py, true);
	}
	else if (keysUp() & KEY_TOUCH) queueStylusEvent("stylusup", prevX, prevY, false);
	prevX = pos.px;
	prevY = pos.py;
}

bool dispatchKeyboardEvent(bool down, const char *key, const char *code, u8 location, bool shift, bool ctrl, bool alt, bool meta, bool caps) {
	jerry_value_t kbdEventArgs[2] = {createString(down ? "keydown" : "keyup"), jerry_create_object()};
	setProperty(kbdEventArgs[1], "cancelable", True);

	jerry_value_t keyStr = createString(key);
	jerry_value_t codeStr = createString(code);
	jerry_value_t locationNum = jerry_create_number(location);
	setProperty(kbdEventArgs[1], "key", keyStr);
	setProperty(kbdEventArgs[1], "code", codeStr);
	setProperty(kbdEventArgs[1], "location", locationNum);
	jerry_release_value(keyStr);
	jerry_release_value(codeStr);
	jerry_release_value(locationNum);

	setProperty(kbdEventArgs[1], "shiftKey", jerry_create_boolean(shift));
	setProperty(kbdEventArgs[1], "ctrlKey", jerry_create_boolean(ctrl));
	setProperty(kbdEventArgs[1], "altKey", jerry_create_boolean(alt));
	setProperty(kbdEventArgs[1], "metaKey", jerry_create_boolean(meta));
	setProperty(kbdEventArgs[1], "modifierAltGraph", jerry_create_boolean(ctrl && alt));
	setProperty(kbdEventArgs[1], "modifierCapsLock", jerry_create_boolean(caps));

	jerry_value_t keyboardEventConstructor = getProperty(ref_global, "KeyboardEvent");
	jerry_value_t kbdEvent = jerry_construct_object(keyboardEventConstructor, kbdEventArgs, 2);
	bool canceled = dispatchEvent(ref_global, kbdEvent, false);
	jerry_release_value(kbdEvent);
	jerry_release_value(keyboardEventConstructor);
	jerry_release_value(kbdEventArgs[0]);
	jerry_release_value(kbdEventArgs[1]);
	return canceled;
}