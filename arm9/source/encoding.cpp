#include <stdlib.h>

#include "util/helpers.hpp"
#include "util/tonccpy.h"
#include "util/unicode.hpp"

const char b64Map[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";



FUNCTION(Text_encode) {
	REQUIRE(1);
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
	REQUIRE(1); EXPECT(jerry_get_typedarray_type(args[0]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	jerry_length_t byteOffset = 0, dataLen = 0;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &byteOffset, &dataLen);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
	jerry_release_value(arrayBuffer);
	if (!jerry_is_valid_utf8_string(data, dataLen)) return TypeError("Invalid UTF-8");
	return StringSized(data, dataLen);
}

FUNCTION(Text_encodeUTF16) {
	REQUIRE(1);
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) {
		EXPECT(jerry_get_typedarray_type(args[1]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	}
	jerry_size_t utf8Size;
	char *utf8 = toRawString(args[0], &utf8Size);
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
	REQUIRE(1); EXPECT(jerry_get_typedarray_type(args[0]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	jerry_length_t byteOffset = 0, dataLen = 0;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &byteOffset, &dataLen);
	u8 *data = jerry_get_arraybuffer_pointer(arrayBuffer) + byteOffset;
	jerry_release_value(arrayBuffer);
	return StringUTF16((char16_t *) data, dataLen / 2);
}

FUNCTION(Base64_encode) {
	REQUIRE(1); EXPECT(jerry_get_typedarray_type(args[0]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
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
	REQUIRE(1); EXPECT(jerry_value_is_string(args[0]), string);
	if (argCount > 1 && !jerry_value_is_undefined(args[1])) {
		EXPECT(jerry_get_typedarray_type(args[1]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	}
	jerry_size_t asciiSize;
	char *ascii = rawString(args[0], &asciiSize);
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

void exposeEncodingAPI(jerry_value_t global) {
	jerry_value_t Text = createObject(global, "Text");
	setMethod(Text, "encode", Text_encode);
	setMethod(Text, "decode", Text_decode);
	setMethod(Text, "encodeUTF16", Text_encodeUTF16);
	setMethod(Text, "decodeUTF16", Text_decodeUTF16);
	jerry_release_value(Text);

	jerry_value_t Base64 = createObject(global, "Base64");
	setMethod(Base64, "encode", Base64_encode);
	setMethod(Base64, "decode", Base64_decode);
	jerry_release_value(Base64);
}