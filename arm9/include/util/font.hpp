#ifndef JSDS_FONT_HPP
#define JSDS_FONT_HPP

#include <nds/ndstypes.h>



#define CHAR_RANGE 0x10000
#define REPLACEMENT_CHAR 0xFFFD
#define NO_TILE 0xFFFF

struct NitroFont {
	u8 tileWidth;
	u8 tileHeight;
	u16 tileSize;
	u8 encoding;
	u8 bitdepth;
	bool replace;

	u8 *tileData;
	u8 *widthData;
	u16 *charMap;
};

NitroFont fontLoad(const u8 *data);
u8 fontGetCodePointWidth(NitroFont font, char16_t codepoint);
void fontPrintCodePoint(NitroFont font, const u16 *palette, char16_t codepoint, u16 *buffer, u32 bufferWidth, u32 x, u32 y);
void fontPrintUnicode(NitroFont font, const u16 *palette, const char16_t *codepoints, u16 *buffer, u32 bufferWidth, u32 x, u32 y, u32 maxWidth, bool scroll);
void fontPrintString(NitroFont font, const u16 *palette, const char *str, u16 *buffer, u32 bufferWidth, u32 x, u32 y, u32 maxWidth, bool scroll);

#endif /* JSDS_FONT_HPP */