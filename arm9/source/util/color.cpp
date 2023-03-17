#include "util/color.hpp"

#include <nds/bios.h>
#include <stdlib.h>
#include <string.h>



// Strength ranges from 0-100, 0 = fully base color, 100 = fully target color
u16 colorBlend(u16 baseColor, u16 targetColor, u8 strength) {
	if (!((baseColor | targetColor) & BIT(15))) return 0x0000;
	
	u8 neg = 100 - strength;
	u16 baseBlue = (baseColor >> 10) & 0b11111;
	u16 baseGreen = (baseColor >> 5) & 0b11111;
	u16 baseRed = (baseColor) & 0b11111;
	baseBlue *= baseBlue, baseGreen *= baseGreen, baseRed *= baseRed;
	
	u16 targetBlue = (targetColor >> 10) & 0b11111;
	u16 targetGreen = (targetColor >> 5) & 0b11111;
	u16 targetRed = (targetColor) & 0b11111;
	targetBlue *= targetBlue, targetGreen *= targetGreen, targetRed *= targetRed;

	u8 blue = swiSqrt(baseBlue * neg / 100 + targetBlue * strength / 100);
	u8 green = swiSqrt(baseGreen * neg / 100 + targetGreen * strength / 100);
	u8 red = swiSqrt(baseRed * neg / 100 + targetRed * strength / 100);

	return BIT(15) | blue << 10 | green << 5 | red;
}

u16 colorParse(const char *colorDesc, u16 noneColor) {
	char *endptr = NULL;
	if (colorDesc[0] == '#') {
		u32 len = strlen(colorDesc);
		if (len == 5) { // BGR15 hex code
			u16 color = strtoul(colorDesc + 1, &endptr, 16);
			if (endptr == colorDesc + 1) return noneColor;
			else return color;
		}
		else if (len == 7) { // RGB hex code
			u32 colorCode = strtoul(colorDesc + 1, &endptr, 16);
			if (endptr == colorDesc + 1) return noneColor;
			else {
				u8 red = (colorCode >> 16 & 0xFF) * 31 / 255;
				u8 green = (colorCode >> 8 & 0xFF) * 31 / 255;
				u8 blue = (colorCode & 0xFF) * 31 / 255;
				return BIT(15) | blue << 10 | green << 5 | red;
			}
		}
	}
	// raw number input (i.e. from DS.profile.color)
	u16 color = strtoul(colorDesc, &endptr, 0);
	if (endptr != colorDesc) return color;
	// list of CSS Level 2 colors + none and transparent
	else if (strcmp(colorDesc, "none") == 0) return noneColor;
	else if (strcmp(colorDesc, "transparent") == 0) return 0x0000;
	else if (strcmp(colorDesc, "black") == 0) return 0x8000;
	else if (strcmp(colorDesc, "silver") == 0) return 0xDEF7;
	else if (strcmp(colorDesc, "gray") == 0 || strcmp(colorDesc, "grey") == 0) return 0xC210;
	else if (strcmp(colorDesc, "white") == 0) return 0xFFFF;
	else if (strcmp(colorDesc, "maroon") == 0) return 0x8010;
	else if (strcmp(colorDesc, "red") == 0) return 0x801F;
	else if (strcmp(colorDesc, "purple") == 0) return 0xC010;
	else if (strcmp(colorDesc, "fuchsia") == 0 || strcmp(colorDesc, "magenta") == 0) return 0xFC1F;
	else if (strcmp(colorDesc, "green") == 0) return 0x8200;
	else if (strcmp(colorDesc, "lime") == 0) return 0x83E0;
	else if (strcmp(colorDesc, "olive") == 0) return 0x8210;
	else if (strcmp(colorDesc, "yellow") == 0) return 0x83FF;
	else if (strcmp(colorDesc, "navy") == 0) return 0xC000;
	else if (strcmp(colorDesc, "blue") == 0) return 0xFC00;
	else if (strcmp(colorDesc, "teal") == 0) return 0xC200;
	else if (strcmp(colorDesc, "aqua") == 0 || strcmp(colorDesc, "cyan") == 0) return 0xFFE0;
	else if (strcmp(colorDesc, "orange") == 0) return 0x829F;
	else return noneColor;
}