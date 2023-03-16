#include "api.hpp"

#include <dirent.h>
#include <nds/arm9/input.h>
#include <nds/arm9/video.h>
#include <nds/arm9/background.h>
#include <nds/interrupts.h>
extern "C" {
#include <nds/system.h>
}
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
#include "input.hpp"
#include "io/console.hpp"
#include "io/keyboard.hpp"
#include "jerry/jerryscript.h"
#include "logging.hpp"
#include "timeouts.hpp"
#include "tonccpy.h"



jerry_value_t ref_global;
jerry_value_t ref_storage;
jerry_value_t ref_Event;
jerry_value_t ref_Error;
jerry_value_t ref_str_name;
jerry_value_t ref_str_constructor;
jerry_value_t ref_str_prototype;
jerry_value_t ref_str_backtrace;
jerry_value_t ref_sym_toStringTag;
jerry_value_t ref_consoleCounters;
jerry_value_t ref_consoleTimers;

const char ONE_ARG[] = "1 argument required.";

#define CALL_INFO const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], u32 argCount
#define FUNCTION(name) static jerry_value_t name(CALL_INFO)
#define LAMBDA(returnVal) [](CALL_INFO) -> jerry_value_t { return returnVal; }
#define REQUIRE_FIRST() if (argCount == 0) return throwTypeError(ONE_ARG)
#define REQUIRE(n) if (argCount < n) return throwTypeError(#n " arguments required.")
#define EXPECT(test, type) if (!(test)) return throwTypeError("Expected type '" #type "'.")
#define CONSTRUCTOR(name) if (isNewTargetUndefined()) return throwTypeError("Constructor '" #name "' cannot be invoked without 'new'.")

FUNCTION(IllegalConstructor) {
	return throwTypeError("Illegal constructor");
}

FUNCTION(closeJSDS) {
	abortFlag = true;
	userClosed = true;
	return jerry_create_abort_from_value(createString("close() was called."), true);
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
		jerry_value_t responseStr = jerry_create_string_sz_from_utf8((jerry_char_t *) response, (jerry_size_t) responseSize);
		free(response);
		keyboardUpdate();
		pauseKeyEvents = false;
		return responseStr;
	}
	else {
		putchar('\n');
		keyboardUpdate();
		pauseKeyEvents = false;
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
		logIndent();
		log(args, argCount);
		consoleSetColor(previousColor);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_error) {
	if (argCount > 0) {
		u16 previousColor = consoleSetColor(LOGCOLOR_ERROR);
		logIndent();
		log(args, argCount);
		consoleSetColor(previousColor);
	}
	return JS_UNDEFINED;
}

FUNCTION(console_assert) {
	if (argCount == 0 || !jerry_value_to_boolean(args[0])) {
		u16 previousColor = consoleSetColor(LOGCOLOR_ERROR);
		logIndent();
		printf("Assertion failed: ");
		log(args + 1, argCount - 1);
		consoleSetColor(previousColor);
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
	else labelStr = createString("default");
	
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
	else labelStr = createString("default");

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
	else labelStr = createString("default");

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
	else labelStr = createString("default");

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
	else labelStr = createString("default");

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
			return throwTypeError("Text size is too big to encode into the given array.");
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
	if (!jerry_is_valid_utf8_string(data, dataLen)) return throwTypeError("Invalid UTF-8");
	return jerry_create_string_sz_from_utf8(data, dataLen);
}

FUNCTION(Text_encodeUTF16) {
	REQUIRE_FIRST();
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) {
		EXPECT(jerry_get_typedarray_type(args[1]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	}
	jerry_size_t utf8Size;
	char *utf8 = getAsString(args[0], &utf8Size);
	u8 utf16[utf8Size * 2];
	char16_t *out = (char16_t *) utf16;
	for (jerry_size_t i = 0; i < utf8Size; i++) {
		u8 byte = utf8[i];
		if (byte < 0x80) *(out++) = byte;
		else if (byte < 0xE0) {
			u8 byte2 = utf8[++i]; // Jerry validates its strings, so this shouldn't be out of range
			*(out++) = (byte & 0b11111) << 6 | (byte2 & 0b111111);
		}
		else if (byte < 0xF0) {
			u8 byte2 = utf8[++i], byte3 = utf8[++i];
			*(out++) = (byte & 0xF) << 12 | (byte2 & 0b111111) << 6 | (byte3 & 0b111111);
		}
		else {
			u8 byte2 = utf8[++i], byte3 = utf8[++i], byte4 = utf8[++i];
			char32_t codepoint = (byte & 0b111) << 18 | (byte2 & 0b111111) << 12 | (byte3 & 0b111111) << 6 | (byte4 & 0b111111);
			codepoint -= 0x10000;
			*(out++) = 0xD800 | codepoint >> 10;
			*(out++) = 0xDC00 | (codepoint & 0x3FF);
		}
	}
	free(utf8);
	jerry_size_t utf16Size = ((u8 *) out) - utf16;
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) {
		jerry_length_t byteOffset, bufSize;
		jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[1], &byteOffset, &bufSize);
		u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
		jerry_release_value(arrayBuffer);
		if (utf16Size > bufSize) {
			return throwTypeError("Text size is too big to encode into the given array.");
		}
		tonccpy(data, utf16, utf16Size);
		return jerry_acquire_value(args[1]);
	}
	jerry_value_t arrayBuffer = jerry_create_arraybuffer(utf16Size);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer);
	tonccpy(data, utf16, utf16Size);
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
	return createStringUTF16((char16_t *) data, dataLen / 2);
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

	return jerry_create_string_sz_from_utf8((jerry_char_t *) ascii, asciiSize);
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
		return throwTypeError(errorMsg);
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
	if (badEncoding) return throwTypeError(errorMsg);

	jerry_value_t u8Array;
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) u8Array = jerry_acquire_value(args[1]);
	else u8Array = jerry_create_typedarray(JERRY_TYPEDARRAY_UINT8, dataLen);
	jerry_length_t byteOffset, arraySize;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(u8Array, &byteOffset, &arraySize);
	if (arraySize < dataLen) {
		jerry_release_value(arrayBuffer);
		jerry_release_value(u8Array);
		return throwTypeError("Data size is too big to decode into the given array.");
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
		return throwError("Unable to read in current file mode.");
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
		return throwError("File read failed.");
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
		return throwError("Unable to write in current file mode.");
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
		return throwError("File write failed.");
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
	if (mode == 10) return throwTypeError("Invalid seek mode");

	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	int success = fseek(file, jerry_value_as_int32(args[0]), mode);
	if (success != 0) return throwError("File seek failed.");
	return JS_UNDEFINED;
}

FUNCTION(File_close) {
	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	if (fclose(file) != 0) return throwError("File close failed.");
	return JS_UNDEFINED;
}

FUNCTION(FileStatic_open) {
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
			return throwTypeError("Invalid file mode");
		}
	}

	FILE *file = fopen(path, mode);
	if (file == NULL) {
		if (mode != defaultMode) free(mode);
		free(path);
		return throwError("Unable to open file.");
	}
	else {
		jerry_value_t modeStr = createString(mode);
		jerry_value_t fileObj = newFile(file, modeStr);
		jerry_release_value(modeStr);
		if (mode != defaultMode) free(mode);
		free(path);
		return fileObj;
	}
}

FUNCTION(FileStatic_copy) {
	REQUIRE(2);
	char *sourcePath = getAsString(args[0]);
	FILE *source = fopen(sourcePath, "r");
	free(sourcePath);
	if (source == NULL) return throwError("Unable to open source file during copy.");
	
	fseek(source, 0, SEEK_END);
	u32 sourceSize = ftell(source);
	rewind(source);
	u8 *data = (u8 *) malloc(sourceSize);
	u32 bytesRead = fread(data, 1, sourceSize, source);
	if (ferror(source)) {
		free(data);
		fclose(source);
		return throwError("Failed to read source file during copy.");
	}
	fclose(source);

	char *destPath = getAsString(args[1]);
	FILE *dest = fopen(destPath, "w");
	free(destPath);
	if (dest == NULL) {
		free(data);
		return throwError("Unable to open destination file during copy.");
	}

	fwrite(data, 1, bytesRead, dest);
	free(data);
	if (ferror(dest)) {
		fclose(dest);
		return throwError("Failed to write destination file during copy.");
	}
	fclose(dest);
	return JS_UNDEFINED;
}

FUNCTION(FileStatic_rename) {
	REQUIRE(2);
	char *sourcePath = getAsString(args[0]);
	char *destPath = getAsString(args[1]);
	int status = rename(sourcePath, destPath);
	free(sourcePath);
	free(destPath);
	if (status != 0) return throwError("Failed to rename file.");
	return JS_UNDEFINED;
}

FUNCTION(FileStatic_remove) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	if (remove(path) != 0) return throwError("Failed to delete file.");
	return JS_UNDEFINED;
}

FUNCTION(FileStatic_read) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "r");
	free(path);
	if (file == NULL) return throwError("Unable to open file.");

	fseek(file, 0, SEEK_END);
	u32 fileSize = ftell(file);
	rewind(file);

	jerry_value_t arrayBuffer = jerry_create_arraybuffer(fileSize);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer);
	u32 bytesRead = fread(buf, 1, fileSize, file);
	if (ferror(file)) {
		jerry_release_value(arrayBuffer);
		fclose(file);
		return throwError("File read failed.");
	}
	fclose(file);
	jerry_value_t u8Array = jerry_create_typedarray_for_arraybuffer_sz(JERRY_TYPEDARRAY_UINT8, arrayBuffer, 0, bytesRead);
	jerry_release_value(arrayBuffer);
	return u8Array;
}

FUNCTION(FileStatic_readText) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "r");
	free(path);
	if (file == NULL) return throwError("Unable to open file.");

	fseek(file, 0, SEEK_END);
	u32 fileSize = ftell(file);
	rewind(file);

	char *buf = (char *) malloc(fileSize);
	u32 bytesRead = fread(buf, 1, fileSize, file);
	if (ferror(file)) {
		free(buf);
		fclose(file);
		return throwError("File read failed.");
	}
	fclose(file);
	jerry_value_t str = jerry_create_string_sz_from_utf8((jerry_char_t *) buf, bytesRead);
	free(buf);
	return str;
}

FUNCTION(FileStatic_write) {
	REQUIRE(2);
	EXPECT(jerry_get_typedarray_type(args[1]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "w");
	free(path);
	if (file == NULL) return throwError("Unable to open file.");
	
	jerry_length_t offset, bufSize;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[1], &offset, &bufSize);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer) + offset;
	jerry_release_value(arrayBuffer);

	u32 bytesWritten = fwrite(buf, 1, bufSize, file);
	if (ferror(file)) {
		fclose(file);
		return throwError("File write failed.");
	}
	fclose(file);
	return jerry_create_number(bytesWritten);
}

FUNCTION(FileStatic_writeText) {
	REQUIRE(2);
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "w");
	free(path);
	if (file == NULL) return throwError("Unable to open file.");
	
	jerry_size_t textSize;
	char *text = getAsString(args[1], &textSize);
	u32 bytesWritten = fwrite(text, 1, textSize, file);
	if (ferror(file)) {
		fclose(file);
		free(text);
		return throwError("File write failed.");
	}
	fclose(file);
	free(text);
	return jerry_create_number(bytesWritten);
}

FUNCTION(FileStatic_makeDir) {
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
	if (status != 0) return throwError("Failed to make directory.");
	return JS_UNDEFINED;
}

FUNCTION(FileStatic_readDir) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	DIR *dir = opendir(path);
	free(path);
	if (dir == NULL) return throwError("Unable to open directory.");

	jerry_value_t dirArr = jerry_create_array(0);
	jerry_value_t pushFunc = getProperty(dirArr, "push");
	dirent *entry = readdir(dir);
	while (entry != NULL) {
		jerry_value_t entryObj = jerry_create_object();
		setProperty(entryObj, "isDirectory", jerry_create_boolean(entry->d_type == DT_DIR));
		setProperty(entryObj, "isFile", jerry_create_boolean(entry->d_type == DT_REG));
		jerry_value_t nameStr = createString(entry->d_name);
		setProperty(entryObj, "name", nameStr);
		jerry_release_value(nameStr);
		jerry_call_function(pushFunc, dirArr, &entryObj, 1);
		jerry_release_value(entryObj);
		entry = readdir(dir);
	}
	closedir(dir);
	jerry_release_value(pushFunc);
	return dirArr;
}

FUNCTION(FileStatic_browse) {
	char browsePathDefault[] = ".";
	char messageDefault[] = "Select a file.";
	char *browsePath = browsePathDefault;
	char *message = messageDefault;
	std::vector<char *> extensions;
	if (argCount > 0 && jerry_value_is_object(args[0])) {
		jerry_value_t pathProp = createString("path");
		jerry_value_t extensionsProp = createString("extensions");
		jerry_value_t messageProp = createString("message");
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
	char *result = fileBrowse(message, browsePath, extensions);
	jerry_value_t resultVal = result == NULL ? JS_NULL : createString(result);
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
	jerry_value_t cancelableBool = getInternalProperty(thisValue, "cancelable");
	if (jerry_value_to_boolean(cancelableBool)) {
		setInternalProperty(thisValue, "defaultPrevented", JS_TRUE);
	}
	jerry_release_value(cancelableBool);
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
	
	jerry_value_t callbackProp = createString("callback");
	jerry_value_t onceProp = createString("once");

	jerry_value_t typeStr = jerry_value_to_string(args[0]);
	jerry_value_t callbackVal = args[1];
	bool once = false;
	if (argCount > 2 && jerry_value_is_object(args[2])) {
		jerry_value_t onceVal = jerry_get_property(args[2], onceProp);
		once = jerry_value_to_boolean(onceVal);
		jerry_release_value(onceVal);
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
		jerry_value_t storedListener = jerry_get_property_by_index(listenersArr, i);
		jerry_value_t storedCallbackVal = jerry_get_property(storedListener, callbackProp);
		jerry_value_t callbackEqualityBool = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, callbackVal, storedCallbackVal);
		if (jerry_get_boolean_value(callbackEqualityBool)) shouldAppend = false;
		jerry_release_value(callbackEqualityBool);
		jerry_release_value(storedCallbackVal);
		jerry_release_value(storedListener);
	}

	if (shouldAppend) {
		jerry_value_t listenerObj = jerry_create_object();

		jerry_release_value(jerry_set_property(listenerObj, callbackProp, callbackVal));
		jerry_release_value(jerry_set_property(listenerObj, onceProp, jerry_create_boolean(once)));
		setProperty(listenerObj, "removed", JS_FALSE);

		jerry_value_t pushFunc = getProperty(listenersArr, "push");
		jerry_release_value(jerry_call_function(pushFunc, listenersArr, &listenerObj, 1));
		jerry_release_value(pushFunc);
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
	jerry_value_t callbackProp = createString("callback");
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
			jerry_value_t callbackEqualityBool = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, callbackVal, storedCallbackVal);
			if (jerry_get_boolean_value(callbackEqualityBool)) {
				jerry_value_t spliceFunc = getProperty(listenersArr, "splice");
				jerry_value_t spliceArgs[2] = {jerry_create_number(i), jerry_create_number(1)};
				jerry_release_value(jerry_call_function(spliceFunc, listenersArr, spliceArgs, 2));
				jerry_release_value(spliceArgs[1]);
				jerry_release_value(spliceArgs[0]);
				jerry_release_value(spliceFunc);
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
			jerry_release_value(callbackEqualityBool);
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
	REQUIRE_FIRST();
	jerry_value_t isInstanceBool = jerry_binary_operation(JERRY_BIN_OP_INSTANCEOF, args[0], ref_Event);
	bool isInstance = jerry_get_boolean_value(isInstanceBool);
	jerry_release_value(isInstanceBool);
	EXPECT(isInstance, Event);
	jerry_value_t targetObj = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	bool canceled = dispatchEvent(targetObj, args[0], true);
	return jerry_create_boolean(!canceled);
}

FUNCTION(DS_getBatteryLevel) {
	u32 level = getBatteryLevel();
	if (level & BIT(7)) return createString("charging");
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
	return throwTypeError("Invalid screen value");
}

FUNCTION(DS_sleep) {
	jerry_value_t eventArgs[2] = {createString("sleep"), jerry_create_object()};
	setProperty(eventArgs[1], "cancelable", JS_TRUE);
	jerry_value_t eventObj = jerry_construct_object(ref_Event, eventArgs, 2);
	bool canceled = dispatchEvent(ref_global, eventObj, true);
	jerry_release_value(eventObj);
	jerry_release_value(eventArgs[0]);
	jerry_release_value(eventArgs[1]);
	if (!canceled) {
		systemSleep();
		swiWaitForVBlank();
		swiWaitForVBlank(); // I know this is jank but it's the easiest solution to stop 'wake' from dispatching before the system sleeps
		eventArgs[0] = createString("wake");
		eventArgs[1] = jerry_create_object();
		eventObj = jerry_construct_object(ref_Event, eventArgs, 2);
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
	// hold some internal references
	ref_str_name = createString("name");
	ref_str_constructor = createString("constructor");
	ref_str_prototype = createString("prototype");
	ref_str_backtrace = createString("backtrace");
	ref_sym_toStringTag = jerry_get_well_known_symbol(JERRY_SYMBOL_TO_STRING_TAG);
	ref_consoleCounters = jerry_create_object();
	ref_consoleTimers = jerry_create_object();
	ref_storage = jerry_create_object();

	ref_global = jerry_get_global_object();
	setProperty(ref_global, "self", ref_global);
	ref_Error = getProperty(ref_global, "Error");

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
	jerry_release_value(console);

	jerry_value_t keyboard = createObject(ref_global, "keyboard");
	setMethod(keyboard, "hide", LAMBDA((keyboardHide(), JS_UNDEFINED)));
	setMethod(keyboard, "show", LAMBDA((keyboardShow(), JS_UNDEFINED)));
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
	setMethod(File.constructor, "open", FileStatic_open);
	setMethod(File.constructor, "copy", FileStatic_copy);
	setMethod(File.constructor, "rename", FileStatic_rename);
	setMethod(File.constructor, "remove", FileStatic_remove);
	setMethod(File.constructor, "read", FileStatic_read);
	setMethod(File.constructor, "readText", FileStatic_readText);
	setMethod(File.constructor, "write", FileStatic_write);
	setMethod(File.constructor, "writeText", FileStatic_writeText);
	setMethod(File.constructor, "makeDir", FileStatic_makeDir);
	setMethod(File.constructor, "readDir", FileStatic_readDir);
	setMethod(File.constructor, "browse", FileStatic_browse);
	releaseClass(File);

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
	jerry_release_value(jerry_set_prototype(ref_global, EventTarget.prototype));
	EventTargetConstructor(EventTarget.constructor, ref_global, NULL, 0);
	releaseClass(EventTarget);

	JS_class Event = createClass(ref_global, "Event", EventConstructor);
	setMethod(Event.prototype, "stopImmediatePropagation", Event_stopImmediatePropagation);
	setMethod(Event.prototype, "preventDefault", Event_preventDefault);
	ref_Event = Event.constructor;
	jerry_release_value(Event.prototype);

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
	setMethod(DS, "getMainScreen", LAMBDA(createString(REG_POWERCNT & POWER_SWAP_LCDS ? "top" : "bottom")));
	setReadonly(DS, "isDSiMode", jerry_create_boolean(isDSiMode()));
	setMethod(DS, "setMainScreen", DS_setMainScreen);
	setMethod(DS, "shutdown", LAMBDA((systemShutDown(), JS_UNDEFINED)));
	setMethod(DS, "sleep", DS_sleep);
	setMethod(DS, "swapScreens", LAMBDA((lcdSwap(), JS_UNDEFINED)));

	jerry_value_t profile = createObject(DS, "profile");
	setReadonlyNumber(profile, "alarmHour", PersonalData->alarmHour);
	setReadonlyNumber(profile, "alarmMinute", PersonalData->alarmMinute);
	setReadonlyNumber(profile, "birthDay", PersonalData->birthDay);
	setReadonlyNumber(profile, "birthMonth", PersonalData->birthMonth);
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
	setReadonlyStringUTF16(profile, "name", (char16_t *) PersonalData->name, PersonalData->nameLen);
	setReadonlyStringUTF16(profile, "message", (char16_t *) PersonalData->message, PersonalData->messageLen);
	#pragma GCC diagnostic pop
	const u16 themeColors[16] = {0xCE0C, 0x8137, 0x8C1F, 0xFE3F, 0x825F, 0x839E, 0x83F5, 0x83E0, 0x9E80, 0xC769, 0xFAE6, 0xF960, 0xC800, 0xE811, 0xF41A, 0xC81F};
	setReadonlyNumber(profile, "color", PersonalData->theme < 16 ? themeColors[PersonalData->theme] : 0);
	setReadonly(profile, "autoMode", jerry_create_boolean(PersonalData->autoMode));
	setReadonlyString(profile, "gbaScreen", PersonalData->gbaScreen ? "bottom" : "top");
	const char languages[8][10] = {"日本語", "English", "Français", "Deutsch", "Italiano", "Español", "中文", "한국어"};
	setReadonlyString(profile, "language", PersonalData->language < 8 ? languages[PersonalData->language] : "");
	jerry_release_value(profile);

	jerry_value_t buttons = createObject(DS, "buttons");
	jerry_value_t pressed = createObject(buttons, "pressed");
	jerry_value_t held = createObject(buttons, "held");
	jerry_value_t released = createObject(buttons, "released");
	#define DEF_GETTER_KEY_DOWN(name, value) defGetter(pressed, name, LAMBDA(jerry_create_boolean(keysDown() & value)));
	#define DEF_GETTER_KEY_HELD(name, value) defGetter(held, name, LAMBDA(jerry_create_boolean(keysHeld() & value)));
	#define DEF_GETTER_KEY_UP(name, value) defGetter(released, name, LAMBDA(jerry_create_boolean(keysUp() & value)));
	FOR_BUTTONS(DEF_GETTER_KEY_DOWN);
	FOR_BUTTONS(DEF_GETTER_KEY_HELD);
	FOR_BUTTONS(DEF_GETTER_KEY_UP);
	jerry_release_value(pressed);
	jerry_release_value(held);
	jerry_release_value(released);
	jerry_release_value(buttons);

	jerry_value_t touch = createObject(DS, "touch");
	defGetter(touch, "start", LAMBDA(jerry_create_boolean(keysDown() & KEY_TOUCH)));
	defGetter(touch, "active", LAMBDA(jerry_create_boolean(keysHeld() & KEY_TOUCH)));
	defGetter(touch, "end", LAMBDA(jerry_create_boolean(keysUp() & KEY_TOUCH)));
	setMethod(touch, "getPosition", DS_touchGetPosition);
	jerry_release_value(touch);

	jerry_release_value(DS);
	exposeBetaAPI();
}

void releaseReferences() {
	jerry_release_value(ref_global);
	jerry_release_value(ref_storage);
	jerry_release_value(ref_Event);
	jerry_release_value(ref_Error);
	jerry_release_value(ref_str_name);
	jerry_release_value(ref_str_constructor);
	jerry_release_value(ref_str_prototype);
	jerry_release_value(ref_str_backtrace);
	jerry_release_value(ref_sym_toStringTag);
	jerry_release_value(ref_consoleCounters);
	jerry_release_value(ref_consoleTimers);
}