#include "encoding.hpp"
#include "event.hpp"
#include "file.hpp"
#include "io.hpp"
#include "io/keyboard.hpp"
#include "sprite.hpp"
#include "system.hpp"
#include "timeouts.hpp"
#include "util/helpers.hpp"
#include "video.hpp"

// beta, will get rid of these soon
#include <nds/arm9/video.h>
#include <nds/arm9/background.h>



jerry_value_t ref_global;
JS_class ref_Error;
jerry_value_t ref_func_push;
jerry_value_t ref_func_slice;
jerry_value_t ref_func_splice;
jerry_value_t ref_str_name;
jerry_value_t ref_str_constructor;
jerry_value_t ref_str_prototype;
jerry_value_t ref_str_backtrace;
jerry_value_t ref_str_removed;
jerry_value_t ref_str_main;
jerry_value_t ref_sym_toStringTag;



FUNCTION(BETA_gfxInit) {
	videoSetMode(MODE_3_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	return JS_UNDEFINED;
}

FUNCTION(BETA_gfxPixel) {
	REQUIRE(3);
	u8 x = jerry_value_as_uint32(args[0]);
	u8 y = jerry_value_as_uint32(args[1]);
	u16 color = jerry_value_as_uint32(args[2]);
	bgGetGfxPtr(3)[x + y*256] = color;
	return JS_UNDEFINED;
}

FUNCTION(BETA_gfxRect) {
	REQUIRE(5);
	u8 x = jerry_value_as_uint32(args[0]);
	u8 y = jerry_value_as_uint32(args[1]) % 192;
	u16 width = jerry_value_as_uint32(args[2]);
	u16 height = jerry_value_as_uint32(args[3]);
	u16 color = jerry_value_as_uint32(args[4]);
	u16 *gfx = bgGetGfxPtr(3);
	
	if (width + x > 256) width = 256 - x;
	if (height + y > 192) height = 192 - y;

	if (width == 0 || height == 0) return JS_UNDEFINED;

	for (u8 i = 0; i < height; i++) {
		dmaFillHalfWords(color, gfx + x + ((y + i) * 256), width * 2);
	}
	return JS_UNDEFINED;
}

void exposeAPI() {
	// hold some internal references first
	ref_global = jerry_get_global_object();
	ref_Error.constructor = getProperty(ref_global, "Error");
	ref_Error.prototype = jerry_get_property(ref_Error.constructor, ref_str_prototype);
	jerry_value_t tempArr = jerry_create_array(0);
	ref_func_push = getProperty(tempArr, "push");
	ref_func_slice = getProperty(tempArr, "slice");
	ref_func_splice = getProperty(tempArr, "splice");
	jerry_release_value(tempArr);
	ref_str_name = String("name");
	ref_str_constructor = String("constructor");
	ref_str_prototype = String("prototype");
	ref_str_backtrace = String("backtrace");
	ref_str_removed = String("removed");
	ref_str_main = String("main");
	ref_sym_toStringTag = jerry_get_well_known_symbol(JERRY_SYMBOL_TO_STRING_TAG);

	setProperty(ref_global, "self", ref_global);

	setMethod(ref_global, "close", RETURN((abortFlag = userClosed = true, jerry_create_abort_from_value(String(""), true))));

	exposeTimeoutAPI(ref_global);
	exposeIOAPI(ref_global);
	exposeEncodingAPI(ref_global);
	exposeFileAPI(ref_global);
	exposeEventAPI(ref_global);
	exposeSystemAPI(ref_global);
	exposeVideoAPI(ref_global);
	exposeSpriteAPI(ref_global);

	// gotta get rid of these soon
	jerry_value_t beta = createObject(ref_global, "beta");
	setMethod(beta, "gfxInit", BETA_gfxInit);
	setMethod(beta, "gfxPixel", BETA_gfxPixel);
	setMethod(beta, "gfxRect", BETA_gfxRect);
	jerry_release_value(beta);
}

void releaseReferences() {
	jerry_release_value(ref_global);
	releaseClass(ref_Error);
	jerry_release_value(ref_func_push);
	jerry_release_value(ref_func_slice);
	jerry_release_value(ref_func_splice);
	jerry_release_value(ref_str_name);
	jerry_release_value(ref_str_constructor);
	jerry_release_value(ref_str_prototype);
	jerry_release_value(ref_str_backtrace);
	jerry_release_value(ref_str_removed);
	jerry_release_value(ref_str_main);
	jerry_release_value(ref_sym_toStringTag);

	releaseIOReferences();
	releaseEventReferences();
	releaseVideoReferences();
	releaseSpriteReferences();
	releaseFileReferences();
}