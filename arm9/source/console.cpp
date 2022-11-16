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

u8 tileWidth = 0;
u8 tileHeight = 0;
u16 tileSize = 0;
bool replace = false;

u8 *tileData = NULL;
u8 *widthData = NULL;
u16 charMap[CHAR_RANGE] = {0};

u16 gfxBuffer[SCREEN_WIDTH * SCREEN_HEIGHT] = {0};

u16 lineWidth = 0;
u16 lineTop = 0;
bool paused = false;

#define getU16(src, offset) *((u16 *) (src + offset))
#define getU32(src, offset) *((u32 *) (src + offset))

void loadFont(u8 *data) {
	u8 *finf = data + getU16(data, 0x0C);
	u8 *cglp = data + getU32(finf, 0x10) - 8;
	u8 *cwdh = data + getU32(finf, 0x14) - 8;

	u8 encoding = finf[0x0F];
	u8 bitdepth = cglp[0x0E];
	if (encoding != 1 || bitdepth != 2) return;

	tileWidth = cglp[0x08];
	tileHeight = cglp[0x09];
	tileSize = getU16(cglp, 0x0A);

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

void newLine() {
	lineWidth = 0;
	lineTop += tileHeight;
	if (lineTop >= SCREEN_HEIGHT) {
		int rowCount = lineTop - tileHeight;
		u32 *dst = (u32 *) gfxBuffer;
		u32 *src = (u32 *) (gfxBuffer + (tileHeight * SCREEN_WIDTH));
		while (rowCount--) { // moves an entire row of bg data at a time.
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
			*dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
		}
		lineTop = SCREEN_HEIGHT - tileHeight;
		memset(gfxBuffer + (lineTop * SCREEN_WIDTH), 0, SCREEN_WIDTH * tileHeight * sizeof(u16));
	}
}

int printChar(u16 codepoint) {
	if (!tileData || !widthData) return 0;
	if (codepoint == '\n') {
		newLine();
		return 2;
	}

	u16 tileNum = charMap[codepoint];
	if (tileNum == NO_TILE) {
		if (replace) tileNum = charMap[REPLACEMENT_CHAR];
		else return 0;
	}

	u8 *tile = tileData + tileNum * tileSize;
	u8 *widths = widthData + tileNum * 3;
	bool newLined = false;
	if (lineWidth + widths[2] > SCREEN_WIDTH) {
		newLine();
		newLined = true;
	}

	u8 byte = 0;
	u8 bitsLeft = 0;
	for (u8 y = 0; y < tileHeight; y++) {
		u16 bufY = lineTop + y;
		if (bufY >= SCREEN_HEIGHT) break;

		for (u8 x = 0; x < widths[0]; x++) {
			u8 bufX = lineWidth + x;
			if (bufX < SCREEN_WIDTH) {
				gfxBuffer[bufX + bufY * SCREEN_WIDTH] = colors[0];
			}
		}

		for (u8 x = 0; x < tileWidth; x++) {
			if (bitsLeft == 0) {
				byte = *(tile++);
				bitsLeft = 8;
			}
			if (x < widths[1]) {
				u16 bufX = lineWidth + widths[0] + x;
				if (bufX < SCREEN_WIDTH) {
					gfxBuffer[bufX + bufY * SCREEN_WIDTH] = colors[byte >> 6];
				}
			}
			byte = (byte & 0b00111111) << 2;
			bitsLeft -= 2;
		}
	}

	lineWidth += widths[2];
	return true;
	return newLined ? 2 : 1;
}

ssize_t writeIn(struct _reent *_r, void *_fd, const char *message, size_t len) {
	if (!message) return 0;
	int amt = len;
	int update = 0;
	while (amt-- > 0) {
		int status = printChar(*(message++));
		if (status > update) update = status;
	}
	if (!paused) {
		if (update == 2) dmaCopy(gfxBuffer, bgGetGfxPtr(3), sizeof(gfxBuffer));
		else if (update == 1) dmaCopy(
			gfxBuffer + (lineTop * SCREEN_WIDTH),
			bgGetGfxPtr(3) + (lineTop * SCREEN_WIDTH),
			SCREEN_WIDTH * tileHeight * sizeof(u16)
		);
	}
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

// Pauses the DMA copies after every console write
void consolePause() {
	paused = true;
}
// Resumes the DMA copies after every console write, and performs one immediately
void consoleResume() {
	paused = false;
	dmaCopy(gfxBuffer, bgGetGfxPtr(3), sizeof(gfxBuffer));
}
void consoleClear() {
	memset(gfxBuffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(u16));
	if (!paused) dmaCopy(gfxBuffer, bgGetGfxPtr(3), sizeof(gfxBuffer));
	lineWidth = lineTop = 0;
}