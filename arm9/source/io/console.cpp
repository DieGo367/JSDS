#include "console.hpp"

#include <nds/arm9/background.h>
#include <nds/arm9/cache.h>
#include <nds/arm9/video.h>
extern "C" {
#include <nds/debug.h>
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/iosupport.h>

#include "font.hpp"



const int TAB_SIZE = 2;

static u16 gfxBuffer[SCREEN_WIDTH * SCREEN_HEIGHT] = {0};

u16 lineWidth = 0;
u16 lineTop = 0;
bool filled = false;
bool paused = false;

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

void consoleDraw() {
	int firstHeight = lineTop + defaultFont.tileHeight;
	if (filled) {
		int secondHeight = SCREEN_HEIGHT - firstHeight;
		dmaCopyWords(0, gfxBuffer, bgGetGfxPtr(3) + (SCREEN_WIDTH * secondHeight), SCREEN_WIDTH * firstHeight * sizeof(u16));
		if (secondHeight > 0) dmaCopyWords(1, gfxBuffer + (SCREEN_WIDTH * firstHeight), bgGetGfxPtr(3), SCREEN_WIDTH * secondHeight * sizeof(u16));
	}
	else dmaCopyWords(0, gfxBuffer, bgGetGfxPtr(3), SCREEN_WIDTH * firstHeight * sizeof(u16));
}

void newLine() {
	lineWidth = 0;
	lineTop += defaultFont.tileHeight;
	if (lineTop >= SCREEN_HEIGHT) {
		lineTop -= SCREEN_HEIGHT;
		filled = true;
	}
	if (filled) memset(gfxBuffer + (lineTop * SCREEN_WIDTH), 0, SCREEN_WIDTH * defaultFont.tileHeight * sizeof(u16));
}

bool writeCodepoint(u16 codepoint) {
	if (codepoint == '\n') {
		newLine();
		return true;
	}
	else if (codepoint == '\t') {
		u8 tabWidth = fontGetCharWidth(defaultFont, ' ') * TAB_SIZE;
		lineWidth = (lineWidth / tabWidth + 1) * tabWidth;
		if (lineWidth > SCREEN_WIDTH) {
			newLine();
			return true;
		}
		return false;
	}

	bool newLined = false;
	u8 width = fontGetCharWidth(defaultFont, codepoint);
	if (lineWidth + width > SCREEN_WIDTH) {
		newLine();
		newLined = true;
	}

	fontPrintChar(defaultFont, colors, codepoint, gfxBuffer, SCREEN_WIDTH, lineWidth, lineTop);

	lineWidth += width;
	return newLined;
}

ssize_t writeIn(struct _reent *_r, void *_fd, const char *message, size_t len) {
	if (!message) return 0;
	int amt = len;
	bool fullUpdate = false;
	while (amt-- > 0) {
		bool newLined = false;
		u8 ch = *message++;
		if (ch & BIT(7)) {
			if (amt >= 1 && (ch & 0xE0) == 0xC0 && (message[0] & 0xC0) == 0x80) {
				newLined = writeCodepoint((ch & 0x1F) << 6 | (message[0] & 0x3F));
				amt--; message++;
			}
			else if (amt >= 2 && (ch & 0xF0) == 0xE0 && (message[0] & 0xC0) == 0x80 && (message[1] & 0xC0) == 0x80) {
				newLined = writeCodepoint((ch & 0x0F) << 12 | (message[0] & 0x3F) << 6 | (message[1] & 0x3F));
				amt -= 2;
				message += 2;
			}
			else if (amt >= 3 && (ch & 0xF8) == 0xF0 && (message[0] & 0xC0) == 0x80 && (message[1] & 0xC0) == 0x80 && (message[2] & 0xC0) == 0x80) {
				newLined = writeCodepoint(REPLACEMENT_CHAR);
				amt -= 3;
				message += 3;
			}
			else newLined = writeCodepoint(REPLACEMENT_CHAR);
		}
		else newLined = writeCodepoint(ch);
		if (newLined) fullUpdate = true;
	}
	if (!paused) {
		if (fullUpdate) consoleDraw();
		else dmaCopyWords(1,
			gfxBuffer + (lineTop * SCREEN_WIDTH),
			bgGetGfxPtr(3) + (lineTop * SCREEN_WIDTH),
			SCREEN_WIDTH * defaultFont.tileHeight * sizeof(u16)
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
	if (defaultFont.tileWidth == 0) fontLoadDefault();
	consoleSetColor(0xFFFF);
}

// Pauses the DMA copies after every console write
void consolePause() {
	paused = true;
}
// Resumes the DMA copies after every console write, and performs one immediately
void consoleResume() {
	paused = false;
	consoleDraw();
}
void consoleClear() {
	memset(gfxBuffer, 0, sizeof(gfxBuffer));
	if (!paused) dmaFillWords(0, bgGetGfxPtr(3), sizeof(gfxBuffer));
	lineWidth = lineTop = 0;
	filled = false;
}