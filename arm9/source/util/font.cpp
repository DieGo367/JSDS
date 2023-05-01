#include "util/font.hpp"

#include <stdlib.h>
#include <string.h>

#include "util/tonccpy.h"
#include "util/unicode.hpp"

#define getU16(src, offset) *((u16 *) (src + offset))
#define getU32(src, offset) *((u32 *) (src + offset))



NitroFont fontLoad(const u8 *data) {
	NitroFont font;

	const u8 *finf = data + getU16(data, 0x0C);
	const u8 *cglp = data + getU32(finf, 0x10) - 8;
	const u8 *cwdh = data + getU32(finf, 0x14) - 8;

	font.encoding = finf[0x0F];
	font.bitdepth = cglp[0x0E];

	font.tileWidth = cglp[0x08];
	font.tileHeight = cglp[0x09];
	font.tileSize = getU16(cglp, 0x0A);

	u32 tileDataSize = getU32(cglp, 0x04) - 0x10;
	font.tileData = (u8 *) malloc(tileDataSize);
	memcpy(font.tileData, cglp + 0x10, tileDataSize);

	u32 widthDataSize = getU32(cwdh, 0x04) - 0x10;
	font.widthData = (u8 *) malloc(widthDataSize);
	memcpy(font.widthData, cwdh + 0x10, widthDataSize);
	
	font.charMap = (u16 *) malloc(CHAR_RANGE * sizeof(u16));
	for (u32 i = 0; i < CHAR_RANGE; i++) font.charMap[i] = NO_TILE;
	u32 nextCMAP = getU32(finf, 0x18);
	while (nextCMAP) {
		const u8 *cmap = data + nextCMAP - 8;
		char16_t firstCodepoint = getU16(cmap, 0x08);
		char16_t lastCodepoint = getU16(cmap, 0x0A);
		switch (getU32(cmap, 0x0C)) {
			case 0: {
				u16 tileNum = getU16(cmap, 0x14);
				for (char16_t codepoint = firstCodepoint; codepoint <= lastCodepoint; codepoint++) {
					font.charMap[codepoint] = tileNum++;
				}
			} break;
			case 1: {
				u16 *tileNumPtr = (u16 *)(cmap + 0x14);
				for (char16_t codepoint = firstCodepoint; codepoint <= lastCodepoint; codepoint++) {
					font.charMap[codepoint] = *(tileNumPtr++);
				}
			} break;
			case 2: {
				u16 pairs = getU16(cmap, 0x14);
				const u8 *pairData = cmap + 0x16;
				while (pairs--) {
					font.charMap[getU16(pairData, 0)] = getU16(pairData, 0x2);
					pairData += 4;
				}
			} break;
		}
		nextCMAP = getU32(cmap, 0x10);
	}
	font.replace = font.charMap[REPLACEMENT_CHAR] != NO_TILE;

	return font;
}

u8 fontGetCodePointWidth(NitroFont font, char16_t codepoint) {
	if (!font.charMap || !font.widthData || font.encoding != 1) return 0;

	u16 tileNum = font.charMap[codepoint];
	if (tileNum == NO_TILE) tileNum = font.charMap[REPLACEMENT_CHAR];
	if (tileNum == NO_TILE) return 0;

	u8 *widths = font.widthData + tileNum * 3;
	return widths[2];
}

// this assumes character is in the buffer's bounds
void fontPrintCodePoint(NitroFont font, const u16 *palette, char16_t codepoint, u16 *buffer, u32 bufferWidth, u32 x, u32 y) {
	if (!font.tileData || !font.widthData || !font.charMap || font.encoding != 1 || font.bitdepth != 2) return;

	u16 tileNum = font.charMap[codepoint];
	if (tileNum == NO_TILE) tileNum = font.charMap[REPLACEMENT_CHAR];
	if (tileNum == NO_TILE) return;

	u8 *tile = font.tileData + tileNum * font.tileSize;
	u8 *widths = font.widthData + tileNum * 3;

	buffer += x + y * bufferWidth;
	for (u8 ty = 0; ty < font.tileHeight; ty++, buffer += bufferWidth) {
		u16 *buf = buffer;

		if (palette[0]) toncset16(buf, palette[0], widths[0]);
		buf += widths[0];

		for (u16 tx = 0, pixel = ty * font.tileWidth; tx < widths[1]; tx++, pixel++, buf++) {
			u16 color = palette[*(tile + pixel / 4) >> ((3 - pixel % 4) * 2) & 0b11];
			if (color) *buf = color;
		}
	}
}

// this assumes string is in the buffer's bounds.
void fontPrintUnicode(NitroFont font, const u16 *palette, const char16_t *codepoints, u16 *buffer, u32 bufferWidth, u32 x, u32 y, u32 maxWidth, bool scroll) {
	if (!codepoints[0] || !font.tileData || !font.widthData || !font.charMap || font.encoding != 1 || font.bitdepth != 2) return;
	
	buffer += x + y * bufferWidth;
	u32 totalX = 0;

	const char16_t *ptr = codepoints;
	bool ltr = true;
	if (scroll) {
		u32 totalWidth = 0;
		while (*ptr) {
			u16 tileNum = font.charMap[*(ptr++)];
			if (tileNum == NO_TILE) tileNum = font.charMap[REPLACEMENT_CHAR];
			if (tileNum == NO_TILE) continue;
			totalWidth += font.widthData[tileNum * 3 + 2];
		}
		if (totalWidth > maxWidth) {
			// will need to print from right to left instead
			ltr = false;
			ptr--;
		}
		else ptr = codepoints; // otherwise, fall back to normal left to right print behavior
	}

	while (true) {
		u16 tileNum = font.charMap[*ptr];
		if (tileNum == NO_TILE) tileNum = font.charMap[REPLACEMENT_CHAR];
		if (tileNum == NO_TILE) continue;

		u8 *tile = font.tileData + tileNum * font.tileSize;
		u8 *widths = font.widthData + tileNum * 3;

		u16 *buff = buffer + (ltr ? totalX : maxWidth - totalX - widths[2]);
		u32 remainingSpace = maxWidth - totalX;
		if (widths[2] > remainingSpace) {
			// print while respecting width bounds
			for (u8 ty = 0; ty < font.tileHeight; ty++, buff += bufferWidth) {
				u16 *buf = buff;
				u32 leftSpacePrint = widths[0];
				if (ltr && leftSpacePrint > remainingSpace) leftSpacePrint = remainingSpace;
				if (!ltr && widths[2] - leftSpacePrint > remainingSpace) leftSpacePrint = remainingSpace - (widths[2] - leftSpacePrint);
				if (palette[0]) toncset16(buf + (ltr ? 0 : widths[2] - remainingSpace), palette[0], leftSpacePrint);
				buf += widths[0];

				for (u16 tx = 0, pixel = ty * font.tileWidth; tx < widths[1]; tx++, pixel++, buf++) {
					if ((ltr && widths[0] + tx < remainingSpace)
					 || (!ltr && widths[0] + tx >= widths[2] - remainingSpace)) {
						u16 color = palette[*(tile + pixel / 4) >> ((3 - pixel % 4) * 2) & 0b11];
						if (color) *buf = color;
					}
				}
			}
			return;
		}
		totalX += widths[2];

		for (u8 ty = 0; ty < font.tileHeight; ty++, buff += bufferWidth) {
			u16 *buf = buff;

			if (palette[0]) toncset16(buf, palette[0], widths[0]);
			buf += widths[0];

			for (u16 tx = 0, pixel = ty * font.tileWidth; tx < widths[1]; tx++, pixel++, buf++) {
				u16 color = palette[*(tile + pixel / 4) >> ((3 - pixel % 4) * 2) & 0b11];
				if (color) *buf = color;
			}
		}
		if (ltr) {
			if (*(++ptr) == 0) return;
		}
		else if ((ptr--) == codepoints) return;
	}
}

// this assumes string is in the buffer's bounds.
void fontPrintString(NitroFont font, const u16 *palette, const char *str, u16 *buffer, u32 bufferWidth, u32 x, u32 y, u32 maxWidth, bool scroll) {
	if (!font.tileData || !font.widthData || !font.charMap || font.encoding != 1 || font.bitdepth != 2) return;

	const char *end = str;
	while(*(end++));
	const u32 utf8Size = end - str;
	char16_t *utf16 = UTF8toUTF16(str, utf8Size);
	fontPrintUnicode(font, palette, utf16, buffer, bufferWidth, x, y, maxWidth, scroll);
	free(utf16);
}