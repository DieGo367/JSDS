#include "font.hpp"

#include <stdlib.h>
#include <string.h>

#include "font_nftr.h"

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
		u16 firstCodepoint = getU16(cmap, 0x08);
		u16 lastCodepoint = getU16(cmap, 0x0A);
		switch (getU32(cmap, 0x0C)) {
			case 0: {
				u16 tileNum = getU16(cmap, 0x14);
				for (u16 codepoint = firstCodepoint; codepoint <= lastCodepoint; codepoint++) {
					font.charMap[codepoint] = tileNum++;
				}
			} break;
			case 1: {
				u16 *tileNumPtr = (u16 *)(cmap + 0x14);
				for (u16 codepoint = firstCodepoint; codepoint <= lastCodepoint; codepoint++) {
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

u8 fontGetCharWidth(NitroFont font, u16 codepoint) {
	if (!font.tileData || !font.widthData || font.encoding != 1 || font.bitdepth != 2) return 0;

	u16 tileNum = font.charMap[codepoint];
	if (tileNum == NO_TILE) {
		if (font.replace) tileNum = font.charMap[REPLACEMENT_CHAR];
		else return 0;
	}

	u8 *widths = font.widthData + tileNum * 3;
	return widths[2];
}

void fontPrintChar(NitroFont font, u16 *palette, u16 codepoint, u16 *buffer, u32 bufferWidth, u32 x, u32 y) {
	if (!font.tileData || !font.widthData || font.encoding != 1 || font.bitdepth != 2) return;

	u16 tileNum = font.charMap[codepoint];
	if (tileNum == NO_TILE) {
		if (font.replace) tileNum = font.charMap[REPLACEMENT_CHAR];
		else return;
	}

	u8 *tile = font.tileData + tileNum * font.tileSize;
	u8 *widths = font.widthData + tileNum * 3;

	// this assumes character is in screen bounds (normal console behavior should ensure that)
	for (u8 ty = 0; ty < font.tileHeight; ty++) {
		u32 bufY = y + ty;
		u32 bufX = x;
		for (u8 tx = 0; tx < widths[0]; tx++) {
			buffer[bufX++ + bufY * bufferWidth] = palette[0];
		}
		// assuming 2bpp (or 4 pixels per byte) and that tileWidth is a multiple of 4
		for (u8 tx = 0; tx < widths[1]; tx += 4) {
			buffer[bufX++ + bufY * bufferWidth] = palette[(*tile & 0b11000000) >> 6];
			buffer[bufX++ + bufY * bufferWidth] = palette[(*tile & 0b00110000) >> 4];
			buffer[bufX++ + bufY * bufferWidth] = palette[(*tile & 0b00001100) >> 2];
			buffer[bufX++ + bufY * bufferWidth] = palette[*tile++ & 0b00000011];
		}
		tile += (font.tileWidth - widths[1]) / 4;
	}
}