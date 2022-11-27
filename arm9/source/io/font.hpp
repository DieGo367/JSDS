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
u8 fontGetCharWidth(NitroFont font, u16 codepoint);
void fontPrintChar(NitroFont font, u16 *palette, u16 codepoint, u16 *buffer, u32 bufferWidth, u32 x, u32 y);

#endif /* JSDS_FONT_HPP */