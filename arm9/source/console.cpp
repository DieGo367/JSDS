#include "console.hpp"

#include <nds/arm9/background.h>
#include <nds/arm9/cache.h>
#include <nds/arm9/video.h>
extern "C" {
#include <nds/debug.h>
}
#include <stdlib.h>
#include <string.h>
#include <sys/iosupport.h>

#include "font_nftr.h"


const u32 CHAR_RANGE = 0x10000;
const u16 NO_TILE = 0xFFFF;
const u16 REPLACEMENT_CHAR = 0xFFFD;
const u8 LINE_CT = 16;
const u8 CHAR_PER_LINE = 32;

u8 tileWidth;
u8 tileHeight;
u16 tileSize;
u8 bitdepth;
bool replace = false;

u8 *tileData;
u8 *widthData;
u16 charMap[CHAR_RANGE];

u16 gfxBuffer[SCREEN_WIDTH * SCREEN_HEIGHT] = {0};

u16 textBuffer[LINE_CT][CHAR_PER_LINE];
u8 lineWidths[LINE_CT];
u8 currentLine = 0;
u8 currentChar = 0;
u16 lineTop = 0;

#define getU16(src, offset) *((u16 *) (src + offset))
#define getU32(src, offset) *((u32 *) (src + offset))

void loadFont(u8 *data) {
	u8 *finf = data + getU16(data, 0x0C);
	u8 *cglp = data + getU32(finf, 0x10) - 8;
	u8 *cwdh = data + getU32(finf, 0x14) - 8;

	tileWidth = cglp[0x08];
	tileHeight = cglp[0x09];
	tileSize = getU16(cglp, 0x0A);
	bitdepth = cglp[0x0E];
	tileData = cglp + 0x10;
	widthData = cwdh + 0x10;
	for (u32 i = 0; i < CHAR_RANGE; i++) charMap[i] = NO_TILE;
	u32 nextCMAP = getU32(finf, 0x18);
	while (nextCMAP) {
		u8 *cmap = data + nextCMAP - 8;
		u16 firstCodepoint = getU16(cmap, 0x08);
		u16 lastCodepoint = getU16(cmap, 0x0A);
		switch (getU32(cmap, 0x0C)) {
			case 0: {
				u16 tileNum = getU16(cmap, 0x14);
				for (u16 codepoint = firstCodepoint; codepoint <= lastCodepoint; codepoint++) {
					charMap[codepoint] = tileNum++;
				}
			} break;
			case 1: {
				u16 *tileNumPtr = (u16 *)(cmap + 0x14);
				for (u16 codepoint = firstCodepoint; codepoint <= lastCodepoint; codepoint++) {
					charMap[codepoint] = *(tileNumPtr++);
				}
			} break;
			case 2: {
				u16 pairs = getU16(cmap, 0x14);
				u8 *pairData = cmap + 0x16;
				while (pairs--) {
					charMap[getU16(pairData, 0)] = getU16(pairData, 0x2);
					pairData += 4;
				}
			} break;
		}
		nextCMAP = getU32(cmap, 0x10);
	}
	replace = charMap[REPLACEMENT_CHAR] != NO_TILE;
}

u16 colors[4] = {0};

u16 colorBlend(u16 baseColor, u16 targetColor, u8 strength) {
	if (!((baseColor | targetColor) & BIT(15))) return 0x0000;
	u8 neg = 31 - strength;
	u16 blue = ((baseColor >> 10) & 0b11111) * neg / 31 + ((targetColor >> 10) & 0b11111) * strength / 31;
	u16 green = ((baseColor >> 5) & 0b11111) * neg / 31 + ((targetColor >> 5) & 0b11111) * strength / 31;
	u16 red = (baseColor & 0b11111) * neg / 31 + (targetColor & 0b11111) * strength / 31;
	return BIT(15) | blue << 10 | green << 5 | red;
}

u16 consoleSetColor(u16 color) {
	u16 prev = colors[3];
	colors[3] = color;
	colors[2] = colorBlend(colors[0], color, 24);
	colors[1] = colorBlend(colors[0], color, 15);
	return prev;
}
u16 consoleGetColor() { return colors[3]; }

u16 consoleSetBackground(u16 color) {
	u16 prev = colors[0];
	colors[0] = color;
	colors[1] = colorBlend(color, colors[3], 15);
	colors[2] = colorBlend(color, colors[3], 24);
	return prev;
}
u16 consoleGetBackground() { return colors[0]; }


bool printChar(u16 codepoint) {
	if (codepoint == '\n') {
		u8 clearWidth = lineWidths[currentLine];
		currentLine = (currentLine + 1) % LINE_CT;
		memset(textBuffer[currentLine], 0, CHAR_PER_LINE * sizeof(u16));
		lineWidths[currentLine] = 0;
		currentChar = 0;
		if (lineTop + tileHeight >= SCREEN_HEIGHT) {
			u8 shift = (lineTop + tileHeight) - (SCREEN_HEIGHT - tileHeight);
			memmove(gfxBuffer, gfxBuffer + (shift * SCREEN_WIDTH), SCREEN_WIDTH * (SCREEN_HEIGHT - shift) * sizeof(u16));
			for (u8 y = 0; y < tileHeight; y++) {
				memset(gfxBuffer + ((lineTop + y) * SCREEN_WIDTH), 0, clearWidth * sizeof(u16));
			}
			lineTop = SCREEN_HEIGHT - tileHeight;
		}
		else lineTop += tileHeight;
		return true;
	}


	u16 tileNum = charMap[codepoint];
	if (tileNum == NO_TILE) {
		if (replace) tileNum = charMap[REPLACEMENT_CHAR];
		else return false;
	}

	u8 *tile = tileData + tileNum * tileSize;
	u8 *widths = widthData + tileNum * 3;
	u8 byte = 0;
	u8 bitsLeft = 0;
	for (u8 y = 0; y < tileHeight; y++) {
		u16 bufY = lineTop + y;
		if (bufY >= SCREEN_HEIGHT) break;

		for (u8 x = 0; x < widths[0]; x++) {
			u8 bufX = lineWidths[currentLine] + x;
			if (bufX < SCREEN_WIDTH) {
				gfxBuffer[bufX + bufY * SCREEN_WIDTH] = 0;
			}
		}

		for (u8 x = 0; x < tileWidth; x++) {
			if (bitsLeft == 0) {
				byte = *(tile++);
				bitsLeft = 8;
			}
			if (x < widths[1]) {
				u16 bufX = lineWidths[currentLine] + widths[0] + x;
				if (bufX < SCREEN_WIDTH) {
					gfxBuffer[bufX + bufY * SCREEN_WIDTH] = colors[byte >> 6];
				}
			}
			byte = (byte & 0b00111111) << 2;
			bitsLeft -= 2;
		}
	}

	textBuffer[currentLine][currentChar++] = codepoint;
	lineWidths[currentLine] += widths[2];
	return true;
}

ssize_t writeIn(struct _reent *_r, void *_fd, const char *message, size_t len) {
	if (!message) return 0;
	int amt = len;
	bool update = false;
	while (amt-- > 0) {
		if (printChar(*(message++))) update = true;
	}
	if (update) dmaCopy(gfxBuffer, bgGetGfxPtr(3), sizeof(gfxBuffer));
	return len;
}

const devoptab_t opTable = {
	"console",
	0,
	NULL,
	NULL,
	writeIn,
	NULL,
	NULL,
	NULL
};

void consoleInit() {
	devoptab_list[STD_OUT] = &opTable;
	devoptab_list[STD_ERR] = &opTable;
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	videoSetMode(MODE_3_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	loadFont((u8 *) font_nftr);
	consoleSetColor(0xFFFF);
}