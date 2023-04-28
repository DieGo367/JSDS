#include "io/console.hpp"

#include <nds/arm9/background.h>
#include <nds/arm9/video.h>
#include <nds/arm9/cache.h>
#include <nds/arm9/console.h>
#include <stdio.h>
#include <stdlib.h>

#include "util/color.hpp"
#include "util/font.hpp"
#include "util/tonccpy.h"
#include "util/unicode.hpp"



const int TAB_SIZE = 2;
const int BUFFER_HEIGHT = SCREEN_HEIGHT;

static u16 gfxBuffer[SCREEN_WIDTH * BUFFER_HEIGHT] = {0};

u16 consoleHeight = SCREEN_HEIGHT;
u16 lineWidth = 0;
int linePos = 0;
bool paused = false;

NitroFont consoleFont = {0};
u16 colors[4] = {0};

u16 consoleSetColor(u16 color) {
	u16 prev = colors[3];
	colors[3] = color;
	colors[2] = colorBlend(colors[0], color, 80);
	colors[1] = colorBlend(colors[0], color, 20);
	return prev;
}
u16 consoleGetColor() { return colors[3]; }

u16 consoleSetBackground(u16 color) {
	u16 prev = colors[0];
	colors[0] = color;
	colors[1] = colorBlend(color, colors[3], 20);
	colors[2] = colorBlend(color, colors[3], 80);
	return prev;
}
u16 consoleGetBackground() { return colors[0]; }
NitroFont consoleGetFont() { return consoleFont; }

void consoleDraw() {
	int pos = linePos + consoleFont.tileHeight;
	if (pos <= consoleHeight) dmaCopyWords(0, gfxBuffer, bgGetGfxPtr(7), pos * SCREEN_WIDTH * sizeof(u16));
	else if (pos <= BUFFER_HEIGHT) dmaCopyWords(0, gfxBuffer + (pos - consoleHeight) * SCREEN_WIDTH, bgGetGfxPtr(7), consoleHeight * SCREEN_WIDTH * sizeof(u16));
	else {
		int bottom = pos % BUFFER_HEIGHT;
		int top = bottom - consoleHeight;
		if (top >= 0) dmaCopyWords(0, gfxBuffer + top * SCREEN_WIDTH, bgGetGfxPtr(7), consoleHeight * SCREEN_WIDTH * sizeof(u16));
		else {
			dmaCopyWords(0, gfxBuffer + (BUFFER_HEIGHT + top) * SCREEN_WIDTH, bgGetGfxPtr(7), -top * SCREEN_WIDTH * sizeof(u16));
			if (bottom) dmaCopyWords(0, gfxBuffer, bgGetGfxPtr(7) - (top * SCREEN_WIDTH), bottom * SCREEN_WIDTH * sizeof(u16));
		}
	}
}

void consoleDrawLine() {
	dmaCopyWords(0,
		gfxBuffer + (linePos % BUFFER_HEIGHT * SCREEN_WIDTH),
		bgGetGfxPtr(7) + (linePos + consoleFont.tileHeight <= consoleHeight ? linePos : consoleHeight - consoleFont.tileHeight) * SCREEN_WIDTH,
		SCREEN_WIDTH * consoleFont.tileHeight * sizeof(u16)
	);
}

void newLine() {
	if (colors[0]) for (u8 i = 0; i < consoleFont.tileHeight; i++) {
		toncset16(gfxBuffer + (((linePos + i) % BUFFER_HEIGHT) * SCREEN_WIDTH) + lineWidth, colors[0], SCREEN_WIDTH - lineWidth);
	}
	DC_FlushRange(gfxBuffer + (linePos % BUFFER_HEIGHT) * SCREEN_WIDTH, consoleFont.tileHeight * SCREEN_WIDTH * sizeof(u16));
	lineWidth = 0;
	linePos += consoleFont.tileHeight;
	toncset16(gfxBuffer + ((linePos % BUFFER_HEIGHT) * SCREEN_WIDTH), 0, SCREEN_WIDTH * consoleFont.tileHeight);
}

bool writeCodepoint(char16_t codepoint) {
	if (codepoint == '\n') {
		newLine();
		return true;
	}
	else if (codepoint == '\t') {
		u8 tabWidth = fontGetCodePointWidth(consoleFont, ' ') * TAB_SIZE;
		lineWidth = (lineWidth / tabWidth + 1) * tabWidth;
		if (lineWidth > SCREEN_WIDTH) {
			newLine();
			return true;
		}
		return false;
	}

	bool newLined = false;
	u8 width = fontGetCodePointWidth(consoleFont, codepoint);
	if (lineWidth + width > SCREEN_WIDTH) {
		newLine();
		newLined = true;
	}

	fontPrintCodePoint(consoleFont, colors, codepoint, gfxBuffer, SCREEN_WIDTH, lineWidth, linePos % BUFFER_HEIGHT);

	lineWidth += width;
	return newLined;
}

ssize_t writeIn(const char *message, size_t len) {
	if (!message) return 0;
	bool fullUpdate = false;
	u32 codepointLen;
	char16_t *codepoints = UTF8toUTF16(message, len, &codepointLen);
	char16_t *ptr = codepoints;
	while (codepointLen-- > 0) {
		if (writeCodepoint(*(ptr++))) fullUpdate = true;
	}
	free(codepoints);
	if (!paused) {
		if (fullUpdate) consoleDraw();
		else consoleDrawLine();
	}
	return len;
}

void consoleInit(NitroFont font) {
	videoSetModeSub(MODE_3_2D);
	vramSetBankC(VRAM_C_SUB_BG);
	bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	consoleSetCustomStdout(writeIn);
	consoleSetCustomStderr(writeIn);
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	consoleFont = font;
	consoleSetColor(0xFFFF);
}

void consolePrintNoWrap(const char *message) {
	fontPrintString(consoleFont, colors, message, gfxBuffer, SCREEN_WIDTH, lineWidth, linePos % BUFFER_HEIGHT, SCREEN_WIDTH - lineWidth);
	if (!paused) consoleDrawLine();
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
void consoleClean() {
	toncset(gfxBuffer, 0, sizeof(gfxBuffer));
	if (!paused) dmaFillWords(0, bgGetGfxPtr(7), SCREEN_WIDTH * consoleHeight * sizeof(u16));
	lineWidth = linePos = 0;
}
void consoleShrink() {
	consoleHeight = SCREEN_HEIGHT / 2;
	if (!paused) {
		consoleDraw();
		dmaFillWords(0, bgGetGfxPtr(7) + SCREEN_WIDTH * SCREEN_HEIGHT / 2, SCREEN_WIDTH * SCREEN_HEIGHT / 2 * sizeof(u16));
	}
}
void consoleExpand() {
	consoleHeight = SCREEN_HEIGHT;
	if (!paused) consoleDraw();
}