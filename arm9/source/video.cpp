#include <nds/arm9/video.h>
#include <stdlib.h>

#include "helpers.hpp"
#include "util/color.hpp"



#define FOR_VRAM_SYMBOL_NAMES(DO) \
	DO(LCD); DO(ARM7_0); DO(ARM7_1); \
	DO(MAIN_BG); DO(MAIN_BG_0x4000); DO(MAIN_BG_0x10000); DO(MAIN_BG_0x14000); DO(MAIN_BG_0x20000); DO(MAIN_BG_0x40000); DO(MAIN_BG_0x60000); \
	DO(MAIN_SPRITE); DO(MAIN_SPRITE_0x4000); DO(MAIN_SPRITE_0x10000); DO(MAIN_SPRITE_0x14000); DO(MAIN_SPRITE_0x20000); \
	DO(MAIN_BG_EXT_PALETTE_0); DO(MAIN_BG_EXT_PALETTE_2); DO(MAIN_SPRITE_EXT_PALETTE); \
	DO(SUB_BG); DO(SUB_BG_0x8000); DO(SUB_SPRITE); DO(SUB_BG_EXT_PALETTE); DO(SUB_SPRITE_EXT_PALETTE); \
	DO(TEXTURE_0); DO(TEXTURE_1); DO(TEXTURE_2); DO(TEXTURE_3); \
	DO(TEXTURE_PALETTE_0); DO(TEXTURE_PALETTE_1); DO(TEXTURE_PALETTE_4); DO(TEXTURE_PALETTE_5);

#define DEFINE_SYMBOL(name) jerry_value_t symbol_##name
FOR_VRAM_SYMBOL_NAMES(DEFINE_SYMBOL)



FUNCTION(VRAM_setBankA) {
	REQUIRE(1);
	if (strictEqual(args[0], symbol_LCD)) vramSetBankA(VRAM_A_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x20000)) vramSetBankA(VRAM_A_MAIN_BG_0x06020000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x40000)) vramSetBankA(VRAM_A_MAIN_BG_0x06040000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x60000)) vramSetBankA(VRAM_A_MAIN_BG_0x06060000);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE)) vramSetBankA(VRAM_A_MAIN_SPRITE_0x06400000);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE_0x20000)) vramSetBankA(VRAM_A_MAIN_SPRITE_0x06420000);
	else if (strictEqual(args[0], symbol_TEXTURE_0)) vramSetBankA(VRAM_A_TEXTURE_SLOT0);
	else if (strictEqual(args[0], symbol_TEXTURE_1)) vramSetBankA(VRAM_A_TEXTURE_SLOT1);
	else if (strictEqual(args[0], symbol_TEXTURE_2)) vramSetBankA(VRAM_A_TEXTURE_SLOT2);
	else if (strictEqual(args[0], symbol_TEXTURE_3)) vramSetBankA(VRAM_A_TEXTURE_SLOT3);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank A.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankB) {
	REQUIRE(1);
	if (strictEqual(args[0], symbol_LCD)) vramSetBankB(VRAM_B_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankB(VRAM_B_MAIN_BG_0x06000000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x20000)) vramSetBankB(VRAM_B_MAIN_BG_0x06020000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x40000)) vramSetBankB(VRAM_B_MAIN_BG_0x06040000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x60000)) vramSetBankB(VRAM_B_MAIN_BG_0x06060000);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE)) vramSetBankB(VRAM_B_MAIN_SPRITE_0x06400000);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE_0x20000)) vramSetBankB(VRAM_B_MAIN_SPRITE_0x06420000);
	else if (strictEqual(args[0], symbol_TEXTURE_0)) vramSetBankB(VRAM_B_TEXTURE_SLOT0);
	else if (strictEqual(args[0], symbol_TEXTURE_1)) vramSetBankB(VRAM_B_TEXTURE_SLOT1);
	else if (strictEqual(args[0], symbol_TEXTURE_2)) vramSetBankB(VRAM_B_TEXTURE_SLOT2);
	else if (strictEqual(args[0], symbol_TEXTURE_3)) vramSetBankB(VRAM_B_TEXTURE_SLOT3);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank B.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankC) {
	REQUIRE(1);
	if (strictEqual(args[0], symbol_LCD)) vramSetBankC(VRAM_C_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankC(VRAM_C_MAIN_BG_0x06000000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x20000)) vramSetBankC(VRAM_C_MAIN_BG_0x06020000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x40000)) vramSetBankC(VRAM_C_MAIN_BG_0x06040000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x60000)) vramSetBankC(VRAM_C_MAIN_BG_0x06060000);
	else if (strictEqual(args[0], symbol_ARM7_0)) vramSetBankC(VRAM_C_ARM7_0x06000000);
	else if (strictEqual(args[0], symbol_ARM7_1)) vramSetBankC(VRAM_C_ARM7_0x06020000);
	else if (strictEqual(args[0], symbol_SUB_BG)) vramSetBankC(VRAM_C_SUB_BG);
	else if (strictEqual(args[0], symbol_TEXTURE_0)) vramSetBankC(VRAM_C_TEXTURE_SLOT0);
	else if (strictEqual(args[0], symbol_TEXTURE_1)) vramSetBankC(VRAM_C_TEXTURE_SLOT1);
	else if (strictEqual(args[0], symbol_TEXTURE_2)) vramSetBankC(VRAM_C_TEXTURE_SLOT2);
	else if (strictEqual(args[0], symbol_TEXTURE_3)) vramSetBankC(VRAM_C_TEXTURE_SLOT3);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank C.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankD) {
	REQUIRE(1);
	if (strictEqual(args[0], symbol_LCD)) vramSetBankD(VRAM_D_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankD(VRAM_D_MAIN_BG_0x06000000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x20000)) vramSetBankD(VRAM_D_MAIN_BG_0x06020000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x40000)) vramSetBankD(VRAM_D_MAIN_BG_0x06040000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x60000)) vramSetBankD(VRAM_D_MAIN_BG_0x06060000);
	else if (strictEqual(args[0], symbol_ARM7_0)) vramSetBankD(VRAM_D_ARM7_0x06000000);
	else if (strictEqual(args[0], symbol_ARM7_1)) vramSetBankD(VRAM_D_ARM7_0x06020000);
	else if (strictEqual(args[0], symbol_SUB_SPRITE)) vramSetBankD(VRAM_D_SUB_SPRITE);
	else if (strictEqual(args[0], symbol_TEXTURE_0)) vramSetBankD(VRAM_D_TEXTURE_SLOT0);
	else if (strictEqual(args[0], symbol_TEXTURE_1)) vramSetBankD(VRAM_D_TEXTURE_SLOT1);
	else if (strictEqual(args[0], symbol_TEXTURE_2)) vramSetBankD(VRAM_D_TEXTURE_SLOT2);
	else if (strictEqual(args[0], symbol_TEXTURE_3)) vramSetBankD(VRAM_D_TEXTURE_SLOT3);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank D.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankE) {
	REQUIRE(1);
	if (strictEqual(args[0], symbol_LCD)) vramSetBankE(VRAM_E_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankE(VRAM_E_MAIN_BG);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE)) vramSetBankE(VRAM_E_MAIN_SPRITE);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_0)) vramSetBankE(VRAM_E_TEX_PALETTE);
	else if (strictEqual(args[0], symbol_MAIN_BG_EXT_PALETTE_0)) vramSetBankE(VRAM_E_BG_EXT_PALETTE);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank E.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankF) {
	REQUIRE(1);
	if (strictEqual(args[0], symbol_LCD)) vramSetBankF(VRAM_F_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankF(VRAM_F_MAIN_BG_0x06000000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x4000)) vramSetBankF(VRAM_F_MAIN_BG_0x06004000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x10000)) vramSetBankF(VRAM_F_MAIN_BG_0x06010000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x14000)) vramSetBankF(VRAM_F_MAIN_BG_0x06014000);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_0)) vramSetBankF(VRAM_F_TEX_PALETTE_SLOT0);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_1)) vramSetBankF(VRAM_F_TEX_PALETTE_SLOT1);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_4)) vramSetBankF(VRAM_F_TEX_PALETTE_SLOT4);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_5)) vramSetBankF(VRAM_F_TEX_PALETTE_SLOT5);
	else if (strictEqual(args[0], symbol_MAIN_BG_EXT_PALETTE_0)) vramSetBankF(VRAM_F_BG_EXT_PALETTE_SLOT01);
	else if (strictEqual(args[0], symbol_MAIN_BG_EXT_PALETTE_2)) vramSetBankF(VRAM_F_BG_EXT_PALETTE_SLOT23);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE_EXT_PALETTE)) vramSetBankF(VRAM_F_SPRITE_EXT_PALETTE);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank F.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankG) {
	REQUIRE(1);
	if (strictEqual(args[0], symbol_LCD)) vramSetBankG(VRAM_G_LCD);
	else if (strictEqual(args[0], symbol_MAIN_BG)) vramSetBankG(VRAM_G_MAIN_BG_0x06000000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x4000)) vramSetBankG(VRAM_G_MAIN_BG_0x06004000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x10000)) vramSetBankG(VRAM_G_MAIN_BG_0x06010000);
	else if (strictEqual(args[0], symbol_MAIN_BG_0x14000)) vramSetBankG(VRAM_G_MAIN_BG_0x06014000);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_0)) vramSetBankG(VRAM_G_TEX_PALETTE_SLOT0);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_1)) vramSetBankG(VRAM_G_TEX_PALETTE_SLOT1);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_4)) vramSetBankG(VRAM_G_TEX_PALETTE_SLOT4);
	else if (strictEqual(args[0], symbol_TEXTURE_PALETTE_5)) vramSetBankG(VRAM_G_TEX_PALETTE_SLOT5);
	else if (strictEqual(args[0], symbol_MAIN_BG_EXT_PALETTE_0)) vramSetBankG(VRAM_G_BG_EXT_PALETTE_SLOT01);
	else if (strictEqual(args[0], symbol_MAIN_BG_EXT_PALETTE_2)) vramSetBankG(VRAM_G_BG_EXT_PALETTE_SLOT23);
	else if (strictEqual(args[0], symbol_MAIN_SPRITE_EXT_PALETTE)) vramSetBankG(VRAM_G_SPRITE_EXT_PALETTE);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank G.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankH) {
	REQUIRE(1);
	if (strictEqual(args[0], symbol_LCD)) vramSetBankH(VRAM_H_LCD);
	else if (strictEqual(args[0], symbol_SUB_BG)) vramSetBankH(VRAM_H_SUB_BG);
	else if (strictEqual(args[0], symbol_SUB_BG_EXT_PALETTE)) vramSetBankH(VRAM_H_SUB_BG_EXT_PALETTE);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank H.");
	return JS_UNDEFINED;
}

FUNCTION(VRAM_setBankI) {
	REQUIRE(1);
	if (strictEqual(args[0], symbol_LCD)) vramSetBankI(VRAM_I_LCD);
	else if (strictEqual(args[0], symbol_SUB_BG_0x8000)) vramSetBankI(VRAM_I_SUB_BG_0x06208000);
	else if (strictEqual(args[0], symbol_SUB_SPRITE)) vramSetBankI(VRAM_I_SUB_SPRITE);
	else if (strictEqual(args[0], symbol_SUB_SPRITE_EXT_PALETTE)) vramSetBankI(VRAM_I_SUB_SPRITE_EXT_PALETTE);
	else return TypeError("Expected a VRAM symbol compatible with VRAM bank I.");
	return JS_UNDEFINED;
}

FUNCTION(Video_main_setBackdropColor) {
	REQUIRE(1);
	char *colorDesc = toRawString(args[0]);
	u16 color = colorParse(colorDesc, 0);
	free(colorDesc);
	setBackdropColor(color);
	return JS_UNDEFINED;
}
FUNCTION(Video_sub_setBackdropColor) {
	REQUIRE(1);
	char *colorDesc = toRawString(args[0]);
	u16 color = colorParse(colorDesc, 0);
	free(colorDesc);
	setBackdropColorSub(color);
	return JS_UNDEFINED;
}

FUNCTION(Video_main_setMode) {
	REQUIRE(1); EXPECT(jerry_value_is_number(args[0]), number);
	bool is3D = argCount > 1 && jerry_get_boolean_value(args[1]);
	u32 mode = jerry_value_as_uint32(args[0]);
	if (mode == 0) videoSetMode(is3D ? MODE_0_3D : MODE_0_2D);
	else if (mode == 1) videoSetMode(is3D ? MODE_1_3D : MODE_1_2D);
	else if (mode == 2) videoSetMode(is3D ? MODE_2_3D : MODE_2_2D);
	else if (mode == 3) videoSetMode(is3D ? MODE_3_3D : MODE_3_2D);
	else if (mode == 4) videoSetMode(is3D ? MODE_4_3D : MODE_4_2D);
	else if (mode == 5) videoSetMode(is3D ? MODE_5_3D : MODE_5_2D);
	else if (mode == 6) videoSetMode(is3D ? MODE_6_3D : MODE_6_2D);
	else return TypeError("Invalid video mode.");
	return JS_UNDEFINED;
}
FUNCTION(Video_sub_setMode) {
	REQUIRE(1); EXPECT(jerry_value_is_number(args[0]), number);
	u32 mode = jerry_value_as_uint32(args[0]);
	if (mode == 0) videoSetModeSub(MODE_0_2D);
	else if (mode == 1) videoSetModeSub(MODE_1_2D);
	else if (mode == 2) videoSetModeSub(MODE_2_2D);
	else if (mode == 3) videoSetModeSub(MODE_3_2D);
	else if (mode == 4) videoSetModeSub(MODE_4_2D);
	else if (mode == 5) videoSetModeSub(MODE_5_2D);
	else return TypeError("Invalid video mode.");
	return JS_UNDEFINED;
}

void exposeVideoAPI(jerry_value_t global) {
	#define CREATE_SYMBOL(name) symbol_##name = Symbol(#name)
	FOR_VRAM_SYMBOL_NAMES(CREATE_SYMBOL)
	
	jerry_value_t VRAM_obj = createObject(global, "VRAM");
	jerry_value_t VRAM_A_obj = createObject(VRAM_obj, "A");
	setSymbol(VRAM_A_obj, symbol_LCD);
	setSymbol(VRAM_A_obj, symbol_MAIN_BG);
	setSymbol(VRAM_A_obj, symbol_MAIN_BG_0x20000);
	setSymbol(VRAM_A_obj, symbol_MAIN_BG_0x40000);
	setSymbol(VRAM_A_obj, symbol_MAIN_BG_0x60000);
	setSymbol(VRAM_A_obj, symbol_MAIN_SPRITE);
	setSymbol(VRAM_A_obj, symbol_MAIN_SPRITE_0x20000);
	setSymbol(VRAM_A_obj, symbol_TEXTURE_0);
	setSymbol(VRAM_A_obj, symbol_TEXTURE_1);
	setSymbol(VRAM_A_obj, symbol_TEXTURE_2);
	setSymbol(VRAM_A_obj, symbol_TEXTURE_3);
	setProperty(VRAM_obj, "B", VRAM_A_obj);
	jerry_release_value(VRAM_A_obj);
	jerry_value_t VRAM_C_obj = createObject(VRAM_obj, "C");
	setSymbol(VRAM_C_obj, symbol_LCD);
	setSymbol(VRAM_C_obj, symbol_ARM7_0);
	setSymbol(VRAM_C_obj, symbol_ARM7_1);
	setSymbol(VRAM_C_obj, symbol_MAIN_BG);
	setSymbol(VRAM_C_obj, symbol_MAIN_BG_0x20000);
	setSymbol(VRAM_C_obj, symbol_MAIN_BG_0x40000);
	setSymbol(VRAM_C_obj, symbol_MAIN_BG_0x60000);
	setSymbol(VRAM_C_obj, symbol_SUB_BG);
	setSymbol(VRAM_C_obj, symbol_TEXTURE_0);
	setSymbol(VRAM_C_obj, symbol_TEXTURE_1);
	setSymbol(VRAM_C_obj, symbol_TEXTURE_2);
	setSymbol(VRAM_C_obj, symbol_TEXTURE_3);
	jerry_release_value(VRAM_C_obj);
	jerry_value_t VRAM_D_obj = createObject(VRAM_obj, "D");
	setSymbol(VRAM_D_obj, symbol_LCD);
	setSymbol(VRAM_D_obj, symbol_ARM7_0);
	setSymbol(VRAM_D_obj, symbol_ARM7_1);
	setSymbol(VRAM_D_obj, symbol_MAIN_BG);
	setSymbol(VRAM_D_obj, symbol_MAIN_BG_0x20000);
	setSymbol(VRAM_D_obj, symbol_MAIN_BG_0x40000);
	setSymbol(VRAM_D_obj, symbol_MAIN_BG_0x60000);
	setSymbol(VRAM_D_obj, symbol_SUB_SPRITE);
	setSymbol(VRAM_D_obj, symbol_TEXTURE_0);
	setSymbol(VRAM_D_obj, symbol_TEXTURE_1);
	setSymbol(VRAM_D_obj, symbol_TEXTURE_2);
	setSymbol(VRAM_D_obj, symbol_TEXTURE_3);
	jerry_release_value(VRAM_D_obj);
	jerry_value_t VRAM_E_obj = createObject(VRAM_obj, "E");
	setSymbol(VRAM_E_obj, symbol_LCD);
	setSymbol(VRAM_E_obj, symbol_MAIN_BG);
	setSymbol(VRAM_E_obj, symbol_MAIN_SPRITE);
	setSymbol(VRAM_E_obj, symbol_MAIN_BG_EXT_PALETTE_0);
	setSymbol(VRAM_E_obj, symbol_TEXTURE_PALETTE_0);
	jerry_release_value(VRAM_E_obj);
	jerry_value_t VRAM_F_obj = createObject(VRAM_obj, "F");
	setSymbol(VRAM_F_obj, symbol_LCD);
	setSymbol(VRAM_F_obj, symbol_MAIN_BG);
	setSymbol(VRAM_F_obj, symbol_MAIN_BG_0x4000);
	setSymbol(VRAM_F_obj, symbol_MAIN_BG_0x10000);
	setSymbol(VRAM_F_obj, symbol_MAIN_BG_0x14000);
	setSymbol(VRAM_F_obj, symbol_MAIN_SPRITE);
	setSymbol(VRAM_F_obj, symbol_MAIN_SPRITE_0x4000);
	setSymbol(VRAM_F_obj, symbol_MAIN_SPRITE_0x10000);
	setSymbol(VRAM_F_obj, symbol_MAIN_SPRITE_0x20000);
	setSymbol(VRAM_F_obj, symbol_MAIN_BG_EXT_PALETTE_0);
	setSymbol(VRAM_F_obj, symbol_MAIN_BG_EXT_PALETTE_2);
	setSymbol(VRAM_F_obj, symbol_MAIN_SPRITE_EXT_PALETTE);
	setSymbol(VRAM_F_obj, symbol_TEXTURE_PALETTE_0);
	setSymbol(VRAM_F_obj, symbol_TEXTURE_PALETTE_1);
	setSymbol(VRAM_F_obj, symbol_TEXTURE_PALETTE_4);
	setSymbol(VRAM_F_obj, symbol_TEXTURE_PALETTE_5);
	setProperty(VRAM_obj, "G", VRAM_F_obj);
	jerry_release_value(VRAM_F_obj);
	jerry_value_t VRAM_H_obj = createObject(VRAM_obj, "H");
	setSymbol(VRAM_H_obj, symbol_LCD);
	setSymbol(VRAM_H_obj, symbol_SUB_BG);
	setSymbol(VRAM_H_obj, symbol_SUB_BG_EXT_PALETTE);
	jerry_release_value(VRAM_H_obj);
	jerry_value_t VRAM_I_obj = createObject(VRAM_obj, "I");
	setSymbol(VRAM_I_obj, symbol_LCD);
	setSymbol(VRAM_I_obj, symbol_SUB_BG_0x8000);
	setSymbol(VRAM_I_obj, symbol_SUB_SPRITE);
	setSymbol(VRAM_I_obj, symbol_SUB_SPRITE_EXT_PALETTE);
	jerry_release_value(VRAM_I_obj);
	setMethod(VRAM_obj, "setBankA", VRAM_setBankA);
	setMethod(VRAM_obj, "setBankB", VRAM_setBankB);
	setMethod(VRAM_obj, "setBankC", VRAM_setBankC);
	setMethod(VRAM_obj, "setBankD", VRAM_setBankD);
	setMethod(VRAM_obj, "setBankE", VRAM_setBankE);
	setMethod(VRAM_obj, "setBankF", VRAM_setBankF);
	setMethod(VRAM_obj, "setBankG", VRAM_setBankG);
	setMethod(VRAM_obj, "setBankH", VRAM_setBankH);
	setMethod(VRAM_obj, "setBankI", VRAM_setBankI);
	jerry_release_value(VRAM_obj);

	jerry_value_t Video = createObject(global, "Video");
	jerry_value_t main = createObject(Video, "main");
	setMethod(main, "setBackdropColor", Video_main_setBackdropColor);
	setMethod(main, "setMode", Video_main_setMode);
	jerry_release_value(main);
	jerry_value_t sub = createObject(Video, "sub");
	setMethod(sub, "setBackdropColor", Video_sub_setBackdropColor);
	setMethod(sub, "setMode", Video_sub_setMode);
	jerry_release_value(sub);
	jerry_release_value(Video);
}

void releaseVideoReferences() {
	#define RELEASE_SYMBOL(name) jerry_release_value(symbol_##name)
	FOR_VRAM_SYMBOL_NAMES(RELEASE_SYMBOL)
}