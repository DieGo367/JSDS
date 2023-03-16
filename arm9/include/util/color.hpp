#ifndef JSDS_COLOR_HPP
#define JSDS_COLOR_HPP

#include <nds/ndstypes.h>

u16 colorBlend(u16 baseColor, u16 targetColor, u8 strength);
u16 colorParse(const char *colorDesc, u16 noneColor);

#endif /* JSDS_COLOR_HPP */