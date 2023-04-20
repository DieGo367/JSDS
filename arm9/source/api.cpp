#include "api.hpp"

#include <dirent.h>
#include <nds/arm9/input.h>
#include <nds/arm9/video.h>
#include <nds/arm9/background.h>
#include <nds/interrupts.h>
extern "C" {
#include <nds/system.h>
}
#include <nds/arm9/sprite.h>
#include <nds/arm9/trig_lut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "error.hpp"
#include "event.hpp"
#include "file.hpp"
#include "helpers.hpp"
#include "io/console.hpp"
#include "io/keyboard.hpp"
#include "jerry/jerryscript.h"
#include "logging.hpp"
#include "timeouts.hpp"
#include "util/color.hpp"
#include "util/tonccpy.h"
#include "util/unicode.hpp"



jerry_value_t ref_global;
JS_class ref_Event;
JS_class ref_Error;
JS_class ref_File;
JS_class ref_Sprite;
JS_class ref_PalettedSprite;
JS_class ref_BitmapSprite;
JS_class ref_SpriteGraphic;
JS_class ref_SpriteAffineMatrix;
jerry_value_t ref_consoleCounters;
jerry_value_t ref_consoleTimers;
jerry_value_t ref_storage;
jerry_value_t ref_func_push;
jerry_value_t ref_func_slice;
jerry_value_t ref_func_splice;
jerry_value_t ref_str_name;
jerry_value_t ref_str_constructor;
jerry_value_t ref_str_prototype;
jerry_value_t ref_str_backtrace;
jerry_value_t ref_str_main;
jerry_value_t ref_str_removed;
jerry_value_t ref_sym_toStringTag;

#define FOR_CUSTOM_SYMBOL_NAMES(DO) \
	DO(LCD); DO(ARM7_0); DO(ARM7_1); \
	DO(MAIN_BG); DO(MAIN_BG_0x4000); DO(MAIN_BG_0x10000); DO(MAIN_BG_0x14000); DO(MAIN_BG_0x20000); DO(MAIN_BG_0x40000); DO(MAIN_BG_0x60000); \
	DO(MAIN_SPRITE); DO(MAIN_SPRITE_0x4000); DO(MAIN_SPRITE_0x10000); DO(MAIN_SPRITE_0x14000); DO(MAIN_SPRITE_0x20000); \
	DO(MAIN_BG_EXT_PALETTE_0); DO(MAIN_BG_EXT_PALETTE_2); DO(MAIN_SPRITE_EXT_PALETTE); \
	DO(SUB_BG); DO(SUB_BG_0x8000); DO(SUB_SPRITE); DO(SUB_BG_EXT_PALETTE); DO(SUB_SPRITE_EXT_PALETTE); \
	DO(TEXTURE_0); DO(TEXTURE_1); DO(TEXTURE_2); DO(TEXTURE_3); \
	DO(TEXTURE_PALETTE_0); DO(TEXTURE_PALETTE_1); DO(TEXTURE_PALETTE_4); DO(TEXTURE_PALETTE_5);

#define DEFINE_SYMBOL(name) jerry_value_t symbol_##name
FOR_CUSTOM_SYMBOL_NAMES(DEFINE_SYMBOL)

#define BOUND(n, min, max) n < min ? min : n > max ? max : n

const char ONE_ARG[] = "1 argument required.";
const char WAS_REMOVED[] = "Using a previously removed object.";

#define REQUIRE_FIRST() if (argCount == 0) return TypeError(ONE_ARG)
#define REQUIRE(n) if (argCount < n) return TypeError(#n " arguments required.")
#define EXPECT(test, type) if (!(test)) return TypeError("Expected type '" #type "'.")
#define CONSTRUCTOR(name) if (isNewTargetUndefined()) return TypeError("Constructor '" #name "' cannot be invoked without 'new'.")
#define NOT_REMOVED(obj) if (JS_testInternalProperty(obj, ref_str_removed)) return TypeError(WAS_REMOVED)

u8 spriteUsage[SPRITE_COUNT] = {0};
#define USAGE_SPRITE_MAIN BIT(0)
#define USAGE_MATRIX_MAIN BIT(1)
#define USAGE_SPRITE_SUB BIT(2)
#define USAGE_MATRIX_SUB BIT(3)
#define SPRITE_ENGINE(obj) (JS_testInternalProperty(obj, ref_str_main) ? &oamMain : &oamSub)
#define SPRITE_ENTRY(obj) (SPRITE_ENGINE(obj)->oamMemory + getID(obj))

FUNCTION(IllegalConstructor) {
	return TypeError("Illegal constructor");
}

FUNCTION(closeJSDS) {
	abortFlag = true;
	userClosed = true;
	return jerry_create_abort_from_value(String(""), true);
}

FUNCTION(alert) {
	if (argCount > 0) printValue(args[0]);
	else printf("Alert");
	printf(" [ OK]\n");
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_A) break;
	}
	return JS_UNDEFINED;
}

FUNCTION(confirm) {
	if (argCount > 0) printValue(args[0]);
	else printf("Confirm");
	printf(" [ OK,  Cancel]\n");
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		u32 keys = keysDown();
		if (keys & KEY_A) return JS_TRUE;
		else if (keys & KEY_B) return JS_FALSE;
	}
}

FUNCTION(prompt) {
	if (argCount > 0) printValue(args[0]);
	else printf("Prompt");
	putchar(' ');
	pauseKeyEvents = true;
	bool hadButtonControls = keyboardButtonControls(true);
	keyboardCompose(true);
	ComposeStatus status = keyboardComposeStatus();
	while (status == KEYBOARD_COMPOSING) {
		swiWaitForVBlank();
		scanKeys();
		keyboardUpdate();
		status = keyboardComposeStatus();
	}
	if (status == KEYBOARD_FINISHED) {
		char *response;
		u32 responseSize;
		keyboardComposeAccept(&response, &responseSize);
		printf(response); putchar('\n');
		jerry_value_t responseStr = StringSized(response, responseSize);
		free(response);
		keyboardUpdate();
		pauseKeyEvents = false;
		keyboardButtonControls(hadButtonControls);
		return responseStr;
	}
	else {
		putchar('\n');
		keyboardUpdate();
		pauseKeyEvents = false;
		keyboardButtonControls(hadButtonControls);
		if (argCount > 1) return jerry_value_to_string(args[1]);
		else return JS_NULL;
	}
}

FUNCTION(setTimeout) {
	if (argCount >= 2) {
		jerry_value_t ticksNum = jerry_value_to_number(args[1]);
		int ticks = jerry_value_as_int32(ticksNum);
		jerry_release_value(ticksNum);
		return addTimeout(args[0], args + 2, argCount - 2, ticks, false);
	}
	else return addTimeout(argCount > 0 ? args[0] : JS_UNDEFINED, NULL, 0, 0, false);
}

FUNCTION(setInterval) {
	if (argCount >= 2) {
		jerry_value_t ticksNum = jerry_value_to_number(args[1]);
		int ticks = jerry_value_as_int32(ticksNum);
		jerry_release_value(ticksNum);
		return addTimeout(args[0], args + 2, argCount - 2, ticks, true);
	}
	else return addTimeout(argCount > 0 ? args[0] : JS_UNDEFINED, NULL, 0, 0, true);
}

FUNCTION(clearInterval) {
	if (argCount > 0) clearTimeout(args[0]);
	else clearTimeout(JS_UNDEFINED);
	return JS_UNDEFINED;
}

FUNCTION(console_log) {
	if (argCount > 0) {
		logIndent();
		log(args, argCount);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_info) {
	if (argCount > 0) {
		u16 previousColor = consoleSetColor(LOGCOLOR_INFO);
		logIndent();
		log(args, argCount);
		consoleSetColor(previousColor);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_warn) {
	if (argCount > 0) {
		u16 previousColor = consoleSetColor(LOGCOLOR_WARN);
		u16 previousBG = consoleSetBackground(LOGCOLOR_WARN_BG);
		logIndent();
		log(args, argCount);
		consoleSetColor(previousColor);
		consoleSetBackground(previousBG);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_error) {
	if (argCount > 0) {
		u16 previousColor = consoleSetColor(LOGCOLOR_ERROR);
		u16 previousBG = consoleSetBackground(LOGCOLOR_ERROR_BG);
		logIndent();
		log(args, argCount);
		consoleSetColor(previousColor);
		consoleSetBackground(previousBG);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_assert) {
	if (argCount == 0 || !jerry_value_to_boolean(args[0])) {
		u16 previousColor = consoleSetColor(LOGCOLOR_ERROR);
		u16 previousBG = consoleSetBackground(LOGCOLOR_ERROR_BG);
		logIndent();
		printf("Assertion failed: ");
		log(args + 1, argCount - 1);
		consoleSetColor(previousColor);
		consoleSetBackground(previousBG);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_debug) {
	if (argCount > 0) {
		u16 previousColor = consoleSetColor(LOGCOLOR_DEBUG);
		logIndent();
		log(args, argCount);
		consoleSetColor(previousColor);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_trace) {
	logIndent();
	if (argCount == 0) printf("Trace\n");
	else log(args, argCount);
	jerry_value_t backtraceArr = jerry_get_backtrace(10);
	u32 length = jerry_get_array_length(backtraceArr);
	for (u32 i = 0; i < length; i++) {
		jerry_value_t lineStr = jerry_get_property_by_index(backtraceArr, i);
		char *line = getString(lineStr);
		logIndent();
		printf(" @ %s\n", line);
		free(line);
		jerry_release_value(lineStr);
	}
	jerry_release_value(backtraceArr);
	return JS_UNDEFINED;
}

FUNCTION(console_dir) {
	consolePause();
	if (argCount > 0) {
		logIndent();
		if (jerry_value_is_object(args[0])) logObject(args[0]);
		else logLiteral(args[0]);
		putchar('\n');
	}
	consoleResume();
	return JS_UNDEFINED;
}

FUNCTION(console_table) {
	if (argCount > 0) logTable(args, argCount);
	return JS_UNDEFINED;
}

FUNCTION(console_group) {
	logIndentAdd();
	if (argCount > 0) {
		logIndent();
		log(args, argCount);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_groupEnd) {
	logIndentRemove();
	return JS_UNDEFINED;
}

FUNCTION(console_count) {
	jerry_value_t labelStr;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		labelStr = jerry_value_to_string(args[0]);
	}
	else labelStr = String("default");
	
	jerry_value_t countNum = jerry_get_property(ref_consoleCounters, labelStr);
	u32 count;
	if (jerry_value_is_undefined(labelStr)) count = 1;
	else count = jerry_value_as_uint32(countNum) + 1;
	jerry_release_value(countNum);

	logIndent();
	printString(labelStr);
	printf(": %lu\n", count);
	
	countNum = jerry_create_number(count);
	jerry_release_value(jerry_set_property(ref_consoleCounters, labelStr, countNum));
	jerry_release_value(countNum);

	jerry_release_value(labelStr);
	return JS_UNDEFINED;
}

FUNCTION(console_countReset) {
	jerry_value_t labelStr;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		labelStr = jerry_value_to_string(args[0]);
	}
	else labelStr = String("default");

	jerry_value_t hasLabelBool = jerry_has_own_property(ref_consoleCounters, labelStr);
	if (jerry_get_boolean_value(hasLabelBool)) {
		jerry_value_t zeroNum = jerry_create_number(0);
		jerry_set_property(ref_consoleCounters, labelStr, zeroNum);
		jerry_release_value(zeroNum);
	}
	else {
		logIndent();
		printf("Count for '");
		printString(labelStr);
		printf("' does not exist\n");
	}
	jerry_release_value(hasLabelBool);

	jerry_release_value(labelStr);
	return JS_UNDEFINED;
}

FUNCTION(console_time) {
	jerry_value_t labelStr;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		labelStr = jerry_value_to_string(args[0]);
	}
	else labelStr = String("default");

	jerry_value_t hasLabelBool = jerry_has_own_property(ref_consoleTimers, labelStr);
	if (jerry_get_boolean_value(hasLabelBool)) {
		logIndent();
		printf("Timer '");
		printString(labelStr);
		printf("' already exists\n");
	}
	else {
		jerry_value_t counterIdNum = jerry_create_number(counterAdd());
		jerry_release_value(jerry_set_property(ref_consoleTimers, labelStr, counterIdNum));
		jerry_release_value(counterIdNum);
	}
	jerry_release_value(hasLabelBool);
	
	jerry_release_value(labelStr);
	return JS_UNDEFINED;
}

FUNCTION(console_timeLog) {
	jerry_value_t labelStr;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		labelStr = jerry_value_to_string(args[0]);
	}
	else labelStr = String("default");

	logIndent();
	jerry_value_t counterIdNum = jerry_get_property(ref_consoleTimers, labelStr);
	if (jerry_value_is_undefined(counterIdNum)) {
		printf("Timer '");
		printString(labelStr);
		printf("' does not exist\n");
	}
	else {
		int counterId = jerry_value_as_int32(counterIdNum);
		printString(labelStr);
		printf(": %i ms", counterGet(counterId));
		if (argCount > 1) {
			putchar(' ');
			log(args + 1, argCount - 1);
		}
	}
	jerry_release_value(counterIdNum);

	jerry_release_value(labelStr);
	return JS_UNDEFINED;
}

FUNCTION(console_timeEnd) {
	jerry_value_t labelStr;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		labelStr = jerry_value_to_string(args[0]);
	}
	else labelStr = String("default");

	logIndent();
	jerry_value_t counterIdNum = jerry_get_property(ref_consoleTimers, labelStr);
	if (jerry_value_is_undefined(counterIdNum)) {
		printf("Timer '");
		printString(labelStr);
		printf("' does not exist\n");
	}
	else {
		int counterId = jerry_value_as_int32(counterIdNum);
		printString(labelStr);
		printf(": %i ms\n", counterGet(counterId));
		counterRemove(counterId);
		jerry_delete_property(ref_consoleTimers, labelStr);
	}
	jerry_release_value(counterIdNum);

	jerry_release_value(labelStr);
	return JS_UNDEFINED;
}

FUNCTION(console_clear) {
	consoleClear();
	return JS_UNDEFINED;
}

FUNCTION(console_textColor) {
	char *colorDesc = getAsString(args[0]);
	u16 color = colorParse(colorDesc, consoleGetColor());
	free(colorDesc);
	consoleSetColor(color);
	return JS_UNDEFINED;
}

FUNCTION(console_textBackground) {
	char *colorDesc = getAsString(args[0]);
	u16 color = colorParse(colorDesc, consoleGetBackground());
	free(colorDesc);
	consoleSetBackground(color);
	return JS_UNDEFINED;
}

FUNCTION(Text_encode) {
	REQUIRE_FIRST();
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) {
		EXPECT(jerry_get_typedarray_type(args[1]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
		jerry_value_t textStr = jerry_value_to_string(args[0]);
		jerry_size_t textSize = jerry_get_utf8_string_size(textStr);
		jerry_length_t byteOffset, bufLen;
		jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[1], &byteOffset, &bufLen);
		u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
		jerry_release_value(arrayBuffer);
		if (textSize > bufLen) {
			jerry_release_value(textStr);
			return TypeError("Text size is too big to encode into the given array.");
		}
		jerry_string_to_utf8_char_buffer(textStr, data, textSize);
		jerry_release_value(textStr);
		return jerry_acquire_value(args[1]);
	}
	jerry_value_t textStr = jerry_value_to_string(args[0]);
	jerry_size_t textSize = jerry_get_utf8_string_size(textStr);
	jerry_value_t arrayBuffer = jerry_create_arraybuffer(textSize);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer);
	jerry_string_to_utf8_char_buffer(textStr, data, textSize);
	jerry_value_t u8Array = jerry_create_typedarray_for_arraybuffer(JERRY_TYPEDARRAY_UINT8, arrayBuffer);
	jerry_release_value(textStr);
	jerry_release_value(arrayBuffer);
	return u8Array;
}

FUNCTION(Text_decode) {
	REQUIRE_FIRST(); EXPECT(jerry_get_typedarray_type(args[0]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	jerry_length_t byteOffset = 0, dataLen = 0;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &byteOffset, &dataLen);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
	jerry_release_value(arrayBuffer);
	if (!jerry_is_valid_utf8_string(data, dataLen)) return TypeError("Invalid UTF-8");
	return StringSized(data, dataLen);
}

FUNCTION(Text_encodeUTF16) {
	REQUIRE_FIRST();
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) {
		EXPECT(jerry_get_typedarray_type(args[1]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	}
	jerry_size_t utf8Size;
	char *utf8 = getAsString(args[0], &utf8Size);
	jerry_size_t utf16Length;
	char16_t *utf16 = UTF8toUTF16(utf8, utf8Size, &utf16Length);
	free(utf8);
	jerry_size_t utf16Size = utf16Length * 2;
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) {
		jerry_length_t byteOffset, bufSize;
		jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[1], &byteOffset, &bufSize);
		u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
		jerry_release_value(arrayBuffer);
		if (utf16Size > bufSize) {
			free(utf16);
			return TypeError("Text size is too big to encode into the given array.");
		}
		tonccpy(data, utf16, utf16Size);
		return jerry_acquire_value(args[1]);
	}
	jerry_value_t arrayBuffer = jerry_create_arraybuffer(utf16Size);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer);
	tonccpy(data, utf16, utf16Size);
	free(utf16);
	jerry_value_t u8Array = jerry_create_typedarray_for_arraybuffer(JERRY_TYPEDARRAY_UINT8, arrayBuffer);
	jerry_release_value(arrayBuffer);
	return u8Array;
}

FUNCTION(Text_decodeUTF16) {
	REQUIRE_FIRST(); EXPECT(jerry_get_typedarray_type(args[0]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	jerry_length_t byteOffset = 0, dataLen = 0;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &byteOffset, &dataLen);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
	jerry_release_value(arrayBuffer);
	return StringUTF16((char16_t *) data, dataLen / 2);
}

const char b64Map[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
FUNCTION(Base64_encode) {
	REQUIRE_FIRST(); EXPECT(jerry_get_typedarray_type(args[0]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	jerry_length_t byteOffset, dataSize;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &byteOffset, &dataSize);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
	jerry_release_value(arrayBuffer);

	const jerry_size_t asciiSize = (dataSize + 2) / 3 * 4;
	char ascii[asciiSize]; // base64 needs 4 chars to encode 3 bytes
	char *out = ascii;
	jerry_size_t i = 2;
	for (; i < dataSize; i += 3) {
		*(out++) = b64Map[data[i - 2] >> 2];
		*(out++) = b64Map[(data[i - 2] & 0b11) << 4 | data[i - 1] >> 4];
		*(out++) = b64Map[(data[i - 1] & 0x0F) << 2 | data[i] >> 6];
		*(out++) = b64Map[data[i] & 0b00111111];
	}
	if (i == dataSize) {
		*(out++) = b64Map[data[i - 2] >> 2];
		*(out++) = b64Map[(data[i - 2] & 0b11) << 4 | data[i - 1] >> 4];
		*(out++) = b64Map[(data[i - 1] & 0x0F) << 2];
		*(out++) = '=';
	}
	else if (i == dataSize + 1) {
		*(out++) = b64Map[data[i - 2] >> 2];
		*(out++) = b64Map[(data[i - 2] & 0b11) << 4];
		*(out++) = '=';
		*(out++) = '=';
	}

	return StringSized(ascii, asciiSize);
}

static inline u8 b64CharValue(char ch, bool *bad) {
	if (ch >= 'A' && ch <= 'Z') return ch - 'A';
	if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
	if (ch >= '0' && ch <= '9') return ch - '0' + 52;
	if (ch == '+') return 62;
	if (ch == '/') return 63;
	*bad = true;
	return 0;
}
FUNCTION(Base64_decode) {
	REQUIRE_FIRST(); EXPECT(jerry_value_is_string(args[0]), string);
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) {
		EXPECT(jerry_get_typedarray_type(args[1]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	}
	jerry_size_t asciiSize;
	char *ascii = getString(args[0], &asciiSize);
	const char errorMsg[] = "Unable to decode Base64.";

	if (asciiSize % 4 == 1) {
		free(ascii);
		return TypeError(errorMsg);
	}
	jerry_length_t dataLen = asciiSize / 4 * 3;
	if (ascii[asciiSize - 1] == '=') dataLen--;
	if (ascii[asciiSize - 2] == '=') dataLen--;

	u8 data[dataLen];
	u8 *out = data;
	jerry_length_t asciiIdx = 0, dataIdx = 2;
	bool badEncoding = false;
	for (; dataIdx < dataLen; dataIdx += 3) {
		u8 first = b64CharValue(ascii[asciiIdx++], &badEncoding);
		u8 second = b64CharValue(ascii[asciiIdx++], &badEncoding);
		u8 third = b64CharValue(ascii[asciiIdx++], &badEncoding);
		u8 fourth = b64CharValue(ascii[asciiIdx++], &badEncoding);
		if (badEncoding) break;
		*(out++) = first << 2 | second >> 4;
		*(out++) = (second & 0xF) << 4 | third >> 2;
		*(out++) = (third & 0b11) << 6 | fourth;
	}
	if (dataIdx == dataLen) {
		u8 first = b64CharValue(ascii[asciiIdx++], &badEncoding);
		u8 second = b64CharValue(ascii[asciiIdx++], &badEncoding);
		u8 third = b64CharValue(ascii[asciiIdx++], &badEncoding);
		*(out++) = first << 2 | second >> 4;
		*(out++) = (second & 0xF) << 4 | third >> 2;
	}
	else if (dataIdx == dataLen + 1) {
		u8 first = b64CharValue(ascii[asciiIdx++], &badEncoding);
		u8 second = b64CharValue(ascii[asciiIdx++], &badEncoding);
		*(out++) = first << 2 | second >> 4;
	}

	free(ascii);
	if (badEncoding) return TypeError(errorMsg);

	jerry_value_t u8Array;
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) u8Array = jerry_acquire_value(args[1]);
	else u8Array = jerry_create_typedarray(JERRY_TYPEDARRAY_UINT8, dataLen);
	jerry_length_t byteOffset, arraySize;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(u8Array, &byteOffset, &arraySize);
	if (arraySize < dataLen) {
		jerry_release_value(arrayBuffer);
		jerry_release_value(u8Array);
		return TypeError("Data size is too big to decode into the given array.");
	}
	jerry_arraybuffer_write(arrayBuffer, byteOffset, data, dataLen);
	jerry_release_value(arrayBuffer);
	return u8Array;
}

FUNCTION(File_read) {
	REQUIRE_FIRST();

	jerry_value_t modeStr = getInternalProperty(thisValue, "mode");
	char *mode = getString(modeStr);
	if (mode[0] != 'r' && mode[1] != '+') {
		free(mode);
		jerry_release_value(modeStr);
		return Error("Unable to read in current file mode.");
	}
	free(mode);
	jerry_release_value(modeStr);
	
	jerry_length_t bytesToRead = jerry_value_as_uint32(args[0]);
	jerry_value_t arrayBuffer = jerry_create_arraybuffer(bytesToRead);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer);
	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	u32 bytesRead = fread(buf, 1, bytesToRead, file);
	if (ferror(file)) {
		jerry_release_value(arrayBuffer);
		return Error("File read failed.");
	}
	else if (feof(file) && bytesRead == 0) {
		jerry_release_value(arrayBuffer);
		return JS_NULL;
	}
	else {
		jerry_value_t u8Array = jerry_create_typedarray_for_arraybuffer_sz(JERRY_TYPEDARRAY_UINT8, arrayBuffer, 0, bytesRead);
		jerry_release_value(arrayBuffer);
		return u8Array;
	}
}

FUNCTION(File_write) {
	REQUIRE_FIRST();
	EXPECT(jerry_get_typedarray_type(args[0]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);

	jerry_value_t modeStr = getInternalProperty(thisValue, "mode");
	char *mode = getString(modeStr);
	if (mode[0] != 'w' && mode[0] != 'a' && mode[1] != '+') {
		free(mode);
		jerry_release_value(modeStr);
		return Error("Unable to write in current file mode.");
	}
	free(mode);
	jerry_release_value(modeStr);
	
	jerry_length_t offset, bufSize;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &offset, &bufSize);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer) + offset;
	jerry_release_value(arrayBuffer);
	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);

	u32 bytesWritten = fwrite(buf, 1, bufSize, file);
	if (ferror(file)) {
		return Error("File write failed.");
	}
	else return jerry_create_number(bytesWritten);
}

FUNCTION(File_seek) {
	REQUIRE_FIRST();

	int mode = SEEK_SET;
	if (argCount > 1) {
		char *seekMode = getAsString(args[1]);
		if (strcmp(seekMode, "start") == 0) mode = SEEK_SET;
		else if (strcmp(seekMode, "current") == 0) mode = SEEK_CUR;
		else if (strcmp(seekMode, "end") == 0) mode = SEEK_END;
		else mode = 10;
		free(seekMode);
	}
	if (mode == 10) return TypeError("Invalid seek mode");

	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	int success = fseek(file, jerry_value_as_int32(args[0]), mode);
	if (success != 0) return Error("File seek failed.");
	return JS_UNDEFINED;
}

FUNCTION(File_close) {
	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	if (fclose(file) != 0) return Error("File close failed.");
	return JS_UNDEFINED;
}

FUNCTION(File_static_open) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	char defaultMode[] = "r";
	char *mode = defaultMode;
	if (argCount > 1) {
		mode = getAsString(args[1]);
		if (strcmp(mode, "r") != 0 && strcmp(mode, "r+") != 0
		 && strcmp(mode, "w") != 0 && strcmp(mode, "w+") != 0
		 && strcmp(mode, "a") != 0 && strcmp(mode, "a+") != 0
		) {
			free(mode);
			free(path);
			return TypeError("Invalid file mode");
		}
	}

	FILE *file = fopen(path, mode);
	if (file == NULL) {
		if (mode != defaultMode) free(mode);
		free(path);
		return Error("Unable to open file.");
	}
	else {
		jerry_value_t modeStr = String(mode);
		jerry_value_t fileObj = newFile(file, modeStr);
		jerry_release_value(modeStr);
		if (mode != defaultMode) free(mode);
		free(path);
		return fileObj;
	}
}

FUNCTION(File_static_copy) {
	REQUIRE(2);
	char *sourcePath = getAsString(args[0]);
	FILE *source = fopen(sourcePath, "r");
	free(sourcePath);
	if (source == NULL) return Error("Unable to open source file during copy.");
	
	fseek(source, 0, SEEK_END);
	u32 sourceSize = ftell(source);
	rewind(source);
	u8 *data = (u8 *) malloc(sourceSize);
	u32 bytesRead = fread(data, 1, sourceSize, source);
	if (ferror(source)) {
		free(data);
		fclose(source);
		return Error("Failed to read source file during copy.");
	}
	fclose(source);

	char *destPath = getAsString(args[1]);
	FILE *dest = fopen(destPath, "w");
	free(destPath);
	if (dest == NULL) {
		free(data);
		return Error("Unable to open destination file during copy.");
	}

	fwrite(data, 1, bytesRead, dest);
	free(data);
	if (ferror(dest)) {
		fclose(dest);
		return Error("Failed to write destination file during copy.");
	}
	fclose(dest);
	return JS_UNDEFINED;
}

FUNCTION(File_static_rename) {
	REQUIRE(2);
	char *sourcePath = getAsString(args[0]);
	char *destPath = getAsString(args[1]);
	int status = rename(sourcePath, destPath);
	free(sourcePath);
	free(destPath);
	if (status != 0) return Error("Failed to rename file.");
	return JS_UNDEFINED;
}

FUNCTION(File_static_remove) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	if (remove(path) != 0) return Error("Failed to delete file.");
	return JS_UNDEFINED;
}

FUNCTION(File_static_read) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "r");
	free(path);
	if (file == NULL) return Error("Unable to open file.");

	fseek(file, 0, SEEK_END);
	u32 fileSize = ftell(file);
	rewind(file);

	jerry_value_t arrayBuffer = jerry_create_arraybuffer(fileSize);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer);
	u32 bytesRead = fread(buf, 1, fileSize, file);
	if (ferror(file)) {
		jerry_release_value(arrayBuffer);
		fclose(file);
		return Error("File read failed.");
	}
	fclose(file);
	jerry_value_t u8Array = jerry_create_typedarray_for_arraybuffer_sz(JERRY_TYPEDARRAY_UINT8, arrayBuffer, 0, bytesRead);
	jerry_release_value(arrayBuffer);
	return u8Array;
}

FUNCTION(File_static_readText) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "r");
	free(path);
	if (file == NULL) return Error("Unable to open file.");

	fseek(file, 0, SEEK_END);
	u32 fileSize = ftell(file);
	rewind(file);

	char *buf = (char *) malloc(fileSize);
	u32 bytesRead = fread(buf, 1, fileSize, file);
	if (ferror(file)) {
		free(buf);
		fclose(file);
		return Error("File read failed.");
	}
	fclose(file);
	jerry_value_t str = StringSized(buf, bytesRead);
	free(buf);
	return str;
}

FUNCTION(File_static_write) {
	REQUIRE(2);
	EXPECT(jerry_get_typedarray_type(args[1]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "w");
	free(path);
	if (file == NULL) return Error("Unable to open file.");
	
	jerry_length_t offset, bufSize;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[1], &offset, &bufSize);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer) + offset;
	jerry_release_value(arrayBuffer);

	u32 bytesWritten = fwrite(buf, 1, bufSize, file);
	if (ferror(file)) {
		fclose(file);
		return Error("File write failed.");
	}
	fclose(file);
	return jerry_create_number(bytesWritten);
}

FUNCTION(File_static_writeText) {
	REQUIRE(2);
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "w");
	free(path);
	if (file == NULL) return Error("Unable to open file.");
	
	jerry_size_t textSize;
	char *text = getAsString(args[1], &textSize);
	u32 bytesWritten = fwrite(text, 1, textSize, file);
	if (ferror(file)) {
		fclose(file);
		free(text);
		return Error("File write failed.");
	}
	fclose(file);
	free(text);
	return jerry_create_number(bytesWritten);
}

FUNCTION(File_static_makeDir) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	int status = -1;
	if (argCount > 1 && jerry_value_to_boolean(args[1])) {
		char *slash = strchr(path, '/');
		if (strchr(path, ':') != NULL || path == slash) slash = strchr(slash + 1, '/');
		while (slash != NULL) {
			slash[0] = '\0';
			mkdir(path, 0777);
			slash[0] = '/';
			slash = strchr(slash + 1, '/');
		}
		status = access(path, F_OK);
	}
	else status = mkdir(path, 0777);
	free(path);
	if (status != 0) return Error("Failed to make directory.");
	return JS_UNDEFINED;
}

FUNCTION(File_static_readDir) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	DIR *dir = opendir(path);
	free(path);
	if (dir == NULL) return Error("Unable to open directory.");

	jerry_value_t dirArr = jerry_create_array(0);
	dirent *entry = readdir(dir);
	while (entry != NULL) {
		jerry_value_t entryObj = jerry_create_object();
		setProperty(entryObj, "isDirectory", jerry_create_boolean(entry->d_type == DT_DIR));
		setProperty(entryObj, "isFile", jerry_create_boolean(entry->d_type == DT_REG));
		setStringProperty(entryObj, "name", entry->d_name);
		jerry_call_function(ref_func_push, dirArr, &entryObj, 1);
		jerry_release_value(entryObj);
		entry = readdir(dir);
	}
	closedir(dir);
	return dirArr;
}

FUNCTION(File_static_browse) {
	char browsePathDefault[] = ".";
	char messageDefault[] = "Select a file.";
	char *browsePath = browsePathDefault;
	char *message = messageDefault;
	std::vector<char *> extensions;
	if (argCount > 0 && jerry_value_is_object(args[0])) {
		jerry_value_t pathProp = String("path");
		jerry_value_t extensionsProp = String("extensions");
		jerry_value_t messageProp = String("message");
		if (jerry_has_property(args[0], pathProp)) {
			jerry_value_t pathVal = jerry_get_property(args[0], pathProp);
			if (pathVal != JS_UNDEFINED) browsePath = getAsString(pathVal);
			jerry_release_value(pathVal);
		}
		if (jerry_has_property(args[0], extensionsProp)) {
			jerry_value_t extensionsArr = jerry_get_property(args[0], extensionsProp);
			if (jerry_value_is_array(extensionsArr)) {
				u32 length = jerry_get_array_length(extensionsArr);
				for (u32 i = 0; i < length; i++) {
					jerry_value_t extVal = jerry_get_property_by_index(extensionsArr, i);
					extensions.emplace_back(getAsString(extVal));
					jerry_release_value(extVal);
				}
			}
			jerry_release_value(extensionsArr);
		}
		if (jerry_has_property(args[0], messageProp)) {
			jerry_value_t messageVal = jerry_get_property(args[0], messageProp);
			if (messageVal != JS_UNDEFINED) message = getAsString(messageVal);
			jerry_release_value(messageVal);
		}
		jerry_release_value(messageProp);
		jerry_release_value(extensionsProp);
		jerry_release_value(pathProp);
	}
	char *result = fileBrowse(consoleGetFont(), message, browsePath, extensions);
	jerry_value_t resultVal = result == NULL ? JS_NULL : String(result);
	free(result);
	for (u32 i = 0; i < extensions.size(); i++) free(extensions[i]);
	if (message != messageDefault) free(message);
	if (browsePath != browsePathDefault) free(browsePath);
	return resultVal;
}

FUNCTION(storage_length) {
	jerry_value_t keysArr = jerry_get_object_keys(ref_storage);
	u32 length = jerry_get_array_length(keysArr);
	jerry_release_value(keysArr);
	return jerry_create_number(length);
}

FUNCTION(storage_key) {
	REQUIRE_FIRST();
	jerry_value_t keyVal = JS_NULL;
	jerry_value_t keysArr = jerry_get_object_keys(ref_storage);
	jerry_value_t nNum = jerry_value_to_number(args[0]);
	u32 n = jerry_value_as_uint32(nNum);
	if (n < jerry_get_array_length(keysArr)) {
		keyVal = jerry_get_property_by_index(keysArr, n);
	}
	jerry_release_value(nNum);
	jerry_release_value(keysArr);
	return keyVal;
}

FUNCTION(storage_getItem) {
	REQUIRE_FIRST();
	jerry_value_t key = jerry_value_to_string(args[0]);
	jerry_value_t value = jerry_get_property(ref_storage, args[0]);
	jerry_release_value(key);
	return jerry_value_is_undefined(value) ? JS_NULL : value;
}

FUNCTION(storage_setItem) {
	REQUIRE(2);
	jerry_value_t key = jerry_value_to_string(args[0]);
	jerry_value_t value = jerry_value_to_string(args[1]);
	jerry_set_property(ref_storage, key, value);
	jerry_release_value(value);
	jerry_release_value(key);
	return JS_UNDEFINED;
}

FUNCTION(storage_removeItem) {
	REQUIRE_FIRST();
	jerry_value_t key = jerry_value_to_string(args[0]);
	jerry_value_t hasOwnBool = jerry_has_own_property(ref_storage, key);
	if (jerry_get_boolean_value(hasOwnBool)) {
		jerry_delete_property(ref_storage, key);
	}
	jerry_release_value(hasOwnBool);
	jerry_release_value(key);
	return JS_UNDEFINED;
}

FUNCTION(storage_clear) {
	jerry_release_value(ref_storage);
	ref_storage = jerry_create_object();
	return JS_UNDEFINED;
}

FUNCTION(storage_save) {
	return jerry_create_boolean(storageSave());
}

FUNCTION(EventConstructor) {
	CONSTRUCTOR(Event); REQUIRE_FIRST();

	setInternalProperty(thisValue, "stopImmediatePropagation", JS_FALSE); // stop immediate propagation flag
	setReadonly(thisValue, "target", JS_NULL);
	setReadonly(thisValue, "cancelable", JS_FALSE);
	setReadonly(thisValue, "defaultPrevented", JS_FALSE);                 // canceled flag
	jerry_value_t currentTimeNum = jerry_create_number(time(NULL));
	setReadonly(thisValue, "timeStamp", currentTimeNum);
	jerry_release_value(currentTimeNum);
	jerry_value_t typeStr = jerry_value_to_string(args[0]);	
	setReadonly(thisValue, "type", typeStr);
	jerry_release_value(typeStr);

	if (argCount > 1 && jerry_value_is_object(args[1])) {
		jerry_value_t keysArr = jerry_get_object_keys(args[1]);
		u32 length = jerry_get_array_length(keysArr);
		for (u32 i = 0; i < length; i++) {
			jerry_value_t key = jerry_get_property_by_index(keysArr, i);
			jerry_value_t value = jerry_get_property(args[1], key);
			JS_setReadonly(thisValue, key, value);
			jerry_release_value(value);
			jerry_release_value(key);
		}
		jerry_release_value(keysArr);
	}

	return JS_UNDEFINED;
}

FUNCTION(Event_stopImmediatePropagation) {
	setInternalProperty(thisValue, "stopImmediatePropagation", JS_TRUE);
	return JS_UNDEFINED;
}

FUNCTION(Event_preventDefault) {
	if (testInternalProperty(thisValue, "cancelable")) {
		setInternalProperty(thisValue, "defaultPrevented", JS_TRUE);
	}
	return JS_UNDEFINED;
}

FUNCTION(EventTargetConstructor) {
	if (thisValue != ref_global) CONSTRUCTOR(EventTarget);

	jerry_value_t eventListenersObj = jerry_create_object();
	setInternalProperty(thisValue, "eventListeners", eventListenersObj);
	jerry_release_value(eventListenersObj);

	return JS_UNDEFINED;
}

FUNCTION(EventTarget_addEventListener) {
	REQUIRE(2);
	if (jerry_value_is_null(args[1])) return JS_UNDEFINED;
	jerry_value_t targetObj = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	
	jerry_value_t callbackProp = String("callback");
	jerry_value_t onceProp = String("once");

	jerry_value_t typeStr = jerry_value_to_string(args[0]);
	jerry_value_t callbackVal = args[1];
	bool once = false;
	if (argCount > 2 && jerry_value_is_object(args[2])) {
		once = JS_testProperty(args[2], onceProp);
	}

	jerry_value_t eventListenersObj = getInternalProperty(targetObj, "eventListeners");
	jerry_value_t listenersArr = jerry_get_property(eventListenersObj, typeStr); // listeners of the given type
	if (jerry_value_is_undefined(listenersArr)) {
		listenersArr = jerry_create_array(0);
		jerry_release_value(jerry_set_property(eventListenersObj, typeStr, listenersArr));
	}
	jerry_release_value(eventListenersObj);

	u32 length = jerry_get_array_length(listenersArr);
	bool shouldAppend = true;
	for (u32 i = 0; shouldAppend && i < length; i++) {
		jerry_value_t storedListenerObj = jerry_get_property_by_index(listenersArr, i);
		jerry_value_t storedCallbackVal = jerry_get_property(storedListenerObj, callbackProp);
		if (strictEqual(callbackVal, storedCallbackVal)) shouldAppend = false;
		jerry_release_value(storedCallbackVal);
		jerry_release_value(storedListenerObj);
	}

	if (shouldAppend) {
		jerry_value_t listenerObj = jerry_create_object();

		jerry_release_value(jerry_set_property(listenerObj, callbackProp, callbackVal));
		jerry_release_value(jerry_set_property(listenerObj, onceProp, jerry_create_boolean(once)));
		setProperty(listenerObj, "removed", JS_FALSE);

		jerry_release_value(jerry_call_function(ref_func_push, listenersArr, &listenerObj, 1));
		jerry_release_value(listenerObj);

		if (targetObj == ref_global) {
			char *type = getString(typeStr);
			if (strcmp(type, "vblank") == 0) dependentEvents |= vblank;
			else if (strcmp(type, "buttondown") == 0) dependentEvents |= buttondown;
			else if (strcmp(type, "buttonup") == 0) dependentEvents |= buttonup;
			else if (strcmp(type, "touchstart") == 0) dependentEvents |= touchstart;
			else if (strcmp(type, "touchmove") == 0) dependentEvents |= touchmove;
			else if (strcmp(type, "touchend") == 0) dependentEvents |= touchend;
			else if (strcmp(type, "keydown") == 0) dependentEvents |= keydown;
			else if (strcmp(type, "keyup") == 0) dependentEvents |= keyup;
			free(type);
		}
	}

	jerry_release_value(listenersArr);
	jerry_release_value(typeStr);
	jerry_release_value(onceProp);
	jerry_release_value(callbackProp);

	return JS_UNDEFINED;
}

FUNCTION(EventTarget_removeEventListener) {
	REQUIRE(2);
	jerry_value_t targetObj = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	jerry_value_t callbackProp = String("callback");
	jerry_value_t typeStr = jerry_value_to_string(args[0]);
	jerry_value_t callbackVal = args[1];

	jerry_value_t eventListenersObj = getInternalProperty(targetObj, "eventListeners");
	jerry_value_t listenersArr = jerry_get_property(eventListenersObj, typeStr); // listeners of the given type
	jerry_release_value(eventListenersObj);
	if (jerry_value_is_array(listenersArr)) {
		u32 length = jerry_get_array_length(listenersArr);
		bool removed = false;
		for (u32 i = 0; !removed && i < length; i++) {
			jerry_value_t storedListenerObj = jerry_get_property_by_index(listenersArr, i);
			jerry_value_t storedCallbackVal = jerry_get_property(storedListenerObj, callbackProp);
			if (strictEqual(callbackVal, storedCallbackVal)) {
				arraySplice(listenersArr, i, 1);
				setProperty(storedListenerObj, "removed", JS_TRUE);
				removed = true;
				if (targetObj == ref_global && jerry_get_array_length(listenersArr) == 0) {
					char *type = getString(typeStr);
					if (strcmp(type, "vblank") == 0) dependentEvents &= ~(vblank);
					else if (strcmp(type, "buttondown") == 0) dependentEvents &= ~(buttondown);
					else if (strcmp(type, "buttonup") == 0) dependentEvents &= ~(buttonup);
					else if (strcmp(type, "touchstart") == 0) dependentEvents &= ~(touchstart);
					else if (strcmp(type, "touchmove") == 0) dependentEvents &= ~(touchmove);
					else if (strcmp(type, "touchend") == 0) dependentEvents &= ~(touchend);
					else if (strcmp(type, "keydown") == 0) dependentEvents &= ~(keydown);
					else if (strcmp(type, "keyup") == 0) dependentEvents &= ~(keyup);
					free(type);
				}
			}
			jerry_release_value(storedCallbackVal);
			jerry_release_value(storedListenerObj);
		}
	}
	jerry_release_value(listenersArr);
	jerry_release_value(typeStr);
	jerry_release_value(callbackProp);

	return JS_UNDEFINED;
}

FUNCTION(EventTarget_dispatchEvent) {
	REQUIRE_FIRST(); EXPECT(isInstance(args[0], ref_Event), Event);
	jerry_value_t targetObj = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	bool canceled = dispatchEvent(targetObj, args[0], true);
	return jerry_create_boolean(!canceled);
}

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
		char *screen = getString(args[0]);
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
	jerry_value_t eventArgs[2] = {String("sleep"), jerry_create_object()};
	setProperty(eventArgs[1], "cancelable", JS_TRUE);
	jerry_value_t eventObj = jerry_construct_object(ref_Event.constructor, eventArgs, 2);
	bool canceled = dispatchEvent(ref_global, eventObj, true);
	jerry_release_value(eventObj);
	jerry_release_value(eventArgs[0]);
	jerry_release_value(eventArgs[1]);
	if (!canceled) {
		systemSleep();
		swiWaitForVBlank();
		swiWaitForVBlank(); // I know this is jank but it's the easiest solution to stop 'wake' from dispatching before the system sleeps
		eventArgs[0] = String("wake");
		eventArgs[1] = jerry_create_object();
		eventObj = jerry_construct_object(ref_Event.constructor, eventArgs, 2);
		dispatchEvent(ref_global, eventObj, true);
		jerry_release_value(eventObj);
		jerry_release_value(eventArgs[0]);
		jerry_release_value(eventArgs[1]);
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

FUNCTION(VRAM_setBankA) {
	REQUIRE_FIRST();
	if (strictEqual(args[0], symbol_LCD)) vramSetBankA(VRAM_A_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x20000)) vramSetBankA(VRAM_A_MAIN_BG_0x06020000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x40000)) vramSetBankA(VRAM_A_MAIN_BG_0x06040000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x60000)) vramSetBankA(VRAM_A_MAIN_BG_0x06060000);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE)) vramSetBankA(VRAM_A_MAIN_SPRITE_0x06400000);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE_0x20000)) vramSetBankA(VRAM_A_MAIN_SPRITE_0x06420000);
	else if (strictEqual(args[0], symbol_TEXTURE_0)) vramSetBankA(VRAM_A_TEXTURE_SLOT0);
	else if (strictEqual(args[0], symbol_TEXTURE_1)) vramSetBankA(VRAM_A_TEXTURE_SLOT1);
	else if (strictEqual(args[0], symbol_TEXTURE_2)) vramSetBankA(VRAM_A_TEXTURE_SLOT2);
	else if (strictEqual(args[0], symbol_TEXTURE_3)) vramSetBankA(VRAM_A_TEXTURE_SLOT3);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank A.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankB) {
	REQUIRE_FIRST();
	if (strictEqual(args[0], symbol_LCD)) vramSetBankB(VRAM_B_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankB(VRAM_B_MAIN_BG_0x06000000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x20000)) vramSetBankB(VRAM_B_MAIN_BG_0x06020000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x40000)) vramSetBankB(VRAM_B_MAIN_BG_0x06040000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x60000)) vramSetBankB(VRAM_B_MAIN_BG_0x06060000);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE)) vramSetBankB(VRAM_B_MAIN_SPRITE_0x06400000);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE_0x20000)) vramSetBankB(VRAM_B_MAIN_SPRITE_0x06420000);
	else if (strictEqual(args[0], symbol_TEXTURE_0)) vramSetBankB(VRAM_B_TEXTURE_SLOT0);
	else if (strictEqual(args[0], symbol_TEXTURE_1)) vramSetBankB(VRAM_B_TEXTURE_SLOT1);
	else if (strictEqual(args[0], symbol_TEXTURE_2)) vramSetBankB(VRAM_B_TEXTURE_SLOT2);
	else if (strictEqual(args[0], symbol_TEXTURE_3)) vramSetBankB(VRAM_B_TEXTURE_SLOT3);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank B.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankC) {
	REQUIRE_FIRST();
	if (strictEqual(args[0], symbol_LCD)) vramSetBankC(VRAM_C_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankC(VRAM_C_MAIN_BG_0x06000000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x20000)) vramSetBankC(VRAM_C_MAIN_BG_0x06020000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x40000)) vramSetBankC(VRAM_C_MAIN_BG_0x06040000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x60000)) vramSetBankC(VRAM_C_MAIN_BG_0x06060000);
	else if (strictEqual(args[0], symbol_ARM7_0)) vramSetBankC(VRAM_C_ARM7_0x06000000);
	else if (strictEqual(args[0], symbol_ARM7_1)) vramSetBankC(VRAM_C_ARM7_0x06020000);
	else if (strictEqual(args[0], symbol_SUB_BG)) vramSetBankC(VRAM_C_SUB_BG);
	else if (strictEqual(args[0], symbol_TEXTURE_0)) vramSetBankC(VRAM_C_TEXTURE_SLOT0);
	else if (strictEqual(args[0], symbol_TEXTURE_1)) vramSetBankC(VRAM_C_TEXTURE_SLOT1);
	else if (strictEqual(args[0], symbol_TEXTURE_2)) vramSetBankC(VRAM_C_TEXTURE_SLOT2);
	else if (strictEqual(args[0], symbol_TEXTURE_3)) vramSetBankC(VRAM_C_TEXTURE_SLOT3);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank C.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankD) {
	REQUIRE_FIRST();
	if (strictEqual(args[0], symbol_LCD)) vramSetBankD(VRAM_D_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankD(VRAM_D_MAIN_BG_0x06000000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x20000)) vramSetBankD(VRAM_D_MAIN_BG_0x06020000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x40000)) vramSetBankD(VRAM_D_MAIN_BG_0x06040000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x60000)) vramSetBankD(VRAM_D_MAIN_BG_0x06060000);
	else if (strictEqual(args[0], symbol_ARM7_0)) vramSetBankD(VRAM_D_ARM7_0x06000000);
	else if (strictEqual(args[0], symbol_ARM7_1)) vramSetBankD(VRAM_D_ARM7_0x06020000);
	else if (strictEqual(args[0], symbol_SUB_SPRITE)) vramSetBankD(VRAM_D_SUB_SPRITE);
	else if (strictEqual(args[0], symbol_TEXTURE_0)) vramSetBankD(VRAM_D_TEXTURE_SLOT0);
	else if (strictEqual(args[0], symbol_TEXTURE_1)) vramSetBankD(VRAM_D_TEXTURE_SLOT1);
	else if (strictEqual(args[0], symbol_TEXTURE_2)) vramSetBankD(VRAM_D_TEXTURE_SLOT2);
	else if (strictEqual(args[0], symbol_TEXTURE_3)) vramSetBankD(VRAM_D_TEXTURE_SLOT3);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank D.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankE) {
	REQUIRE_FIRST();
	if (strictEqual(args[0], symbol_LCD)) vramSetBankE(VRAM_E_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankE(VRAM_E_MAIN_BG);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE)) vramSetBankE(VRAM_E_MAIN_SPRITE);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_0)) vramSetBankE(VRAM_E_TEX_PALETTE);
	else if (strictEqual(args[0], symbol_MAIN_BG_EXT_PALETTE_0)) vramSetBankE(VRAM_E_BG_EXT_PALETTE);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank E.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankF) {
	REQUIRE_FIRST();
	if (strictEqual(args[0], symbol_LCD)) vramSetBankF(VRAM_F_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankF(VRAM_F_MAIN_BG_0x06000000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x4000)) vramSetBankF(VRAM_F_MAIN_BG_0x06004000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x10000)) vramSetBankF(VRAM_F_MAIN_BG_0x06010000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x14000)) vramSetBankF(VRAM_F_MAIN_BG_0x06014000);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_0)) vramSetBankF(VRAM_F_TEX_PALETTE_SLOT0);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_1)) vramSetBankF(VRAM_F_TEX_PALETTE_SLOT1);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_4)) vramSetBankF(VRAM_F_TEX_PALETTE_SLOT4);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_5)) vramSetBankF(VRAM_F_TEX_PALETTE_SLOT5);
	else if (strictEqual(args[0], symbol_MAIN_BG_EXT_PALETTE_0)) vramSetBankF(VRAM_F_BG_EXT_PALETTE_SLOT01);
	else if (strictEqual(args[0], symbol_MAIN_BG_EXT_PALETTE_2)) vramSetBankF(VRAM_F_BG_EXT_PALETTE_SLOT23);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE_EXT_PALETTE)) vramSetBankF(VRAM_F_SPRITE_EXT_PALETTE);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank F.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankG) {
	REQUIRE_FIRST();
	if (strictEqual(args[0], symbol_LCD)) vramSetBankG(VRAM_G_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankG(VRAM_G_MAIN_BG_0x06000000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x4000)) vramSetBankG(VRAM_G_MAIN_BG_0x06004000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x10000)) vramSetBankG(VRAM_G_MAIN_BG_0x06010000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x14000)) vramSetBankG(VRAM_G_MAIN_BG_0x06014000);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_0)) vramSetBankG(VRAM_G_TEX_PALETTE_SLOT0);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_1)) vramSetBankG(VRAM_G_TEX_PALETTE_SLOT1);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_4)) vramSetBankG(VRAM_G_TEX_PALETTE_SLOT4);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_5)) vramSetBankG(VRAM_G_TEX_PALETTE_SLOT5);
	else if (strictEqual(args[0], symbol_MAIN_BG_EXT_PALETTE_0)) vramSetBankG(VRAM_G_BG_EXT_PALETTE_SLOT01);
	else if (strictEqual(args[0], symbol_MAIN_BG_EXT_PALETTE_2)) vramSetBankG(VRAM_G_BG_EXT_PALETTE_SLOT23);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE_EXT_PALETTE)) vramSetBankG(VRAM_G_SPRITE_EXT_PALETTE);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank G.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankH) {
	REQUIRE_FIRST();
	if (strictEqual(args[0], symbol_LCD)) vramSetBankH(VRAM_H_LCD);
	else if (strictEqual(args[0], symbol_SUB_BG)) vramSetBankH(VRAM_H_SUB_BG);
	else if (strictEqual(args[0], symbol_SUB_BG_EXT_PALETTE)) vramSetBankH(VRAM_H_SUB_BG_EXT_PALETTE);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank H.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankI) {
	REQUIRE_FIRST();
	if (strictEqual(args[0], symbol_LCD)) vramSetBankI(VRAM_I_LCD);
	else if (strictEqual(args[0], symbol_SUB_BG_0x8000)) vramSetBankI(VRAM_I_SUB_BG_0x06208000);
	else if (strictEqual(args[0], symbol_SUB_SPRITE)) vramSetBankI(VRAM_I_SUB_SPRITE);
	else if (strictEqual(args[0], symbol_SUB_SPRITE_EXT_PALETTE)) vramSetBankI(VRAM_I_SUB_SPRITE_EXT_PALETTE);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank I.");
	return JS_UNDEFINED;
}

FUNCTION(Video_main_setBackdropColor) {
	REQUIRE_FIRST();
	char *colorDesc = getAsString(args[0]);
	u16 color = colorParse(colorDesc, 0);
	free(colorDesc);
	setBackdropColor(color);
	return JS_UNDEFINED;
}
FUNCTION(Video_sub_setBackdropColor) {
	REQUIRE_FIRST();
	char *colorDesc = getAsString(args[0]);
	u16 color = colorParse(colorDesc, 0);
	free(colorDesc);
	setBackdropColorSub(color);
	return JS_UNDEFINED;
}

FUNCTION(Video_main_setMode) {
	REQUIRE_FIRST(); EXPECT(jerry_value_is_number(args[0]), number);
	bool is3D = argCount > 1 && jerry_get_boolean_value(args[1]);
	u32 mode = jerry_value_as_uint32(args[0]);
	if (mode == 0) videoSetMode(is3D ? MODE_0_3D : MODE_0_2D);
	else if (mode == 1) videoSetMode(is3D ? MODE_1_3D : MODE_1_2D);
	else if (mode == 2) videoSetMode(is3D ? MODE_2_3D : MODE_2_2D);
	else if (mode == 3) videoSetMode(is3D ? MODE_3_3D : MODE_3_2D);
	else if (mode == 4) videoSetMode(is3D ? MODE_4_3D : MODE_4_2D);
	else if (mode == 5) videoSetMode(is3D ? MODE_5_3D : MODE_5_2D);
	else if (mode == 6) videoSetMode(is3D ? MODE_6_3D : MODE_6_2D);
	else return TypeError("Invalid video mode.");
	return JS_UNDEFINED;
}
FUNCTION(Video_sub_setMode) {
	REQUIRE_FIRST(); EXPECT(jerry_value_is_number(args[0]), number);
	u32 mode = jerry_value_as_uint32(args[0]);
	if (mode == 0) videoSetModeSub(MODE_0_2D);
	else if (mode == 1) videoSetModeSub(MODE_1_2D);
	else if (mode == 2) videoSetModeSub(MODE_2_2D);
	else if (mode == 3) videoSetModeSub(MODE_3_2D);
	else if (mode == 4) videoSetModeSub(MODE_4_2D);
	else if (mode == 5) videoSetModeSub(MODE_5_2D);
	else return TypeError("Invalid video mode.");
	return JS_UNDEFINED;
}

inline int getID(jerry_value_t obj) {
	jerry_value_t idNum = getInternalProperty(obj, "id");
	int id = jerry_value_as_integer(idNum);
	jerry_release_value(idNum);
	return id;
}

FUNCTION(Sprite_set_x) {
	NOT_REMOVED(thisValue);
	SPRITE_ENTRY(thisValue)->x = jerry_value_as_uint32(args[0]);
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_x) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(SPRITE_ENTRY(thisValue)->x);
}

FUNCTION(Sprite_set_y) {
	NOT_REMOVED(thisValue);
	SPRITE_ENTRY(thisValue)->y = jerry_value_as_uint32(args[0]);
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_y) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(SPRITE_ENTRY(thisValue)->y);
}

FUNCTION(Sprite_setPosition) {
	NOT_REMOVED(thisValue);
	REQUIRE(2);
	oamSetXY(SPRITE_ENGINE(thisValue), getID(thisValue), jerry_value_as_int32(args[0]), jerry_value_as_int32(args[1]));
	return JS_UNDEFINED;
}

FUNCTION(Sprite_set_gfx) {
	NOT_REMOVED(thisValue);
	EXPECT(isInstance(args[0], ref_SpriteGraphic), SpriteGraphic);
	NOT_REMOVED(args[0]);
	OamState *engine = SPRITE_ENGINE(thisValue);
	if (SPRITE_ENGINE(args[0]) != engine) return TypeError("Given SpriteGraphic was from the wrong engine.");
	
	jerry_value_t sizeNum = getInternalProperty(args[0], "size");
	jerry_value_t bppNum = getInternalProperty(args[0], "colorFormat");
	SpriteSize size = (SpriteSize) jerry_value_as_uint32(sizeNum);
	int bpp = jerry_value_as_int32(bppNum);
	jerry_release_value(sizeNum);
	jerry_release_value(bppNum);
	jerry_value_t typedArray = getInternalProperty(args[0], "data");
	jerry_length_t byteOffset, arrayBufferLen;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(typedArray, &byteOffset, &arrayBufferLen);
	u8 *gfxData = jerry_get_arraybuffer_pointer(arrayBuffer);
	jerry_release_value(arrayBuffer);
	jerry_release_value(typedArray);

	oamSetGfx(
		engine,
		getID(thisValue),
		size,
		bpp == 4 ? SpriteColorFormat_16Color : bpp == 8 ? SpriteColorFormat_256Color : SpriteColorFormat_Bmp,
		gfxData
	);

	setInternalProperty(thisValue, "gfx", args[0]);
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_gfx) {
	NOT_REMOVED(thisValue);
	return getInternalProperty(thisValue, "gfx");
}

FUNCTION(Sprite_set_palette) {
	NOT_REMOVED(thisValue);
	int palette = jerry_value_as_int32(args[0]);
	SPRITE_ENTRY(thisValue)->palette = BOUND(palette, 0, 15);
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_palette) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(SPRITE_ENTRY(thisValue)->palette);
}

FUNCTION(Sprite_set_priority) {
	NOT_REMOVED(thisValue);
	int priority = jerry_value_as_int32(args[0]);
	oamSetPriority(SPRITE_ENGINE(thisValue), getID(thisValue), BOUND(priority, 0, 3));
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_priority) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(SPRITE_ENTRY(thisValue)->priority);
}

FUNCTION(Sprite_set_hidden) {
	NOT_REMOVED(thisValue);
	SpriteEntry *sprite = SPRITE_ENTRY(thisValue);
	if (jerry_get_boolean_value(args[0])) { // hide
		if (sprite->isRotateScale) {
			// detach affine index so the sprite can be hidden
			sprite->isRotateScale = false;
			sprite->isSizeDouble = false;
		}
		sprite->isHidden = true;
	}
	else if (!sprite->isRotateScale) { // unhide (if isRotateScale is true, then it is already visible)
		sprite->isHidden = false;
		jerry_value_t affineObj = getInternalProperty(thisValue, "affine");
		if (!jerry_value_is_null(affineObj)) {
			// reattach affine index and reset sizeDouble value
			sprite->rotationIndex = getID(affineObj);
			sprite->isSizeDouble = testInternalProperty(thisValue, "sizeDouble");
			sprite->isRotateScale = true;
		}
		jerry_release_value(affineObj);
	}
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_hidden) {
	NOT_REMOVED(thisValue);
	SpriteEntry *sprite = SPRITE_ENTRY(thisValue);
	return jerry_create_boolean(sprite->isHidden && !sprite->isRotateScale);
}

FUNCTION(Sprite_set_flipH) {
	NOT_REMOVED(thisValue);
	bool set = jerry_get_boolean_value(args[0]);
	setInternalProperty(thisValue, "flipH", jerry_create_boolean(set));
	SpriteEntry *sprite = SPRITE_ENTRY(thisValue);
	if (!sprite->isRotateScale) sprite->hFlip = set;
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_flipH) {
	NOT_REMOVED(thisValue);
	return getInternalProperty(thisValue, "flipH");
}

FUNCTION(Sprite_set_flipV) {
	NOT_REMOVED(thisValue);
	bool set = jerry_get_boolean_value(args[0]);
	setInternalProperty(thisValue, "flipV", jerry_create_boolean(set));
	SpriteEntry *sprite = SPRITE_ENTRY(thisValue);
	if (!sprite->isRotateScale) sprite->vFlip = set;
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_flipV) {
	NOT_REMOVED(thisValue);
	return getInternalProperty(thisValue, "flipV");
}

FUNCTION(Sprite_set_affine) {
	NOT_REMOVED(thisValue);
	OamState *engine = SPRITE_ENGINE(thisValue);
	SpriteEntry *sprite = engine->oamMemory + getID(thisValue);
	if (jerry_value_is_null(args[0])) {
		if (sprite->isRotateScale) {
			sprite->isRotateScale = false;
			sprite->isSizeDouble = false;
		}
		sprite->hFlip = testInternalProperty(thisValue, "flipH");
		sprite->vFlip = testInternalProperty(thisValue, "flipV");
	}
	else {
		EXPECT(isInstance(args[0], ref_SpriteAffineMatrix), SpriteAffineMatrix);
		NOT_REMOVED(args[0]);
		if (SPRITE_ENGINE(args[0]) != engine) return TypeError("Given SpriteAffineMatrix was from the wrong engine.");
		if (sprite->isRotateScale || !sprite->isHidden) {
			sprite->rotationIndex = getID(args[0]);
			sprite->isSizeDouble = testInternalProperty(thisValue, "sizeDouble");
			sprite->isRotateScale = true;
		}
	}
	setInternalProperty(thisValue, "affine", args[0]);
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_affine) {
	NOT_REMOVED(thisValue);
	return getInternalProperty(thisValue, "affine");
}

FUNCTION(Sprite_set_sizeDouble) {
	NOT_REMOVED(thisValue);
	bool set = jerry_get_boolean_value(args[0]);
	setInternalProperty(thisValue, "sizeDouble", jerry_create_boolean(set));
	SpriteEntry *sprite = SPRITE_ENTRY(thisValue);
	if (sprite->isRotateScale) sprite->isSizeDouble = set;
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_sizeDouble) {
	NOT_REMOVED(thisValue);
	return getInternalProperty(thisValue, "sizeDouble");
}

FUNCTION(Sprite_set_mosaic) {
	NOT_REMOVED(thisValue);
	SPRITE_ENTRY(thisValue)->isMosaic = jerry_get_boolean_value(args[0]);
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_mosaic) {
	NOT_REMOVED(thisValue);
	return jerry_create_boolean(SPRITE_ENTRY(thisValue)->isMosaic);
}

FUNCTION(Sprite_remove) {
	NOT_REMOVED(thisValue);
	OamState *engine = SPRITE_ENGINE(thisValue);
	int id = getID(thisValue);
	oamClearSprite(engine, id);
	u8 usageMask = (engine == &oamMain ? USAGE_SPRITE_MAIN : USAGE_SPRITE_SUB);
	spriteUsage[id] ^= ~usageMask;
	jerry_set_internal_property(thisValue, ref_str_removed, JS_TRUE);
	return JS_UNDEFINED;
}

FUNCTION(SpriteGraphic_get_width) {
	NOT_REMOVED(thisValue);
	jerry_value_t sizeNum = getInternalProperty(thisValue, "size");
	SpriteSize size = (SpriteSize) jerry_value_as_uint32(sizeNum);
	jerry_release_value(sizeNum);
	if (size == SpriteSize_8x8 || size == SpriteSize_8x16 || size == SpriteSize_8x32) return jerry_create_number(8);
	if (size == SpriteSize_16x8 || size == SpriteSize_16x16 || size == SpriteSize_16x32) return jerry_create_number(16);
	if (size == SpriteSize_32x8 || size == SpriteSize_32x16 || size == SpriteSize_32x32 || size == SpriteSize_32x64) return jerry_create_number(32);
	if (size == SpriteSize_64x32 || size == SpriteSize_64x64) return jerry_create_number(64);
	return jerry_create_number_nan();
}
FUNCTION(SpriteGraphic_get_height) {
	NOT_REMOVED(thisValue);
	jerry_value_t sizeNum = getInternalProperty(thisValue, "size");
	SpriteSize size = (SpriteSize) jerry_value_as_uint32(sizeNum);
	jerry_release_value(sizeNum);
	if (size == SpriteSize_8x8 || size == SpriteSize_16x8 || size == SpriteSize_32x8) return jerry_create_number(8);
	if (size == SpriteSize_8x16 || size == SpriteSize_16x16 || size == SpriteSize_32x16) return jerry_create_number(16);
	if (size == SpriteSize_8x32 || size == SpriteSize_16x32 || size == SpriteSize_32x32 || size == SpriteSize_64x32) return jerry_create_number(32);
	if (size == SpriteSize_32x64 || size == SpriteSize_64x64) return jerry_create_number(64);
	return jerry_create_number_nan();
}

FUNCTION(SpriteGraphic_remove) {
	NOT_REMOVED(thisValue);
	jerry_value_t typedArray = getInternalProperty(thisValue, "data");
	jerry_length_t byteOffset, byteLength;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(typedArray, &byteOffset, &byteLength);
	u8 *gfxData = jerry_get_arraybuffer_pointer(arrayBuffer);
	jerry_release_value(arrayBuffer);
	jerry_release_value(typedArray);
	oamFreeGfx(SPRITE_ENGINE(thisValue), gfxData);
	jerry_set_internal_property(thisValue, ref_str_removed, JS_TRUE);
	return JS_UNDEFINED;
}

FUNCTION(SpriteAffineMatrix_set_hdx) {
	NOT_REMOVED(thisValue);
	SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].hdx = floatToFixed(jerry_get_number_value(args[0]), 8);
	return JS_UNDEFINED;
}
FUNCTION(SpriteAffineMatrix_get_hdx) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(fixedToFloat(SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].hdx, 8));
}
FUNCTION(SpriteAffineMatrix_set_hdy) {
	NOT_REMOVED(thisValue);
	SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].hdy = floatToFixed(jerry_get_number_value(args[0]), 8);
	return JS_UNDEFINED;
}
FUNCTION(SpriteAffineMatrix_get_hdy) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(fixedToFloat(SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].hdy, 8));
}
FUNCTION(SpriteAffineMatrix_set_vdx) {
	NOT_REMOVED(thisValue);
	SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].vdx = floatToFixed(jerry_get_number_value(args[0]), 8);
	return JS_UNDEFINED;
}
FUNCTION(SpriteAffineMatrix_get_vdx) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(fixedToFloat(SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].vdx, 8));
}
FUNCTION(SpriteAffineMatrix_set_vdy) {
	NOT_REMOVED(thisValue);
	SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].vdy = floatToFixed(jerry_get_number_value(args[0]), 8);
	return JS_UNDEFINED;
}
FUNCTION(SpriteAffineMatrix_get_vdy) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(fixedToFloat(SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].vdy, 8));
}

FUNCTION(SpriteAffineMatrix_rotateScale) {
	NOT_REMOVED(thisValue);
	REQUIRE(3);
	int angle = degreesToAngle(jerry_value_as_int32(args[0]));
	int sx = floatToFixed(jerry_get_number_value(args[1]), 8);
	int sy = floatToFixed(jerry_get_number_value(args[2]), 8);
	oamRotateScale(SPRITE_ENGINE(thisValue), getID(thisValue), angle, sx, sy);
	return JS_UNDEFINED;
}

FUNCTION(SpriteAffineMatrix_remove) {
	NOT_REMOVED(thisValue);
	u8 usageMask = (JS_testInternalProperty(thisValue, ref_str_main) ? USAGE_MATRIX_MAIN : USAGE_MATRIX_SUB);
	spriteUsage[getID(thisValue)] ^= ~usageMask;
	jerry_set_internal_property(thisValue, ref_str_removed, JS_TRUE);
	return JS_UNDEFINED;
}

FUNCTION(SpriteEngine_init) {
	SpriteMapping mapping;
	bool allowBitmaps = argCount > 0 && jerry_get_boolean_value(args[0]);
	bool use2DMapping = argCount > 1 && jerry_get_boolean_value(args[1]);
	int boundarySizeSet = argCount > 2;
	int boundarySize = boundarySizeSet ? jerry_value_as_int32(args[2]) : 0;
	bool useExternalPalettes = argCount > 3 && jerry_get_boolean_value(args[3]);
	if (allowBitmaps) {
		if (!boundarySizeSet || boundarySize == 128) mapping = use2DMapping ? SpriteMapping_Bmp_2D_128 : SpriteMapping_Bmp_1D_128;
		else if (boundarySize == 256) mapping = use2DMapping ? SpriteMapping_Bmp_2D_256 : SpriteMapping_Bmp_1D_256;
		else return TypeError("Boundary size for bitmap sprites should be 128 or 256.");
	}
	else if (use2DMapping) {
		if (!boundarySizeSet || boundarySize == 32) mapping = SpriteMapping_2D;
		else return TypeError("Boundary size for 2D sprite tiles should be 32.");
	}
	else if (!boundarySizeSet || boundarySize == 32) mapping = SpriteMapping_1D_32;
	else if (boundarySize == 64) mapping = SpriteMapping_1D_64;
	else if (boundarySize == 128) mapping = SpriteMapping_1D_128;
	else if (boundarySize == 256) mapping = SpriteMapping_1D_256;
	else return TypeError("Boundary size for 1D sprite tiles should be 32, 64, 128, or 256.");
	bool isMain = JS_testInternalProperty(thisValue, ref_str_main);
	oamInit(isMain ? &oamMain : &oamSub, mapping, useExternalPalettes);
	if (isMain) spriteUpdateMain = true;
	else spriteUpdateSub = true;
	return JS_UNDEFINED;
}

FUNCTION(SpriteEngine_enable) {
	bool isMain = JS_testInternalProperty(thisValue, ref_str_main);
	oamEnable(isMain ? &oamMain : &oamSub);
	if (isMain) spriteUpdateMain = true;
	else spriteUpdateSub = true;
	return JS_UNDEFINED;
}
FUNCTION(SpriteEngine_disable) {
	bool isMain = JS_testInternalProperty(thisValue, ref_str_main);
	oamDisable(isMain ? &oamMain : &oamSub);
	if (isMain) spriteUpdateMain = false;
	else spriteUpdateSub = false;
	return JS_UNDEFINED;
}

FUNCTION(SpriteEngine_addSprite) {
	REQUIRE(3);
	EXPECT(isInstance(args[2], ref_SpriteGraphic), SpriteGraphic);
	NOT_REMOVED(args[2]);
	OamState *engine = SPRITE_ENGINE(thisValue);
	if (SPRITE_ENGINE(args[2]) != engine) return TypeError("Given SpriteGraphic was from the wrong engine.");
	bool setsAffine = argCount > 8 && !jerry_value_is_undefined(args[8]) && !jerry_value_is_null(args[8]);
	if (setsAffine) {
		EXPECT(isInstance(args[8], ref_SpriteAffineMatrix), SpriteAffineMatrix);
		NOT_REMOVED(args[8]);
		if (SPRITE_ENGINE(args[8]) != engine) return TypeError("Given SpriteAffineMatrix was from the wrong engine.");
	}

	int id = -1;
	u8 usageMask = engine == &oamMain ? USAGE_SPRITE_MAIN : USAGE_SPRITE_SUB;
	for (int i = 0; i < SPRITE_COUNT; i++) {
		if ((spriteUsage[i] & usageMask) == 0) {
			spriteUsage[i] |= usageMask;
			id = i;
			break;
		}
	}
	if (id == -1) return Error("Out of sprite slots.");

	jerry_value_t sizeNum = getInternalProperty(args[2], "size");
	jerry_value_t bppNum = getInternalProperty(args[2], "colorFormat");
	SpriteSize size = (SpriteSize) jerry_value_as_uint32(sizeNum);
	int bpp = jerry_value_as_int32(bppNum);
	jerry_release_value(sizeNum);
	jerry_release_value(bppNum);
	jerry_value_t typedArray = getInternalProperty(args[2], "data");
	jerry_length_t byteOffset, arrayBufferLen;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(typedArray, &byteOffset, &arrayBufferLen);
	u8 *gfxData = jerry_get_arraybuffer_pointer(arrayBuffer);
	jerry_release_value(arrayBuffer);
	jerry_release_value(typedArray);

	int x = jerry_value_as_int32(args[0]);
	int y = jerry_value_as_int32(args[1]);
	int paletteOrAlpha = argCount > 3 ? jerry_value_as_int32(args[3]) : 0;
	int priority = argCount > 4 ? jerry_value_as_int32(args[4]) : 0;
	bool hide = argCount > 5 && jerry_get_boolean_value(args[5]);
	bool flipH = !setsAffine && argCount > 6 && jerry_get_boolean_value(args[6]);
	bool flipV = !setsAffine && argCount > 7 && jerry_get_boolean_value(args[7]);
	int affineIndex = setsAffine ? getID(args[8]) : -1;
	bool sizeDouble = argCount > 9 && jerry_get_boolean_value(args[9]);
	bool mosaic = argCount > 10 && jerry_get_boolean_value(args[10]);

	oamSet(
		engine, id, x, y,
		BOUND(priority, 0, 3),
		BOUND(paletteOrAlpha, 0, 15),
		size,
		bpp == 4 ? SpriteColorFormat_16Color : bpp == 8 ? SpriteColorFormat_256Color : SpriteColorFormat_Bmp,
		gfxData,
		hide ? -1 : affineIndex,
		sizeDouble, false, flipH, flipV, mosaic
	);
	// set hidden flag manually, because oamSet ignores the other parameters if hide is true.
	if (hide) engine->oamMemory[id].isHidden = true;

	jerry_value_t spriteObj = jerry_create_object();
	jerry_value_t idNum = jerry_create_number(id);
	setInternalProperty(spriteObj, "id", idNum);
	jerry_release_value(idNum);
	JS_setReadonly(spriteObj, ref_str_main, jerry_get_internal_property(thisValue, ref_str_main));
	setPrototype(spriteObj, bpp == 16 ? ref_BitmapSprite.prototype : ref_PalettedSprite.prototype);
	setInternalProperty(spriteObj, "flipH", JS_FALSE);
	setInternalProperty(spriteObj, "flipV", JS_FALSE);
	setInternalProperty(spriteObj, "affine", setsAffine ? args[8] : JS_NULL);
	setInternalProperty(spriteObj, "sizeDouble", jerry_create_boolean(sizeDouble));
	return spriteObj;
}

FUNCTION(SpriteEngine_addGraphic) {
	REQUIRE(3);
	if (argCount > 3) EXPECT(jerry_get_typedarray_type(args[3]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	int width = jerry_value_as_int32(args[0]);
	int height = jerry_value_as_int32(args[1]);
	int bpp = jerry_value_as_int32(args[2]);

	SpriteSize size;
	if (width > 64) return TypeError("Sprite width is above the limit of 64.");
	if (height > 64) return TypeError("Sprite height is above the limit of 64.");
	if (width <= 8) {
		if (height <= 8) size = SpriteSize_8x8;
		else if (height <= 16) size = SpriteSize_8x16;
		else if (height <= 32) size = SpriteSize_8x32;
		else size = SpriteSize_32x64;
	}
	else if (width <= 16) {
		if (height <= 8) size = SpriteSize_16x8;
		else if (height <= 16) size = SpriteSize_16x16;
		else if (height <= 32) size = SpriteSize_16x32;
		else size = SpriteSize_32x64;
	}
	else if (width <= 32) {
		if (height <= 8) size = SpriteSize_32x8;
		else if (height <= 16) size = SpriteSize_32x16;
		else if (height <= 32) size = SpriteSize_32x32;
		else size = SpriteSize_32x64;
	}
	else if (height <= 32) size = SpriteSize_64x32;
	else size = SpriteSize_64x64;

	SpriteColorFormat format;
	if (bpp == 4) format = SpriteColorFormat_16Color;
	else if (bpp == 8) format = SpriteColorFormat_256Color;
	else if (bpp == 16) format = SpriteColorFormat_Bmp;
	else return TypeError("Expected a bits-per-pixel value of either 4, 8, or 16.");
	
	OamState *engine = SPRITE_ENGINE(thisValue);
	u16 *gfxData = oamAllocateGfx(engine, size, format);
	if (engine->firstFree == -1) return Error("Out of sprite graphics memory.");
	u32 byteSize = SPRITE_SIZE_PIXELS(size);
	if (bpp == 4) byteSize /= 2;
	else if (bpp == 16) byteSize *= 2;

	if (argCount > 3) {
		jerry_length_t byteOffset, inputArrayBufferLen;
		jerry_value_t inputArrayBuffer = jerry_get_typedarray_buffer(args[3], &byteOffset, &inputArrayBufferLen);
		jerry_arraybuffer_read(inputArrayBuffer, byteOffset, (u8 *) gfxData, byteSize < inputArrayBufferLen ? byteSize : inputArrayBufferLen);
		jerry_release_value(inputArrayBuffer);
	}

	jerry_value_t arrayBuffer = jerry_create_arraybuffer_external(byteSize, (u8 *) gfxData, [](void * _){});
	jerry_value_t typedArray = jerry_create_typedarray_for_arraybuffer(JERRY_TYPEDARRAY_UINT8, arrayBuffer);
	jerry_release_value(arrayBuffer);
	
	jerry_value_t spriteGraphicObj = jerry_create_object();
	JS_setReadonly(spriteGraphicObj, ref_str_main, jerry_get_internal_property(thisValue, ref_str_main));
	setPrototype(spriteGraphicObj, ref_SpriteGraphic.prototype);
	setReadonly(spriteGraphicObj, "colorFormat", args[2]);
	jerry_value_t sizeNum = jerry_create_number(size);
	setInternalProperty(spriteGraphicObj, "size", sizeNum);
	jerry_release_value(sizeNum);
	setReadonly(spriteGraphicObj, "data", typedArray);
	jerry_release_value(typedArray);
	return spriteGraphicObj;
}

FUNCTION(SpriteEngine_addAffineMatrix) {
	OamState *engine = SPRITE_ENGINE(thisValue);

	int id = -1;
	u8 usageMask = engine == &oamMain ? USAGE_MATRIX_MAIN : USAGE_MATRIX_SUB;
	for (int i = 0; i < MATRIX_COUNT; i++) {
		if ((spriteUsage[i] & usageMask) == 0) {
			spriteUsage[i] |= usageMask;
			id = i;
			break;
		}
	}
	if (id == -1) return Error("Out of affine matrix slots.");

	int hdx = argCount > 0 ? floatToFixed(jerry_get_number_value(args[0]), 8) : (1 << 8);
	int hdy = argCount > 1 ? floatToFixed(jerry_get_number_value(args[1]), 8) : 0;
	int vdx = argCount > 2 ? floatToFixed(jerry_get_number_value(args[2]), 8) : 0;
	int vdy = argCount > 3 ? floatToFixed(jerry_get_number_value(args[3]), 8) : (1 << 8);
	oamAffineTransformation(engine, id, hdx, hdy, vdx, vdy);
	
	jerry_value_t affineObj = jerry_create_object();
	jerry_value_t idNum = jerry_create_number(id);
	setInternalProperty(affineObj, "id", idNum);
	jerry_release_value(idNum);
	JS_setReadonly(affineObj, ref_str_main, jerry_get_internal_property(thisValue, ref_str_main));
	setPrototype(affineObj, ref_SpriteAffineMatrix.prototype);
	return affineObj;
}

FUNCTION(SpriteEngine_setMosaic) {
	REQUIRE(2);
	bool isMain = JS_testInternalProperty(thisValue, ref_str_main);
	int dx = jerry_value_as_uint32(args[0]);
	int dy = jerry_value_as_uint32(args[1]);
	(isMain ? oamSetMosaic : oamSetMosaicSub)(BOUND(dx, 0, 15), BOUND(dy, 0, 15));
	return JS_UNDEFINED;
}

FUNCTION(BETA_gfxInit) {
	videoSetMode(MODE_3_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	return JS_UNDEFINED;
}

FUNCTION(BETA_gfxPixel) {
	REQUIRE(3);
	u8 x = jerry_value_as_uint32(args[0]);
	u8 y = jerry_value_as_uint32(args[1]);
	u16 color = jerry_value_as_uint32(args[2]);
	bgGetGfxPtr(3)[x + y*256] = color;
	return JS_UNDEFINED;
}

FUNCTION(BETA_gfxRect) {
	REQUIRE(5);
	u8 x = jerry_value_as_uint32(args[0]);
	u8 y = jerry_value_as_uint32(args[1]) % 192;
	u16 width = jerry_value_as_uint32(args[2]);
	u16 height = jerry_value_as_uint32(args[3]);
	u16 color = jerry_value_as_uint32(args[4]);
	u16 *gfx = bgGetGfxPtr(3);
	
	if (width + x > 256) width = 256 - x;
	if (height + y > 192) height = 192 - y;

	if (width == 0 || height == 0) return JS_UNDEFINED;

	for (u8 i = 0; i < height; i++) {
		dmaFillHalfWords(color, gfx + x + ((y + i) * 256), width * 2);
	}
	return JS_UNDEFINED;
}

void exposeBetaAPI() {
	jerry_value_t beta = jerry_create_object();
	setProperty(ref_global, "beta", beta);

	setMethod(beta, "gfxInit", BETA_gfxInit);
	setMethod(beta, "gfxPixel", BETA_gfxPixel);
	setMethod(beta, "gfxRect", BETA_gfxRect);

	jerry_release_value(beta);
}

void exposeAPI() {
	// hold some internal references first
	ref_global = jerry_get_global_object();
	ref_Error.constructor = getProperty(ref_global, "Error");
	ref_Error.prototype = jerry_get_property(ref_Error.constructor, ref_str_prototype);
	ref_consoleCounters = jerry_create_object();
	ref_consoleTimers = jerry_create_object();
	ref_storage = jerry_create_object();
	jerry_value_t tempArr = jerry_create_array(0);
	ref_func_push = getProperty(tempArr, "push");
	ref_func_slice = getProperty(tempArr, "slice");
	ref_func_splice = getProperty(tempArr, "splice");
	jerry_release_value(tempArr);
	ref_str_name = String("name");
	ref_str_constructor = String("constructor");
	ref_str_prototype = String("prototype");
	ref_str_backtrace = String("backtrace");
	ref_str_main = String("main");
	ref_str_removed = String("removed");
	ref_sym_toStringTag = jerry_get_well_known_symbol(JERRY_SYMBOL_TO_STRING_TAG);
	#define CREATE_SYMBOL(name) symbol_##name = Symbol(#name)
	FOR_CUSTOM_SYMBOL_NAMES(CREATE_SYMBOL)

	setProperty(ref_global, "self", ref_global);

	setMethod(ref_global, "alert", alert);
	setMethod(ref_global, "clearInterval", clearInterval);
	setMethod(ref_global, "clearTimeout", clearInterval);
	setMethod(ref_global, "close", closeJSDS);
	setMethod(ref_global, "confirm", confirm);
	setMethod(ref_global, "prompt", prompt);
	setMethod(ref_global, "setInterval", setInterval);
	setMethod(ref_global, "setTimeout", setTimeout);
	
	jerry_value_t console = createObject(ref_global, "console");
	setMethod(console, "assert", console_assert);
	setMethod(console, "clear", console_clear);
	setMethod(console, "count", console_count);
	setMethod(console, "countReset", console_countReset);
	setMethod(console, "debug", console_debug);
	setMethod(console, "dir", console_dir);
	setMethod(console, "error", console_error);
	setMethod(console, "group", console_group);
	setMethod(console, "groupEnd", console_groupEnd);
	setMethod(console, "info", console_info);
	setMethod(console, "log", console_log);
	setMethod(console, "table", console_table);
	setMethod(console, "time", console_time);
	setMethod(console, "timeLog", console_timeLog);
	setMethod(console, "timeEnd", console_timeEnd);
	setMethod(console, "trace", console_trace);
	setMethod(console, "warn", console_warn);
	defGetterSetter(console, "textColor", RETURN(jerry_create_number(consoleGetColor())), console_textColor);
	defGetterSetter(console, "textBackground", RETURN(jerry_create_number(consoleGetBackground())), console_textBackground);
	jerry_release_value(console);

	jerry_value_t keyboard = createObject(ref_global, "keyboard");
	setMethod(keyboard, "hide", VOID(keyboardHide()));
	setMethod(keyboard, "show", VOID(keyboardShow()));
	setMethod(keyboard, "watchButtons", VOID(keyboardButtonControls(true)));
	setMethod(keyboard, "ignoreButtons", VOID(keyboardButtonControls(false)));
	jerry_release_value(keyboard);

	jerry_value_t Text = createObject(ref_global, "Text");
	setMethod(Text, "encode", Text_encode);
	setMethod(Text, "decode", Text_decode);
	setMethod(Text, "encodeUTF16", Text_encodeUTF16);
	setMethod(Text, "decodeUTF16", Text_decodeUTF16);
	jerry_release_value(Text);

	jerry_value_t Base64 = createObject(ref_global, "Base64");
	setMethod(Base64, "encode", Base64_encode);
	setMethod(Base64, "decode", Base64_decode);
	jerry_release_value(Base64);

	// Simple custom File class, nothing like the web version
	JS_class File = createClass(ref_global, "File", IllegalConstructor);
	setMethod(File.prototype, "read", File_read);
	setMethod(File.prototype, "write", File_write);
	setMethod(File.prototype, "seek", File_seek);
	setMethod(File.prototype, "close", File_close);
	setMethod(File.constructor, "open", File_static_open);
	setMethod(File.constructor, "copy", File_static_copy);
	setMethod(File.constructor, "rename", File_static_rename);
	setMethod(File.constructor, "remove", File_static_remove);
	setMethod(File.constructor, "read", File_static_read);
	setMethod(File.constructor, "readText", File_static_readText);
	setMethod(File.constructor, "write", File_static_write);
	setMethod(File.constructor, "writeText", File_static_writeText);
	setMethod(File.constructor, "makeDir", File_static_makeDir);
	setMethod(File.constructor, "readDir", File_static_readDir);
	setMethod(File.constructor, "browse", File_static_browse);
	ref_File = File;

	jerry_value_t storage = createObject(ref_global, "storage");
	defGetter(storage, "length", storage_length);
	setMethod(storage, "key", storage_key);
	setMethod(storage, "getItem", storage_getItem);
	setMethod(storage, "setItem", storage_setItem);
	setMethod(storage, "removeItem", storage_removeItem);
	setMethod(storage, "clear", storage_clear);
	setMethod(storage, "save", storage_save);
	jerry_release_value(storage);

	JS_class EventTarget = createClass(ref_global, "EventTarget", EventTargetConstructor);
	setMethod(EventTarget.prototype, "addEventListener", EventTarget_addEventListener);
	setMethod(EventTarget.prototype, "removeEventListener", EventTarget_removeEventListener);
	setMethod(EventTarget.prototype, "dispatchEvent", EventTarget_dispatchEvent);
	// turn global into an EventTarget
	setPrototype(ref_global, EventTarget.prototype);
	EventTargetConstructor(EventTarget.constructor, ref_global, NULL, 0);
	releaseClass(EventTarget);

	JS_class Event = createClass(ref_global, "Event", EventConstructor);
	setMethod(Event.prototype, "stopImmediatePropagation", Event_stopImmediatePropagation);
	setMethod(Event.prototype, "preventDefault", Event_preventDefault);
	ref_Event = Event;

	defEventAttribute(ref_global, "onerror");
	defEventAttribute(ref_global, "onunhandledrejection");
	defEventAttribute(ref_global, "onkeydown");
	defEventAttribute(ref_global, "onkeyup");
	defEventAttribute(ref_global, "onbuttondown");
	defEventAttribute(ref_global, "onbuttonup");
	defEventAttribute(ref_global, "onsleep");
	defEventAttribute(ref_global, "ontouchstart");
	defEventAttribute(ref_global, "ontouchmove");
	defEventAttribute(ref_global, "ontouchend");
	defEventAttribute(ref_global, "onvblank");
	defEventAttribute(ref_global, "onwake");

	jerry_value_t DS = createObject(ref_global, "DS");
	setMethod(DS, "getBatteryLevel", DS_getBatteryLevel);
	setMethod(DS, "getMainScreen", RETURN(String(REG_POWERCNT & POWER_SWAP_LCDS ? "top" : "bottom")));
	setReadonly(DS, "isDSiMode", jerry_create_boolean(isDSiMode()));
	setMethod(DS, "setMainScreen", DS_setMainScreen);
	setMethod(DS, "shutdown", VOID(systemShutDown()));
	setMethod(DS, "sleep", DS_sleep);
	setMethod(DS, "swapScreens", VOID(lcdSwap()));
	jerry_release_value(DS);

	jerry_value_t Profile = createObject(ref_global, "Profile");
	setReadonlyNumber(Profile, "alarmHour", PersonalData->alarmHour);
	setReadonlyNumber(Profile, "alarmMinute", PersonalData->alarmMinute);
	setReadonlyNumber(Profile, "birthDay", PersonalData->birthDay);
	setReadonlyNumber(Profile, "birthMonth", PersonalData->birthMonth);
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
	setReadonlyStringUTF16(Profile, "name", (char16_t *) PersonalData->name, PersonalData->nameLen);
	setReadonlyStringUTF16(Profile, "message", (char16_t *) PersonalData->message, PersonalData->messageLen);
	#pragma GCC diagnostic pop
	const u16 themeColors[16] = {0xCE0C, 0x8137, 0x8C1F, 0xFE3F, 0x825F, 0x839E, 0x83F5, 0x83E0, 0x9E80, 0xC769, 0xFAE6, 0xF960, 0xC800, 0xE811, 0xF41A, 0xC81F};
	setReadonlyNumber(Profile, "color", PersonalData->theme < 16 ? themeColors[PersonalData->theme] : 0);
	setReadonly(Profile, "autoMode", jerry_create_boolean(PersonalData->autoMode));
	setReadonlyString(Profile, "gbaScreen", PersonalData->gbaScreen ? "bottom" : "top");
	const char languages[8][10] = {"日本語", "English", "Français", "Deutsch", "Italiano", "Español", "中文", "한국어"};
	setReadonlyString(Profile, "language", PersonalData->language < 8 ? languages[PersonalData->language] : "");
	jerry_release_value(Profile);

	jerry_value_t Button = createObject(ref_global, "Button");
	jerry_value_t buttonObj;
	#define DEF_BUTTON_OBJECT(name, value) \
		buttonObj = createObject(Button, name); \
		defGetter(buttonObj, "pressed", RETURN(jerry_create_boolean(keysDown() & value))); \
		defGetter(buttonObj, "held", RETURN(jerry_create_boolean(keysHeld() & value))); \
		defGetter(buttonObj, "release", RETURN(jerry_create_boolean(keysUp() & value))); \
		jerry_release_value(buttonObj);
	FOR_BUTTONS(DEF_BUTTON_OBJECT);
	jerry_release_value(Button);

	jerry_value_t Touch = createObject(ref_global, "Touch");
	defGetter(Touch, "start", RETURN(jerry_create_boolean(keysDown() & KEY_TOUCH)));
	defGetter(Touch, "active", RETURN(jerry_create_boolean(keysHeld() & KEY_TOUCH)));
	defGetter(Touch, "end", RETURN(jerry_create_boolean(keysUp() & KEY_TOUCH)));
	setMethod(Touch, "getPosition", DS_touchGetPosition);
	jerry_release_value(Touch);

	jerry_value_t VRAM_obj = createObject(ref_global, "VRAM");
	jerry_value_t VRAM_A_obj = createObject(VRAM_obj, "A");
	setSymbol(VRAM_A_obj, symbol_LCD);
	setSymbol(VRAM_A_obj, symbol_MAIN_BG);
	setSymbol(VRAM_A_obj, symbol_MAIN_BG_0x20000);
	setSymbol(VRAM_A_obj, symbol_MAIN_BG_0x40000);
	setSymbol(VRAM_A_obj, symbol_MAIN_BG_0x60000);
	setSymbol(VRAM_A_obj, symbol_MAIN_SPRITE);
	setSymbol(VRAM_A_obj, symbol_MAIN_SPRITE_0x20000);
	setSymbol(VRAM_A_obj, symbol_TEXTURE_0);
	setSymbol(VRAM_A_obj, symbol_TEXTURE_1);
	setSymbol(VRAM_A_obj, symbol_TEXTURE_2);
	setSymbol(VRAM_A_obj, symbol_TEXTURE_3);
	setProperty(VRAM_obj, "B", VRAM_A_obj);
	jerry_release_value(VRAM_A_obj);
	jerry_value_t VRAM_C_obj = createObject(VRAM_obj, "C");
	setSymbol(VRAM_C_obj, symbol_LCD);
	setSymbol(VRAM_C_obj, symbol_ARM7_0);
	setSymbol(VRAM_C_obj, symbol_ARM7_1);
	setSymbol(VRAM_C_obj, symbol_MAIN_BG);
	setSymbol(VRAM_C_obj, symbol_MAIN_BG_0x20000);
	setSymbol(VRAM_C_obj, symbol_MAIN_BG_0x40000);
	setSymbol(VRAM_C_obj, symbol_MAIN_BG_0x60000);
	setSymbol(VRAM_C_obj, symbol_SUB_BG);
	setSymbol(VRAM_C_obj, symbol_TEXTURE_0);
	setSymbol(VRAM_C_obj, symbol_TEXTURE_1);
	setSymbol(VRAM_C_obj, symbol_TEXTURE_2);
	setSymbol(VRAM_C_obj, symbol_TEXTURE_3);
	jerry_release_value(VRAM_C_obj);
	jerry_value_t VRAM_D_obj = createObject(VRAM_obj, "D");
	setSymbol(VRAM_D_obj, symbol_LCD);
	setSymbol(VRAM_D_obj, symbol_ARM7_0);
	setSymbol(VRAM_D_obj, symbol_ARM7_1);
	setSymbol(VRAM_D_obj, symbol_MAIN_BG);
	setSymbol(VRAM_D_obj, symbol_MAIN_BG_0x20000);
	setSymbol(VRAM_D_obj, symbol_MAIN_BG_0x40000);
	setSymbol(VRAM_D_obj, symbol_MAIN_BG_0x60000);
	setSymbol(VRAM_D_obj, symbol_SUB_SPRITE);
	setSymbol(VRAM_D_obj, symbol_TEXTURE_0);
	setSymbol(VRAM_D_obj, symbol_TEXTURE_1);
	setSymbol(VRAM_D_obj, symbol_TEXTURE_2);
	setSymbol(VRAM_D_obj, symbol_TEXTURE_3);
	jerry_release_value(VRAM_D_obj);
	jerry_value_t VRAM_E_obj = createObject(VRAM_obj, "E");
	setSymbol(VRAM_E_obj, symbol_LCD);
	setSymbol(VRAM_E_obj, symbol_MAIN_BG);
	setSymbol(VRAM_E_obj, symbol_MAIN_SPRITE);
	setSymbol(VRAM_E_obj, symbol_MAIN_BG_EXT_PALETTE_0);
	setSymbol(VRAM_E_obj, symbol_TEXTURE_PALETTE_0);
	jerry_release_value(VRAM_E_obj);
	jerry_value_t VRAM_F_obj = createObject(VRAM_obj, "F");
	setSymbol(VRAM_F_obj, symbol_LCD);
	setSymbol(VRAM_F_obj, symbol_MAIN_BG);
	setSymbol(VRAM_F_obj, symbol_MAIN_BG_0x4000);
	setSymbol(VRAM_F_obj, symbol_MAIN_BG_0x10000);
	setSymbol(VRAM_F_obj, symbol_MAIN_BG_0x14000);
	setSymbol(VRAM_F_obj, symbol_MAIN_SPRITE);
	setSymbol(VRAM_F_obj, symbol_MAIN_SPRITE_0x4000);
	setSymbol(VRAM_F_obj, symbol_MAIN_SPRITE_0x10000);
	setSymbol(VRAM_F_obj, symbol_MAIN_SPRITE_0x20000);
	setSymbol(VRAM_F_obj, symbol_MAIN_BG_EXT_PALETTE_0);
	setSymbol(VRAM_F_obj, symbol_MAIN_BG_EXT_PALETTE_2);
	setSymbol(VRAM_F_obj, symbol_MAIN_SPRITE_EXT_PALETTE);
	setSymbol(VRAM_F_obj, symbol_TEXTURE_PALETTE_0);
	setSymbol(VRAM_F_obj, symbol_TEXTURE_PALETTE_1);
	setSymbol(VRAM_F_obj, symbol_TEXTURE_PALETTE_4);
	setSymbol(VRAM_F_obj, symbol_TEXTURE_PALETTE_5);
	setProperty(VRAM_obj, "G", VRAM_F_obj);
	jerry_release_value(VRAM_F_obj);
	jerry_value_t VRAM_H_obj = createObject(VRAM_obj, "H");
	setSymbol(VRAM_H_obj, symbol_LCD);
	setSymbol(VRAM_H_obj, symbol_SUB_BG);
	setSymbol(VRAM_H_obj, symbol_SUB_BG_EXT_PALETTE);
	jerry_release_value(VRAM_H_obj);
	jerry_value_t VRAM_I_obj = createObject(VRAM_obj, "I");
	setSymbol(VRAM_I_obj, symbol_LCD);
	setSymbol(VRAM_I_obj, symbol_SUB_BG_0x8000);
	setSymbol(VRAM_I_obj, symbol_SUB_SPRITE);
	setSymbol(VRAM_I_obj, symbol_SUB_SPRITE_EXT_PALETTE);
	jerry_release_value(VRAM_I_obj);
	setMethod(VRAM_obj, "setBankA", VRAM_setBankA);
	setMethod(VRAM_obj, "setBankB", VRAM_setBankB);
	setMethod(VRAM_obj, "setBankC", VRAM_setBankC);
	setMethod(VRAM_obj, "setBankD", VRAM_setBankD);
	setMethod(VRAM_obj, "setBankE", VRAM_setBankE);
	setMethod(VRAM_obj, "setBankF", VRAM_setBankF);
	setMethod(VRAM_obj, "setBankG", VRAM_setBankG);
	setMethod(VRAM_obj, "setBankH", VRAM_setBankH);
	setMethod(VRAM_obj, "setBankI", VRAM_setBankI);
	jerry_release_value(VRAM_obj);

	jerry_value_t Video = createObject(ref_global, "Video");
	jerry_value_t main = createObject(Video, "main");
	setMethod(main, "setBackdropColor", Video_main_setBackdropColor);
	setMethod(main, "setMode", Video_main_setMode);
	jerry_release_value(main);
	jerry_value_t sub = createObject(Video, "sub");
	setMethod(sub, "setBackdropColor", Video_sub_setBackdropColor);
	setMethod(sub, "setMode", Video_sub_setMode);
	jerry_release_value(sub);
	jerry_release_value(Video);

	JS_class Sprite = createClass(ref_global, "Sprite", IllegalConstructor);
	defGetterSetter(Sprite.prototype, "x", Sprite_get_x, Sprite_set_x);
	defGetterSetter(Sprite.prototype, "y", Sprite_get_y, Sprite_set_y);
	setMethod(Sprite.prototype, "setPosition", Sprite_setPosition);
	defGetterSetter(Sprite.prototype, "gfx", Sprite_get_gfx, Sprite_set_gfx);
	defGetterSetter(Sprite.prototype, "priority", Sprite_get_priority, Sprite_set_priority);
	defGetterSetter(Sprite.prototype, "hidden", Sprite_get_hidden, Sprite_set_hidden);
	defGetterSetter(Sprite.prototype, "flipH", Sprite_get_flipH, Sprite_set_flipH);
	defGetterSetter(Sprite.prototype, "flipV", Sprite_get_flipV, Sprite_set_flipV);
	defGetterSetter(Sprite.prototype, "affine", Sprite_get_affine, Sprite_set_affine);
	defGetterSetter(Sprite.prototype, "sizeDouble", Sprite_get_sizeDouble, Sprite_set_sizeDouble);
	defGetterSetter(Sprite.prototype, "mosaic", Sprite_get_mosaic, Sprite_set_mosaic);
	setMethod(Sprite.prototype, "remove", Sprite_remove);
	JS_class PalettedSprite = extendClass(ref_global, "PalettedSprite", IllegalConstructor, Sprite.prototype);
	defGetterSetter(PalettedSprite.prototype, "palette", Sprite_get_palette, Sprite_set_palette);
	ref_PalettedSprite = PalettedSprite;
	JS_class BitmapSprite = extendClass(ref_global, "BitmapSprite", IllegalConstructor, Sprite.prototype);
	defGetterSetter(BitmapSprite.prototype, "alpha", Sprite_get_palette, Sprite_set_palette);
	ref_BitmapSprite = BitmapSprite;
	jerry_value_t SpriteEngine = jerry_create_object();
	setMethod(SpriteEngine, "init", SpriteEngine_init);
	setMethod(SpriteEngine, "enable", SpriteEngine_enable);
	setMethod(SpriteEngine, "disable", SpriteEngine_disable);
	setMethod(SpriteEngine, "addSprite", SpriteEngine_addSprite);
	setMethod(SpriteEngine, "addGraphic", SpriteEngine_addGraphic);
	setMethod(SpriteEngine, "addAffineMatrix", SpriteEngine_addAffineMatrix);
	setMethod(SpriteEngine, "setMosaic", SpriteEngine_setMosaic);
	main = createObject(Sprite.constructor, "main");
	jerry_set_internal_property(main, ref_str_main, JS_TRUE);
	jerry_value_t mainSpritePaletteArrayBuffer = jerry_create_arraybuffer_external(256 * sizeof(u16), (u8*) SPRITE_PALETTE, [](void * _){});
	jerry_value_t mainSpritePaletteTypedArray = jerry_create_typedarray_for_arraybuffer_sz(JERRY_TYPEDARRAY_UINT16, mainSpritePaletteArrayBuffer, 0, 256);
	setReadonly(main, "palette", mainSpritePaletteTypedArray);
	jerry_release_value(mainSpritePaletteTypedArray);
	jerry_release_value(mainSpritePaletteArrayBuffer);
	setPrototype(main, SpriteEngine);
	jerry_release_value(main);
	sub = createObject(Sprite.constructor, "sub");
	jerry_set_internal_property(sub, ref_str_main, JS_FALSE);
	jerry_value_t subSpritePaletteArrayBuffer = jerry_create_arraybuffer_external(256 * sizeof(u16), (u8*) SPRITE_PALETTE_SUB, [](void * _){});
	jerry_value_t subSpritePaletteTypedArray = jerry_create_typedarray_for_arraybuffer_sz(JERRY_TYPEDARRAY_UINT16, subSpritePaletteArrayBuffer, 0, 256);
	setReadonly(sub, "palette", subSpritePaletteTypedArray);
	jerry_release_value(subSpritePaletteTypedArray);
	jerry_release_value(subSpritePaletteArrayBuffer);
	setPrototype(sub, SpriteEngine);
	jerry_release_value(sub);
	jerry_release_value(SpriteEngine);
	ref_Sprite = Sprite;

	JS_class SpriteGraphic = createClass(ref_global, "SpriteGraphic", IllegalConstructor);
	defGetter(SpriteGraphic.prototype, "width", SpriteGraphic_get_width);
	defGetter(SpriteGraphic.prototype, "height", SpriteGraphic_get_height);
	setMethod(SpriteGraphic.prototype, "remove", SpriteGraphic_remove);
	ref_SpriteGraphic = SpriteGraphic;

	JS_class SpriteAffineMatrix = createClass(ref_global, "SpriteAffineMatrix", IllegalConstructor);
	defGetterSetter(SpriteAffineMatrix.prototype, "hdx", SpriteAffineMatrix_get_hdx, SpriteAffineMatrix_set_hdx);
	defGetterSetter(SpriteAffineMatrix.prototype, "hdy", SpriteAffineMatrix_get_hdy, SpriteAffineMatrix_set_hdy);
	defGetterSetter(SpriteAffineMatrix.prototype, "vdx", SpriteAffineMatrix_get_vdx, SpriteAffineMatrix_set_vdx);
	defGetterSetter(SpriteAffineMatrix.prototype, "vdy", SpriteAffineMatrix_get_vdy, SpriteAffineMatrix_set_vdy);
	setMethod(SpriteAffineMatrix.prototype, "rotateScale", SpriteAffineMatrix_rotateScale);
	setMethod(SpriteAffineMatrix.prototype, "remove", SpriteAffineMatrix_remove);
	ref_SpriteAffineMatrix = SpriteAffineMatrix;

	exposeBetaAPI();
}

void releaseReferences() {
	jerry_release_value(ref_global);
	releaseClass(ref_Event);
	releaseClass(ref_Error);
	releaseClass(ref_File);
	releaseClass(ref_Sprite);
	releaseClass(ref_PalettedSprite);
	releaseClass(ref_BitmapSprite);
	releaseClass(ref_SpriteGraphic);
	releaseClass(ref_SpriteAffineMatrix);
	jerry_release_value(ref_consoleCounters);
	jerry_release_value(ref_consoleTimers);
	jerry_release_value(ref_storage);
	jerry_release_value(ref_func_push);
	jerry_release_value(ref_func_slice);
	jerry_release_value(ref_func_splice);
	jerry_release_value(ref_str_name);
	jerry_release_value(ref_str_constructor);
	jerry_release_value(ref_str_prototype);
	jerry_release_value(ref_str_backtrace);
	jerry_release_value(ref_str_main);
	jerry_release_value(ref_str_removed);
	jerry_release_value(ref_sym_toStringTag);
	#define RELEASE_SYMBOL(name) jerry_release_value(symbol_##name)
	FOR_CUSTOM_SYMBOL_NAMES(RELEASE_SYMBOL)
}