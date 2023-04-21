extern "C" {
#include <nds/system.h>
}

#include <nds/interrupts.h>
#include <nds/arm9/input.h>
#include <stdlib.h>
#include <string.h>

#include "event.hpp"
#include "util/helpers.hpp"



FUNCTION(DS_getBatteryLevel) {
	u32 level = getBatteryLevel();
	if (level & BIT(7)) return String("charging");
	level = level & 0xF;
	if (level == 0x1) return jerry_create_number(0);
	else if (level == 0x3) return jerry_create_number(1);
	else if (level == 0x7) return jerry_create_number(2);
	else if (level == 0xB) return jerry_create_number(3);
	else if (level == 0xF) return jerry_create_number(4);
	else return jerry_create_number_nan();
}

FUNCTION(DS_setMainScreen) {
	if (argCount > 0 && jerry_value_is_string(args[0])) {
		bool set = false;
		char *screen = rawString(args[0]);
		if (strcmp(screen, "top") == 0) {
			lcdMainOnTop();
			set = true;
		}
		else if (strcmp(screen, "bottom") == 0) {
			lcdMainOnBottom();
			set = true;
		}
		free(screen);
		if (set) return JS_UNDEFINED;
	}
	return TypeError("Invalid screen value");
}

FUNCTION(DS_sleep) {
	jerry_value_t sleepEvent = createEvent("sleep", true);
	bool canceled = dispatchEvent(ref_global, sleepEvent, true);
	jerry_release_value(sleepEvent);
	if (!canceled) {
		systemSleep();
		swiWaitForVBlank();
		swiWaitForVBlank(); // I know this is jank but it's the easiest solution to stop 'wake' from dispatching before the system sleeps
		jerry_value_t wakeEvent = createEvent("wake", false);
		dispatchEvent(ref_global, wakeEvent, true);
		jerry_release_value(wakeEvent);
	}
	return JS_UNDEFINED;
}

FUNCTION(DS_touchGetPosition) {
	if ((keysHeld() & KEY_TOUCH) == 0) {
		jerry_value_t positionObj = jerry_create_object();
		jerry_value_t NaN = jerry_create_number_nan();
		setProperty(positionObj, "x", NaN);
		setProperty(positionObj, "y", NaN);
		jerry_release_value(NaN);
		return positionObj;
	}
	touchPosition pos; touchRead(&pos);
	jerry_value_t positionObj = jerry_create_object();
	jerry_value_t xNum = jerry_create_number(pos.px);
	jerry_value_t yNum = jerry_create_number(pos.py);
	setProperty(positionObj, "x", xNum);
	setProperty(positionObj, "y", yNum);
	jerry_release_value(xNum);
	jerry_release_value(yNum);
	return positionObj;
}

void exposeSystemAPI(jerry_value_t global) {
	jerry_value_t DS = createObject(global, "DS");
	setMethod(DS, "getBatteryLevel", DS_getBatteryLevel);
	setMethod(DS, "getMainScreen", RETURN(String(REG_POWERCNT & POWER_SWAP_LCDS ? "top" : "bottom")));
	defReadonly(DS, "isDSiMode", jerry_create_boolean(isDSiMode()));
	setMethod(DS, "setMainScreen", DS_setMainScreen);
	setMethod(DS, "shutdown", VOID(systemShutDown()));
	setMethod(DS, "sleep", DS_sleep);
	setMethod(DS, "swapScreens", VOID(lcdSwap()));
	jerry_release_value(DS);

	jerry_value_t Profile = createObject(global, "Profile");
	defReadonly(Profile, "alarmHour", (double) PersonalData->alarmHour);
	defReadonly(Profile, "alarmMinute", (double) PersonalData->alarmMinute);
	defReadonly(Profile, "birthDay", (double) PersonalData->birthDay);
	defReadonly(Profile, "birthMonth", (double) PersonalData->birthMonth);
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
	defReadonly(Profile, "name", (char16_t *) PersonalData->name, PersonalData->nameLen);
	defReadonly(Profile, "message", (char16_t *) PersonalData->message, PersonalData->messageLen);
	#pragma GCC diagnostic pop
	const u16 themeColors[16] = {0xCE0C, 0x8137, 0x8C1F, 0xFE3F, 0x825F, 0x839E, 0x83F5, 0x83E0, 0x9E80, 0xC769, 0xFAE6, 0xF960, 0xC800, 0xE811, 0xF41A, 0xC81F};
	defReadonly(Profile, "color", (double) (PersonalData->theme < 16 ? themeColors[PersonalData->theme] : 0));
	defReadonly(Profile, "autoMode", jerry_create_boolean(PersonalData->autoMode));
	defReadonly(Profile, "gbaScreen", PersonalData->gbaScreen ? "bottom" : "top");
	const char languages[8][10] = {"日本語", "English", "Français", "Deutsch", "Italiano", "Español", "中文", "한국어"};
	defReadonly(Profile, "language", PersonalData->language < 8 ? languages[PersonalData->language] : "");
	jerry_release_value(Profile);

	jerry_value_t Button = createObject(global, "Button");
	jerry_value_t buttonObj;
	#define DEF_BUTTON_OBJECT(name, value) \
		buttonObj = createObject(Button, name); \
		defGetter(buttonObj, "pressed", RETURN(jerry_create_boolean(keysDown() & value))); \
		defGetter(buttonObj, "held", RETURN(jerry_create_boolean(keysHeld() & value))); \
		defGetter(buttonObj, "release", RETURN(jerry_create_boolean(keysUp() & value))); \
		jerry_release_value(buttonObj);
	FOR_BUTTONS(DEF_BUTTON_OBJECT);
	jerry_release_value(Button);

	jerry_value_t Touch = createObject(global, "Touch");
	defGetter(Touch, "start", RETURN(jerry_create_boolean(keysDown() & KEY_TOUCH)));
	defGetter(Touch, "active", RETURN(jerry_create_boolean(keysHeld() & KEY_TOUCH)));
	defGetter(Touch, "end", RETURN(jerry_create_boolean(keysUp() & KEY_TOUCH)));
	setMethod(Touch, "getPosition", DS_touchGetPosition);
	jerry_release_value(Touch);
}