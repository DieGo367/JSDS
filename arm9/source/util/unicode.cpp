#include "util/unicode.hpp"
#include <stdlib.h>

char *UTF16toUTF8(const char16_t *utf16, u32 utf16Length, u32 *utf8Length) {
	char *utf8 = (char *) malloc(utf16Length * 3 + 1);
	char *ptr = utf8;
	for (u32 i = 0; i < utf16Length; i++) {
		char16_t codepoint = utf16[i];
		if (codepoint < 0x80) *(ptr++) = codepoint;
		else if (codepoint < 0x800) {
			*(ptr++) = 0b11000000 | (codepoint >> 6 & 0b00011111);
			*(ptr++) = BIT(7) | (codepoint & 0b00111111);
		}
		else if (codepoint >= 0xD800 && codepoint < 0xDC00 && i < utf16Length && utf16[i + 1] >= 0xDC00 && utf16[i + 1] < 0xF000) {
			char16_t surrogate = utf16[++i];
			*(ptr++) = 0xF0 | (codepoint >> 7 & 0b111);
			*(ptr++) = BIT(7) | (codepoint >> 1 & 0b00111111);
			*(ptr++) = BIT(7) | (codepoint & 1) << 5 | (surrogate >> 5 & 0b00011111); // different mask than the rest
			*(ptr++) = BIT(7) | (surrogate & 0b00111111);
		}
		else {
			*(ptr++) = 0b11100000 | (codepoint >> 12 & 0xF);
			*(ptr++) = BIT(7) | (codepoint >> 6 & 0b00111111);
			*(ptr++) = BIT(7) | (codepoint & 0b00111111);
		}
	}
	*ptr = 0;
	if (utf8Length != NULL) *utf8Length = ptr - utf8;
	return utf8;
}

char16_t *UTF8toUTF16(const char *utf8, u32 utf8Length, u32 *utf16Length) {
	char16_t *utf16 = (char16_t *) malloc(utf8Length * 2 + 1);
	char16_t *ptr = utf16;
	for (u32 i = 0; i < utf8Length; i++) {
		u8 byte = utf8[i];
		if (byte < 0x80) *(ptr++) = byte;
		else if (byte < 0xE0) {
			if (i + 1 >= utf8Length) *(ptr++) = 0xFFFD;
			else *(ptr++) = (byte & 0b11111) << 6 | (utf8[++i] & 0b111111);
		}
		else if (byte < 0xF0) {
			if (i + 2 >= utf8Length) *(ptr++) = 0xFFFD;
			else {
				u8 byte2 = utf8[++i], byte3 = utf8[++i];
				*(ptr++) = (byte & 0xF) << 12 | (byte2 & 0b111111) << 6 | (byte3 & 0b111111);
			}
		}
		else {
			if (i + 3 >= utf8Length) *(ptr++) = 0xFFFD;
			else {
				u8 byte2 = utf8[++i], byte3 = utf8[++i], byte4 = utf8[++i];
				char32_t codepoint = (byte & 0b111) << 18 | (byte2 & 0b111111) << 12 | (byte3 & 0b111111) << 6 | (byte4 & 0b111111);
				codepoint -= 0x10000;
				*(ptr++) = 0xD800 | codepoint >> 10;
				*(ptr++) = 0xDC00 | (codepoint & 0x3FF);
			}
		}
	}
	*ptr = 0;
	if (utf16Length != NULL) *utf16Length = ptr - utf16;
	return utf16;
}