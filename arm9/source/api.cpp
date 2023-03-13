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
#include "inline.hpp"
#include "input.hpp"
#include "io/console.hpp"
#include "io/keyboard.hpp"
#include "jerry/jerryscript.h"
#include "logging.hpp"
#include "storage.hpp"
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
	return undefined;
}

FUNCTION(confirm) {
	if (argCount > 0) printValue(args[0]);
	else printf("Confirm");
	printf(" [ OK,  Cancel]\n");
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		u32 keys = keysDown();
		if (keys & KEY_A) return True;
		else if (keys & KEY_B) return False;
	}
}

FUNCTION(prompt) {
	if (argCount > 0) printValue(args[0]);
	else printf("Prompt");
	putchar(' ');
	pauseKeyEvents = true;
	keyboardCompose(true);
	ComposeStatus status = keyboardComposeStatus();
	while (status == COMPOSING) {
		swiWaitForVBlank();
		scanKeys();
		keyboardUpdate();
		status = keyboardComposeStatus();
	}
	if (status == FINISHED) {
		char *str;
		int strSize;
		keyboardComposeAccept(&str, &strSize);
		printf(str); putchar('\n');
		jerry_value_t strVal = jerry_create_string_sz_from_utf8((jerry_char_t *) str, (jerry_size_t) strSize);
		free(str);
		keyboardUpdate();
		pauseKeyEvents = false;
		return strVal;
	}
	else {
		putchar('\n');
		keyboardUpdate();
		pauseKeyEvents = false;
		if (argCount > 1) return jerry_value_to_string(args[1]);
		else return null;
	}
}

FUNCTION(setTimeout) {
	if (argCount >= 2) return addTimeout(args[0], args[1], (jerry_value_t *)(args) + 2, argCount - 2, false);
	else {
		jerry_value_t zero = jerry_create_number(0);
		jerry_value_t result = addTimeout(argCount > 0 ? args[0] : undefined, zero, NULL, 0, false);
		jerry_release_value(zero);
		return result;
	}
}

FUNCTION(setInterval) {
	if (argCount >= 2) return addTimeout(args[0], args[1], (jerry_value_t *)(args) + 2, argCount - 2, true);
	else {
		jerry_value_t zero = jerry_create_number(0);
		jerry_value_t result = addTimeout(argCount > 0 ? args[0] : undefined, zero, NULL, 0, true);
		jerry_release_value(zero);
		return result;
	}
}

FUNCTION(clearInterval) {
	if (argCount > 0) clearTimeout(args[0]);
	else clearTimeout(undefined);
	return undefined;
}

FUNCTION(console_log) {
	if (argCount > 0) {
		logIndent();
		log(args, argCount);
	}
	return undefined;
}

FUNCTION(console_info) {
	if (argCount > 0) {
		u16 prev = consoleSetColor(INFO);
		logIndent();
		log(args, argCount);
		consoleSetColor(prev);
	}
	return undefined;
}

FUNCTION(console_warn) {
	if (argCount > 0) {
		u16 prev = consoleSetColor(WARN);
		logIndent();
		log(args, argCount);
		consoleSetColor(prev);
	}
	return undefined;
}

FUNCTION(console_error) {
	if (argCount > 0) {
		u16 prev = consoleSetColor(ERROR);
		logIndent();
		log(args, argCount);
		consoleSetColor(prev);
	}
	return undefined;
}

FUNCTION(console_assert) {
	if (argCount == 0 || !jerry_value_to_boolean(args[0])) {
		u16 prev = consoleSetColor(ERROR);
		logIndent();
		printf("Assertion failed: ");
		log(args + 1, argCount - 1);
		consoleSetColor(prev);
	}
	return undefined;
}

FUNCTION(console_debug) {
	if (argCount > 0) {
		u16 prev = consoleSetColor(DEBUG);
		logIndent();
		log(args, argCount);
		consoleSetColor(prev);
	}
	return undefined;
}

FUNCTION(console_trace) {
	logIndent();
	if (argCount == 0) printf("Trace\n");
	else log(args, argCount);
	jerry_value_t backtrace = jerry_get_backtrace(10);
	u32 length = jerry_get_array_length(backtrace);
	for (u32 i = 0; i < length; i++) {
		jerry_value_t traceLine = jerry_get_property_by_index(backtrace, i);
		char *step = getString(traceLine);
		logIndent();
		printf(" @ %s\n", step);
		free(step);
		jerry_release_value(traceLine);
	}
	jerry_release_value(backtrace);
	return undefined;
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
	return undefined;
}

FUNCTION(console_table) {
	if (argCount > 0) logTable(args, argCount);
	return undefined;
}

FUNCTION(console_group) {
	logIndentAdd();
	if (argCount > 0) {
		logIndent();
		log(args, argCount);
	}
	return undefined;
}

FUNCTION(console_groupEnd) {
	logIndentRemove();
	return undefined;
}

FUNCTION(console_count) {
	jerry_value_t label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		label = jerry_value_to_string(args[0]);
	}
	else label = createString("default");
	
	jerry_value_t countVal = jerry_get_property(ref_consoleCounters, label);
	u32 count;
	if (jerry_value_is_undefined(label)) count = 1;
	else count = jerry_value_as_uint32(countVal) + 1;
	jerry_release_value(countVal);

	logIndent();
	printString(label);
	printf(": %lu\n", count);
	
	countVal = jerry_create_number(count);
	jerry_release_value(jerry_set_property(ref_consoleCounters, label, countVal));
	jerry_release_value(countVal);

	jerry_release_value(label);
	return undefined;
}

FUNCTION(console_countReset) {
	jerry_value_t label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		label = jerry_value_to_string(args[0]);
	}
	else label = createString("default");

	jerry_value_t hasLabel = jerry_has_own_property(ref_consoleCounters, label);
	if (jerry_get_boolean_value(hasLabel)) {
		jerry_value_t zero = jerry_create_number(0);
		jerry_set_property(ref_consoleCounters, label, zero);
		jerry_release_value(zero);
	}
	else {
		logIndent();
		printf("Count for '");
		printString(label);
		printf("' does not exist\n");
	}
	jerry_release_value(hasLabel);

	jerry_release_value(label);
	return undefined;
}

FUNCTION(console_time) {
	jerry_value_t label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		label = jerry_value_to_string(args[0]);
	}
	else label = createString("default");

	jerry_value_t hasLabel = jerry_has_own_property(ref_consoleTimers, label);
	if (jerry_get_boolean_value(hasLabel)) {
		logIndent();
		printf("Timer '");
		printString(label);
		printf("' already exists\n");
	}
	else {
		jerry_value_t counterId = jerry_create_number(counterAdd());
		jerry_release_value(jerry_set_property(ref_consoleTimers, label, counterId));
		jerry_release_value(counterId);
	}
	jerry_release_value(hasLabel);
	
	jerry_release_value(label);
	return undefined;
}

FUNCTION(console_timeLog) {
	jerry_value_t label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		label = jerry_value_to_string(args[0]);
	}
	else label = createString("default");

	logIndent();
	jerry_value_t counterVal = jerry_get_property(ref_consoleTimers, label);
	if (jerry_value_is_undefined(counterVal)) {
		printf("Timer '");
		printString(label);
		printf("' does not exist\n");
	}
	else {
		int counterId = jerry_value_as_int32(counterVal);
		printString(label);
		printf(": %i ms", counterGet(counterId));
		if (argCount > 1) {
			putchar(' ');
			log(args + 1, argCount - 1);
		}
	}
	jerry_release_value(counterVal);

	jerry_release_value(label);
	return undefined;
}

FUNCTION(console_timeEnd) {
	jerry_value_t label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		label = jerry_value_to_string(args[0]);
	}
	else label = createString("default");

	logIndent();
	jerry_value_t counterVal = jerry_get_property(ref_consoleTimers, label);
	if (jerry_value_is_undefined(counterVal)) {
		printf("Timer '");
		printString(label);
		printf("' does not exist\n");
	}
	else {
		int counterId = jerry_value_as_int32(counterVal);
		printString(label);
		printf(": %i ms\n", counterGet(counterId));
		counterRemove(counterId);
		jerry_delete_property(ref_consoleTimers, label);
	}
	jerry_release_value(counterVal);

	jerry_release_value(label);
	return undefined;
}

FUNCTION(console_clear) {
	consoleClear();
	return undefined;
}

FUNCTION(Text_encode) {
	REQUIRE_FIRST();
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) {
		EXPECT(jerry_value_is_typedarray(args[1]), Uint8Array);
		jerry_value_t text = jerry_value_to_string(args[0]);
		jerry_length_t size = jerry_get_utf8_string_size(text);
		jerry_length_t byteOffset, bufSize;
		jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[1], &byteOffset, &bufSize);
		u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
		jerry_release_value(arrayBuffer);
		if (size > bufSize) {
			jerry_release_value(text);
			return throwTypeError("Text size is too big to encode into the given array.");
		}
		jerry_string_to_utf8_char_buffer(text, data, size);
		jerry_release_value(text);
		return jerry_acquire_value(args[1]);
	}
	jerry_value_t text = jerry_value_to_string(args[0]);
	jerry_length_t size = jerry_get_utf8_string_size(text);
	jerry_value_t arrayBuffer = jerry_create_arraybuffer(size);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer);
	jerry_string_to_utf8_char_buffer(text, data, size);
	jerry_value_t u8Array = jerry_create_typedarray_for_arraybuffer(JERRY_TYPEDARRAY_UINT8, arrayBuffer);
	jerry_release_value(text);
	jerry_release_value(arrayBuffer);
	return u8Array;
}

FUNCTION(Text_decode) {
	REQUIRE_FIRST(); EXPECT(jerry_value_is_typedarray(args[0]), Uint8Array);
	u32 byteOffset = 0, dataLen = 0;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &byteOffset, &dataLen);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
	jerry_release_value(arrayBuffer);
	if (!jerry_is_valid_utf8_string(data, dataLen)) return throwTypeError("Invalid UTF-8");
	return jerry_create_string_sz_from_utf8(data, dataLen);
}

FUNCTION(Text_encodeUTF16) {
	REQUIRE_FIRST();
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) {
		EXPECT(jerry_value_is_typedarray(args[1]), Uint8Array);
	}
	jerry_length_t utf8Size;
	char *utf8 = getAsString(args[0], &utf8Size);
	u8 utf16[utf8Size * 2];
	u16 *out = (u16 *) utf16;
	for (u32 i = 0; i < utf8Size; i++) {
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
			u32 codepoint = (byte & 0b111) << 18 | (byte2 & 0b111111) << 12 | (byte3 & 0b111111) << 6 | (byte4 & 0b111111);
			codepoint -= 0x10000;
			*(out++) = 0xD800 | codepoint >> 10;
			*(out++) = 0xDC00 | (codepoint & 0x3FF);
		}
	}
	free(utf8);
	jerry_length_t utf16Size = ((u8 *) out) - utf16;
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
	REQUIRE_FIRST(); EXPECT(jerry_value_is_typedarray(args[0]), Uint8Array);
	u32 byteOffset = 0, dataLen = 0;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &byteOffset, &dataLen);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
	jerry_release_value(arrayBuffer);
	return createStringU16((u16 *) data, dataLen / 2);
}

const char b64Map[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
FUNCTION(Base64_encode) {
	REQUIRE_FIRST(); EXPECT(jerry_value_is_typedarray(args[0]), Uint8Array);
	jerry_length_t byteOffset, dataSize;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &byteOffset, &dataSize);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
	jerry_release_value(arrayBuffer);

	const u32 asciiSize = (dataSize + 2) / 3 * 4;
	char ascii[asciiSize]; // base64 needs 4 chars to encode 3 bytes
	char *out = ascii;
	u32 i = 2;
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
		EXPECT(jerry_value_is_typedarray(args[1]), Uint8Array);
	}
	jerry_length_t asciiSize;
	char *ascii = getString(args[0], &asciiSize);
	const char errorMsg[] = "Unable to decode Base64.";

	if (asciiSize % 4 == 1) {
		free(ascii);
		return throwTypeError(errorMsg);
	}
	u32 dataLen = asciiSize / 4 * 3;
	if (ascii[asciiSize - 1] == '=') dataLen--;
	if (ascii[asciiSize - 2] == '=') dataLen--;

	u8 data[dataLen];
	u8 *out = data;
	u32 asciiIdx = 0, dataIdx = 2;
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
	
	u32 size = jerry_value_as_uint32(args[0]);
	jerry_value_t arrayBuffer = jerry_create_arraybuffer(size);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer);
	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	u32 bytesRead = fread(buf, 1, size, file);
	if (ferror(file)) {
		jerry_release_value(arrayBuffer);
		return throwError("File read failed.");
	}
	else if (feof(file) && bytesRead == 0) {
		jerry_release_value(arrayBuffer);
		return null;
	}
	else {
		jerry_value_t u8Array = jerry_create_typedarray_for_arraybuffer_sz(JERRY_TYPEDARRAY_UINT8, arrayBuffer, 0, bytesRead);
		jerry_release_value(arrayBuffer);
		return u8Array;
	}
}

FUNCTION(File_write) {
	REQUIRE_FIRST();
	EXPECT(jerry_value_is_object(args[0]) && jerry_value_is_typedarray(args[0]) && jerry_get_typedarray_type(args[0]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);

	jerry_value_t modeStr = getInternalProperty(thisValue, "mode");
	char *mode = getString(modeStr);
	if (mode[0] != 'w' && mode[0] != 'a' && mode[1] != '+') {
		free(mode);
		jerry_release_value(modeStr);
		return throwError("Unable to write in current file mode.");
	}
	free(mode);
	jerry_release_value(modeStr);
	
	u32 offset, size;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &offset, &size);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer) + offset;
	jerry_release_value(arrayBuffer);
	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);

	u32 bytesWritten = fwrite(buf, 1, size, file);
	if (ferror(file)) {
		return throwError("File write failed.");
	}
	else return jerry_create_number(bytesWritten);
}

FUNCTION(File_seek) {
	REQUIRE_FIRST();

	int mode = 10;
	if (argCount > 1) {
		char *seekMode = getAsString(args[1]);
		if (strcmp(seekMode, "start") == 0) mode = SEEK_SET;
		else if (strcmp(seekMode, "current") == 0) mode = SEEK_CUR;
		else if (strcmp(seekMode, "end") == 0) mode = SEEK_END;
		free(seekMode);
	}
	else mode = SEEK_SET;
	if (mode == 10) return throwTypeError("Invalid seek mode");

	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	int success = fseek(file, jerry_value_as_int32(args[0]), mode);
	if (success != 0) return throwError("File seek failed.");
	return undefined;
}

FUNCTION(File_close) {
	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	if (fclose(file) != 0) return throwError("File close failed.");
	return undefined;
}

FUNCTION(FileStatic_open) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	char defaultMode[2] = "r";
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
	u32 size = ftell(source);
	rewind(source);
	u8 *data = (u8 *) malloc(size);
	u32 bytesRead = fread(data, 1, size, source);
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
	return undefined;
}

FUNCTION(FileStatic_rename) {
	REQUIRE(2);
	char *sourcePath = getAsString(args[0]);
	char *destPath = getAsString(args[1]);
	int status = rename(sourcePath, destPath);
	free(sourcePath);
	free(destPath);
	if (status != 0) return throwError("Failed to rename file.");
	return undefined;
}

FUNCTION(FileStatic_remove) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	if (remove(path) != 0) return throwError("Failed to delete file.");
	return undefined;
}

FUNCTION(FileStatic_read) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "r");
	free(path);
	if (file == NULL) return throwError("Unable to open file.");

	fseek(file, 0, SEEK_END);
	u32 size = ftell(file);
	rewind(file);

	jerry_value_t arrayBuffer = jerry_create_arraybuffer(size);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer);
	fread(buf, 1, size, file);
	if (ferror(file)) {
		jerry_release_value(arrayBuffer);
		fclose(file);
		return throwError("File read failed.");
	}
	fclose(file);
	jerry_value_t u8Array = jerry_create_typedarray_for_arraybuffer_sz(JERRY_TYPEDARRAY_UINT8, arrayBuffer, 0, size);
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
	u32 size = ftell(file);
	rewind(file);

	char *buf = (char *) malloc(size);
	fread(buf, 1, size, file);
	if (ferror(file)) {
		free(buf);
		fclose(file);
		return throwError("File read failed.");
	}
	fclose(file);
	jerry_value_t str = jerry_create_string_sz_from_utf8((jerry_char_t *) buf, size);
	free(buf);
	return str;
}

FUNCTION(FileStatic_write) {
	REQUIRE(2);
	EXPECT(jerry_value_is_object(args[1]) && jerry_value_is_typedarray(args[1]) && jerry_get_typedarray_type(args[1]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	char *path = getAsString(args[0]);
	FILE *file = fopen(path, "w");
	free(path);
	if (file == NULL) return throwError("Unable to open file.");
	
	u32 offset, size;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[1], &offset, &size);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer) + offset;
	jerry_release_value(arrayBuffer);

	u32 bytesWritten = fwrite(buf, 1, size, file);
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
	
	u32 size;
	char *text = getAsString(args[1], &size);
	u32 bytesWritten = fwrite(text, 1, size, file);
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
	bool recursive = argCount > 1 ? jerry_value_to_boolean(args[1]) : false;
	char *path = getAsString(args[0]);
	int status = -1;
	if (recursive) {
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
	return undefined;
}

FUNCTION(FileStatic_readDir) {
	REQUIRE_FIRST();
	char *path = getAsString(args[0]);
	DIR *dir = opendir(path);
	free(path);
	if (dir == NULL) return throwError("Unable to open directory.");

	jerry_value_t arr = jerry_create_array(0);
	jerry_value_t push = getProperty(arr, "push");
	dirent *entry = readdir(dir);
	while (entry != NULL) {
		jerry_value_t dirEnt = jerry_create_object();
		setProperty(dirEnt, "isDirectory", jerry_create_boolean(entry->d_type == DT_DIR));
		setProperty(dirEnt, "isFile", jerry_create_boolean(entry->d_type == DT_REG));
		jerry_value_t name = createString(entry->d_name);
		setProperty(dirEnt, "name", name);
		jerry_release_value(name);
		jerry_call_function(push, arr, &dirEnt, 1);
		jerry_release_value(dirEnt);
		entry = readdir(dir);
	}
	closedir(dir);
	jerry_release_value(push);
	return arr;
}

FUNCTION(storage_length) {
	jerry_value_t propNames = jerry_get_object_keys(ref_storage);
	u32 length = jerry_get_array_length(propNames);
	jerry_release_value(propNames);
	return jerry_create_number(length);
}

FUNCTION(storage_key) {
	REQUIRE_FIRST();
	jerry_value_t key = null;
	jerry_value_t propNames = jerry_get_object_keys(ref_storage);
	jerry_value_t nVal = jerry_value_to_number(args[0]);
	u32 n = jerry_value_as_uint32(nVal);
	if (n < jerry_get_array_length(propNames)) {
		jerry_value_t prop = jerry_get_property_by_index(propNames, n);
		if (!jerry_value_is_undefined(prop)) key = prop;
		else jerry_release_value(prop);
	}
	jerry_release_value(nVal);
	jerry_release_value(propNames);
	return key;
}

FUNCTION(storage_getItem) {
	REQUIRE_FIRST();
	jerry_value_t hasOwnVal = jerry_has_own_property(ref_storage, args[0]);
	bool hasOwn = jerry_get_boolean_value(hasOwnVal);
	jerry_release_value(hasOwnVal);
	if (hasOwn) return jerry_get_property(ref_storage, args[0]);
	else return null;
}

FUNCTION(storage_setItem) {
	REQUIRE(2);
	jerry_value_t propertyAsString = jerry_value_to_string(args[0]);
	jerry_value_t valAsString = jerry_value_to_string(args[1]);
	jerry_set_property(ref_storage, propertyAsString, valAsString);
	jerry_release_value(valAsString);
	jerry_release_value(propertyAsString);
	return undefined;
}

FUNCTION(storage_removeItem) {
	REQUIRE_FIRST();
	jerry_value_t propertyAsString = jerry_value_to_string(args[0]);
	jerry_value_t hasOwn = jerry_has_own_property(ref_storage, propertyAsString);
	if (jerry_get_boolean_value(hasOwn)) {
		jerry_delete_property(ref_storage, propertyAsString);
	}
	jerry_release_value(hasOwn);
	jerry_release_value(propertyAsString);
	return undefined;
}

FUNCTION(storage_clear) {
	jerry_release_value(ref_storage);
	ref_storage = jerry_create_object();
	return undefined;
}

FUNCTION(storage_save) {
	return jerry_create_boolean(storageSave());
}

FUNCTION(EventConstructor) {
	CONSTRUCTOR(Event); REQUIRE_FIRST();

	setInternalProperty(thisValue, "stopImmediatePropagation", False); // stop immediate propagation flag
	setReadonly(thisValue, "target", null);
	setReadonly(thisValue, "cancelable", False);
	setReadonly(thisValue, "defaultPrevented", False);                 // canceled flag
	jerry_value_t currentTime = jerry_create_number(time(NULL));
	setReadonly(thisValue, "timeStamp", currentTime);
	jerry_release_value(currentTime);

	jerry_value_t typeAsString = jerry_value_to_string(args[0]);	
	setReadonly(thisValue, "type", typeAsString);
	jerry_release_value(typeAsString);

	if (argCount > 1 && jerry_value_is_object(args[1])) {
		jerry_value_t keys = jerry_get_object_keys(args[1]);
		jerry_length_t length = jerry_get_array_length(keys);
		for (u32 i = 0; i < length; i++) {
			jerry_value_t key = jerry_get_property_by_index(keys, i);
			jerry_value_t value = jerry_get_property(args[1], key);
			setReadonlyJV(thisValue, key, value);
			jerry_release_value(value);
			jerry_release_value(key);
		}
		jerry_release_value(keys);
	}

	return undefined;
}

FUNCTION(Event_stopImmediatePropagation) {
	setInternalProperty(thisValue, "stopImmediatePropagation", True);
	return undefined;
}

FUNCTION(Event_preventDefault) {
	jerry_value_t cancelable = getInternalProperty(thisValue, "cancelable");
	if (jerry_value_to_boolean(cancelable)) {
		setInternalProperty(thisValue, "defaultPrevented", True);
	}
	jerry_release_value(cancelable);
	return undefined;
}

FUNCTION(EventTargetConstructor) {
	if (thisValue != ref_global) CONSTRUCTOR(EventTarget);

	jerry_value_t eventListenerList = jerry_create_object();
	setInternalProperty(thisValue, "eventListeners", eventListenerList);
	jerry_release_value(eventListenerList);

	return undefined;
}

FUNCTION(EventTarget_addEventListener) {
	REQUIRE(2);
	if (jerry_value_is_null(args[1])) return undefined;
	jerry_value_t target = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	
	jerry_value_t typeStr = createString("type");
	jerry_value_t callbackStr = createString("callback");
	jerry_value_t onceStr = createString("once");

	jerry_value_t typeVal = jerry_value_to_string(args[0]);
	jerry_value_t callbackVal = args[1];
	bool once = false;
	if (argCount > 2 && jerry_value_is_object(args[2])) {
		jerry_value_t onceVal = jerry_get_property(args[2], onceStr);
		once = jerry_value_to_boolean(onceVal);
		jerry_release_value(onceVal);
	}

	jerry_value_t eventListeners = getInternalProperty(target, "eventListeners");
	jerry_value_t listenersOfType = jerry_get_property(eventListeners, typeVal);
	if (jerry_value_is_undefined(listenersOfType)) {
		jerry_release_value(listenersOfType);
		listenersOfType = jerry_create_array(0);
		jerry_release_value(jerry_set_property(eventListeners, typeVal, listenersOfType));
	}
	u32 length = jerry_get_array_length(listenersOfType);
	bool shouldAppend = true;
	for (u32 i = 0; shouldAppend && i < length; i++) {
		jerry_value_t storedListener = jerry_get_property_by_index(listenersOfType, i);
		jerry_value_t storedCallback = jerry_get_property(storedListener, callbackStr);
		jerry_value_t callbackEquality = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, callbackVal, storedCallback);
		if (jerry_get_boolean_value(callbackEquality)) shouldAppend = false;
		jerry_release_value(callbackEquality);
		jerry_release_value(storedCallback);
		jerry_release_value(storedListener);
	}

	if (shouldAppend) {
		jerry_value_t listener = jerry_create_object();

		jerry_release_value(jerry_set_property(listener, typeStr, typeVal));
		jerry_release_value(jerry_set_property(listener, callbackStr, callbackVal));
		jerry_value_t onceVal = jerry_create_boolean(once);
		jerry_release_value(jerry_set_property(listener, onceStr, onceVal));
		jerry_release_value(onceVal);
		setProperty(listener, "removed", False);

		jerry_value_t pushFunc = getProperty(listenersOfType, "push");
		jerry_release_value(jerry_call_function(pushFunc, listenersOfType, &listener, 1));
		jerry_release_value(pushFunc);

		jerry_value_t isGlobal = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, target, ref_global);
		if (jerry_get_boolean_value(isGlobal)) {
			char *type = getString(typeVal);
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
		jerry_release_value(isGlobal);

		jerry_release_value(listener);
	}

	jerry_release_value(listenersOfType);
	jerry_release_value(eventListeners);
	jerry_release_value(typeVal);
	jerry_release_value(onceStr);
	jerry_release_value(callbackStr);
	jerry_release_value(typeStr);

	return undefined;
}

FUNCTION(EventTarget_removeEventListener) {
	REQUIRE(2);
	jerry_value_t target = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	
	jerry_value_t typeStr = createString("type");
	jerry_value_t callbackStr = createString("callback");

	jerry_value_t typeVal = jerry_value_to_string(args[0]);
	jerry_value_t callbackVal = args[1];

	jerry_value_t eventListeners = getInternalProperty(target, "eventListeners");
	jerry_value_t listenersOfType = jerry_get_property(eventListeners, typeVal);
	if (jerry_value_is_array(listenersOfType)) {
		u32 length = jerry_get_array_length(listenersOfType);
		bool removed = false;
		for (u32 i = 0; !removed && i < length; i++) {
			jerry_value_t storedListener = jerry_get_property_by_index(listenersOfType, i);
			jerry_value_t storedCallback = jerry_get_property(storedListener, callbackStr);
			jerry_value_t callbackEquality = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, callbackVal, storedCallback);
			if (jerry_get_boolean_value(callbackEquality)) {
				jerry_value_t spliceFunc = getProperty(listenersOfType, "splice");
				jerry_value_t spliceArgs[2] = {jerry_create_number(i), jerry_create_number(1)};
				jerry_release_value(jerry_call_function(spliceFunc, listenersOfType, spliceArgs, 2));
				jerry_release_value(spliceArgs[1]);
				jerry_release_value(spliceArgs[0]);
				jerry_release_value(spliceFunc);
				setProperty(storedListener, "removed", True);
				removed = true;
				if (jerry_get_array_length(listenersOfType) == 0) {
					jerry_value_t isGlobal = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, target, ref_global);
					if (jerry_get_boolean_value(isGlobal)) {
						char *type = getString(typeVal);
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
					jerry_release_value(isGlobal);
				}
			}
			jerry_release_value(callbackEquality);
			jerry_release_value(storedCallback);
			jerry_release_value(storedListener);
		}
	}
	jerry_release_value(listenersOfType);
	jerry_release_value(eventListeners);
	jerry_release_value(typeVal);
	jerry_release_value(callbackStr);
	jerry_release_value(typeStr);

	return undefined;
}

FUNCTION(EventTarget_dispatchEvent) {
	REQUIRE_FIRST();
	jerry_value_t isInstanceVal = jerry_binary_operation(JERRY_BIN_OP_INSTANCEOF, args[0], ref_Event);
	bool isInstance = jerry_get_boolean_value(isInstanceVal);
	jerry_release_value(isInstanceVal);
	EXPECT(isInstance, Event);
	jerry_value_t target = jerry_value_is_undefined(thisValue) ? ref_global : thisValue;
	bool canceled = dispatchEvent(target, args[0], true);
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
		char *str = getString(args[0]);
		if (strcmp(str, "top") == 0) {
			lcdMainOnTop();
			set = true;
		}
		else if (strcmp(str, "bottom") == 0) {
			lcdMainOnBottom();
			set = true;
		}
		free(str);
		if (set) return undefined;
	}
	return throwTypeError("Invalid screen value");
}

FUNCTION(DS_sleep) {
	jerry_value_t eventArgs[2] = {createString("sleep"), jerry_create_object()};
	setProperty(eventArgs[1], "cancelable", True);
	jerry_value_t event = jerry_construct_object(ref_Event, eventArgs, 2);
	bool canceled = dispatchEvent(ref_global, event, true);
	jerry_release_value(event);
	jerry_release_value(eventArgs[0]);
	jerry_release_value(eventArgs[1]);
	if (!canceled) {
		systemSleep();
		swiWaitForVBlank();
		swiWaitForVBlank(); // I know this is jank but it's the easiest solution to stop 'wake' from dispatching before the system sleeps
		eventArgs[0] = createString("wake");
		eventArgs[1] = jerry_create_object();
		event = jerry_construct_object(ref_Event, eventArgs, 2);
		dispatchEvent(ref_global, event, true);
		jerry_release_value(event);
		jerry_release_value(eventArgs[0]);
		jerry_release_value(eventArgs[1]);
	}
	return undefined;
}

FUNCTION(DS_touchGetPosition) {
	if ((keysHeld() & KEY_TOUCH) == 0) {
		jerry_value_t position = jerry_create_object();
		jerry_value_t NaN = jerry_create_number_nan();
		setProperty(position, "x", NaN);
		setProperty(position, "y", NaN);
		jerry_release_value(NaN);
		return position;
	}
	touchPosition pos; touchRead(&pos);
	jerry_value_t position = jerry_create_object();
	jerry_value_t x = jerry_create_number(pos.px);
	jerry_value_t y = jerry_create_number(pos.py);
	setProperty(position, "x", x);
	setProperty(position, "y", y);
	jerry_release_value(x);
	jerry_release_value(y);
	return position;
}

FUNCTION(BETA_gfxInit) {
	videoSetMode(MODE_3_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	return undefined;
}

FUNCTION(BETA_gfxPixel) {
	REQUIRE(3);
	u8 x = jerry_value_as_uint32(args[0]);
	u8 y = jerry_value_as_uint32(args[1]);
	u16 color = jerry_value_as_uint32(args[2]);
	bgGetGfxPtr(3)[x + y*256] = color;
	return undefined;
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

	if (width == 0 || height == 0) return undefined;

	for (u8 i = 0; i < height; i++) {
		dmaFillHalfWords(color, gfx + x + ((y + i) * 256), width * 2);
	}
	return undefined;
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
	setMethod(keyboard, "hide", LAMBDA((keyboardHide(), undefined)));
	setMethod(keyboard, "show", LAMBDA((keyboardShow(), undefined)));
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
	jsClass File = createClass(ref_global, "File", IllegalConstructor);
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
	setMethod(File.prototype, "read", File_read);
	setMethod(File.prototype, "write", File_write);
	setMethod(File.prototype, "seek", File_seek);
	setMethod(File.prototype, "close", File_close);
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

	jsClass EventTarget = createClass(ref_global, "EventTarget", EventTargetConstructor);
	setMethod(EventTarget.prototype, "addEventListener", EventTarget_addEventListener);
	setMethod(EventTarget.prototype, "removeEventListener", EventTarget_removeEventListener);
	setMethod(EventTarget.prototype, "dispatchEvent", EventTarget_dispatchEvent);
	// turn global into an EventTarget
	jerry_release_value(jerry_set_prototype(ref_global, EventTarget.prototype));
	EventTargetConstructor(EventTarget.constructor, ref_global, NULL, 0);
	releaseClass(EventTarget);

	jsClass Event = createClass(ref_global, "Event", EventConstructor);
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
	setMethod(DS, "shutdown", LAMBDA((systemShutDown(), undefined)));
	setMethod(DS, "sleep", DS_sleep);
	setMethod(DS, "swapScreens", LAMBDA((lcdSwap(), undefined)));

	jerry_value_t profile = createObject(DS, "profile");
	setReadonlyNumber(profile, "alarmHour", PersonalData->alarmHour);
	setReadonlyNumber(profile, "alarmMinute", PersonalData->alarmMinute);
	setReadonlyNumber(profile, "birthDay", PersonalData->birthDay);
	setReadonlyNumber(profile, "birthMonth", PersonalData->birthMonth);
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
	setReadonlyStringU16(profile, "name", (u16 *) PersonalData->name, PersonalData->nameLen);
	setReadonlyStringU16(profile, "message", (u16 *) PersonalData->message, PersonalData->messageLen);
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