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

extern NitroFont defaultFont;

NitroFont fontLoad(const u8 *data);
void fontLoadDefault();
u8 fontGetCodePointWidth(NitroFont font, u16 codepoint);
inline u8 fontGetCharWidth(NitroFont font, char chr) {
	return fontGetCodePointWidth(font, (u16) chr);
}
void fontPrintCodePoint(NitroFont font, const u16 *palette, u16 codepoint, u16 *buffer, u32 bufferWidth, u32 x, u32 y);
inline void fontPrintChar(NitroFont font, const u16 *palette, char chr, u16 *buffer, u32 bufferWidth, u32 x, u32 y) {
	return fontPrintCodePoint(font, palette, (u16) chr, buffer, bufferWidth, x, y);
}
void fontPrintUnicode(NitroFont font, const u16 *palette, const u16 *codepoints, u16 *buffer, u32 bufferWidth, u32 x, u32 y, u32 maxWidth);
void fontPrintString(NitroFont font, const u16 *palette, const char *str, u16 *buffer, u32 bufferWidth, u32 x, u32 y, u32 maxWidth);

#endif /* JSDS_FONT_HPP */