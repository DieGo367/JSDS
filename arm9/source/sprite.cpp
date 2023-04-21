#include <nds/arm9/sprite.h>
#include <nds/arm9/trig_lut.h>

#include "event.hpp"
#include "helpers.hpp"



jerry_value_t ref_str_main;
jerry_value_t ref_str_removed;
JS_class ref_Sprite;
JS_class ref_PalettedSprite;
JS_class ref_BitmapSprite;
JS_class ref_SpriteGraphic;
JS_class ref_SpriteAffineMatrix;

const char WAS_REMOVED[] = "Using a previously removed object.";
#define NOT_REMOVED(obj) if (testInternal(obj, ref_str_removed)) return TypeError(WAS_REMOVED)

#define BOUND(n, min, max) n < min ? min : n > max ? max : n

bool spriteUpdateMain = false;
bool spriteUpdateSub = false;
u8 spriteUsage[SPRITE_COUNT] = {0};
#define USAGE_SPRITE_MAIN BIT(0)
#define USAGE_MATRIX_MAIN BIT(1)
#define USAGE_SPRITE_SUB BIT(2)
#define USAGE_MATRIX_SUB BIT(3)
#define SPRITE_ENGINE(obj) (testInternal(obj, ref_str_main) ? &oamMain : &oamSub)
#define SPRITE_ENTRY(obj) (SPRITE_ENGINE(obj)->oamMemory + getID(obj))

inline int getID(jerry_value_t obj) {
	jerry_value_t idNum = getInternal(obj, "id");
	int id = jerry_value_as_integer(idNum);
	jerry_release_value(idNum);
	return id;
}

void spriteUpdate() {
	if (spriteUpdateMain) oamUpdate(&oamMain);
	if (spriteUpdateSub) oamUpdate(&oamSub);
}



FUNCTION(Sprite_set_x) {
	NOT_REMOVED(thisValue);
	SPRITE_ENTRY(thisValue)->x = jerry_value_as_uint32(args[0]);
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_x) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(SPRITE_ENTRY(thisValue)->x);
}

FUNCTION(Sprite_set_y) {
	NOT_REMOVED(thisValue);
	SPRITE_ENTRY(thisValue)->y = jerry_value_as_uint32(args[0]);
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_y) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(SPRITE_ENTRY(thisValue)->y);
}

FUNCTION(Sprite_setPosition) {
	NOT_REMOVED(thisValue);
	REQUIRE(2);
	oamSetXY(SPRITE_ENGINE(thisValue), getID(thisValue), jerry_value_as_int32(args[0]), jerry_value_as_int32(args[1]));
	return JS_UNDEFINED;
}

FUNCTION(Sprite_set_gfx) {
	NOT_REMOVED(thisValue);
	EXPECT(isInstance(args[0], ref_SpriteGraphic), SpriteGraphic);
	NOT_REMOVED(args[0]);
	OamState *engine = SPRITE_ENGINE(thisValue);
	if (SPRITE_ENGINE(args[0]) != engine) return TypeError("Given SpriteGraphic was from the wrong engine.");
	
	jerry_value_t sizeNum = getInternal(args[0], "size");
	jerry_value_t bppNum = getInternal(args[0], "colorFormat");
	SpriteSize size = (SpriteSize) jerry_value_as_uint32(sizeNum);
	int bpp = jerry_value_as_int32(bppNum);
	jerry_release_value(sizeNum);
	jerry_release_value(bppNum);
	jerry_value_t typedArray = getInternal(args[0], "data");
	jerry_length_t byteOffset, arrayBufferLen;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(typedArray, &byteOffset, &arrayBufferLen);
	u8 *gfxData = jerry_get_arraybuffer_pointer(arrayBuffer);
	jerry_release_value(arrayBuffer);
	jerry_release_value(typedArray);

	oamSetGfx(
		engine,
		getID(thisValue),
		size,
		bpp == 4 ? SpriteColorFormat_16Color : bpp == 8 ? SpriteColorFormat_256Color : SpriteColorFormat_Bmp,
		gfxData
	);

	setInternal(thisValue, "gfx", args[0]);
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_gfx) {
	NOT_REMOVED(thisValue);
	return getInternal(thisValue, "gfx");
}

FUNCTION(Sprite_set_palette) {
	NOT_REMOVED(thisValue);
	int palette = jerry_value_as_int32(args[0]);
	SPRITE_ENTRY(thisValue)->palette = BOUND(palette, 0, 15);
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_palette) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(SPRITE_ENTRY(thisValue)->palette);
}

FUNCTION(Sprite_set_priority) {
	NOT_REMOVED(thisValue);
	int priority = jerry_value_as_int32(args[0]);
	oamSetPriority(SPRITE_ENGINE(thisValue), getID(thisValue), BOUND(priority, 0, 3));
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_priority) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(SPRITE_ENTRY(thisValue)->priority);
}

FUNCTION(Sprite_set_hidden) {
	NOT_REMOVED(thisValue);
	SpriteEntry *sprite = SPRITE_ENTRY(thisValue);
	if (jerry_get_boolean_value(args[0])) { // hide
		if (sprite->isRotateScale) {
			// detach affine index so the sprite can be hidden
			sprite->isRotateScale = false;
			sprite->isSizeDouble = false;
		}
		sprite->isHidden = true;
	}
	else if (!sprite->isRotateScale) { // unhide (if isRotateScale is true, then it is already visible)
		sprite->isHidden = false;
		jerry_value_t affineObj = getInternal(thisValue, "affine");
		if (!jerry_value_is_null(affineObj)) {
			// reattach affine index and reset sizeDouble value
			sprite->rotationIndex = getID(affineObj);
			sprite->isSizeDouble = testInternal(thisValue, "sizeDouble");
			sprite->isRotateScale = true;
		}
		jerry_release_value(affineObj);
	}
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_hidden) {
	NOT_REMOVED(thisValue);
	SpriteEntry *sprite = SPRITE_ENTRY(thisValue);
	return jerry_create_boolean(sprite->isHidden && !sprite->isRotateScale);
}

FUNCTION(Sprite_set_flipH) {
	NOT_REMOVED(thisValue);
	bool set = jerry_get_boolean_value(args[0]);
	setInternal(thisValue, "flipH", jerry_create_boolean(set));
	SpriteEntry *sprite = SPRITE_ENTRY(thisValue);
	if (!sprite->isRotateScale) sprite->hFlip = set;
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_flipH) {
	NOT_REMOVED(thisValue);
	return getInternal(thisValue, "flipH");
}

FUNCTION(Sprite_set_flipV) {
	NOT_REMOVED(thisValue);
	bool set = jerry_get_boolean_value(args[0]);
	setInternal(thisValue, "flipV", jerry_create_boolean(set));
	SpriteEntry *sprite = SPRITE_ENTRY(thisValue);
	if (!sprite->isRotateScale) sprite->vFlip = set;
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_flipV) {
	NOT_REMOVED(thisValue);
	return getInternal(thisValue, "flipV");
}

FUNCTION(Sprite_set_affine) {
	NOT_REMOVED(thisValue);
	OamState *engine = SPRITE_ENGINE(thisValue);
	SpriteEntry *sprite = engine->oamMemory + getID(thisValue);
	if (jerry_value_is_null(args[0])) {
		if (sprite->isRotateScale) {
			sprite->isRotateScale = false;
			sprite->isSizeDouble = false;
		}
		sprite->hFlip = testInternal(thisValue, "flipH");
		sprite->vFlip = testInternal(thisValue, "flipV");
	}
	else {
		EXPECT(isInstance(args[0], ref_SpriteAffineMatrix), SpriteAffineMatrix);
		NOT_REMOVED(args[0]);
		if (SPRITE_ENGINE(args[0]) != engine) return TypeError("Given SpriteAffineMatrix was from the wrong engine.");
		if (sprite->isRotateScale || !sprite->isHidden) {
			sprite->rotationIndex = getID(args[0]);
			sprite->isSizeDouble = testInternal(thisValue, "sizeDouble");
			sprite->isRotateScale = true;
		}
	}
	setInternal(thisValue, "affine", args[0]);
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_affine) {
	NOT_REMOVED(thisValue);
	return getInternal(thisValue, "affine");
}

FUNCTION(Sprite_set_sizeDouble) {
	NOT_REMOVED(thisValue);
	bool set = jerry_get_boolean_value(args[0]);
	setInternal(thisValue, "sizeDouble", jerry_create_boolean(set));
	SpriteEntry *sprite = SPRITE_ENTRY(thisValue);
	if (sprite->isRotateScale) sprite->isSizeDouble = set;
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_sizeDouble) {
	NOT_REMOVED(thisValue);
	return getInternal(thisValue, "sizeDouble");
}

FUNCTION(Sprite_set_mosaic) {
	NOT_REMOVED(thisValue);
	SPRITE_ENTRY(thisValue)->isMosaic = jerry_get_boolean_value(args[0]);
	return JS_UNDEFINED;
}
FUNCTION(Sprite_get_mosaic) {
	NOT_REMOVED(thisValue);
	return jerry_create_boolean(SPRITE_ENTRY(thisValue)->isMosaic);
}

FUNCTION(Sprite_remove) {
	NOT_REMOVED(thisValue);
	OamState *engine = SPRITE_ENGINE(thisValue);
	int id = getID(thisValue);
	oamClearSprite(engine, id);
	u8 usageMask = (engine == &oamMain ? USAGE_SPRITE_MAIN : USAGE_SPRITE_SUB);
	spriteUsage[id] ^= ~usageMask;
	jerry_set_internal_property(thisValue, ref_str_removed, JS_TRUE);
	return JS_UNDEFINED;
}

FUNCTION(SpriteGraphic_get_width) {
	NOT_REMOVED(thisValue);
	jerry_value_t sizeNum = getInternal(thisValue, "size");
	SpriteSize size = (SpriteSize) jerry_value_as_uint32(sizeNum);
	jerry_release_value(sizeNum);
	if (size == SpriteSize_8x8 || size == SpriteSize_8x16 || size == SpriteSize_8x32) return jerry_create_number(8);
	if (size == SpriteSize_16x8 || size == SpriteSize_16x16 || size == SpriteSize_16x32) return jerry_create_number(16);
	if (size == SpriteSize_32x8 || size == SpriteSize_32x16 || size == SpriteSize_32x32 || size == SpriteSize_32x64) return jerry_create_number(32);
	if (size == SpriteSize_64x32 || size == SpriteSize_64x64) return jerry_create_number(64);
	return jerry_create_number_nan();
}
FUNCTION(SpriteGraphic_get_height) {
	NOT_REMOVED(thisValue);
	jerry_value_t sizeNum = getInternal(thisValue, "size");
	SpriteSize size = (SpriteSize) jerry_value_as_uint32(sizeNum);
	jerry_release_value(sizeNum);
	if (size == SpriteSize_8x8 || size == SpriteSize_16x8 || size == SpriteSize_32x8) return jerry_create_number(8);
	if (size == SpriteSize_8x16 || size == SpriteSize_16x16 || size == SpriteSize_32x16) return jerry_create_number(16);
	if (size == SpriteSize_8x32 || size == SpriteSize_16x32 || size == SpriteSize_32x32 || size == SpriteSize_64x32) return jerry_create_number(32);
	if (size == SpriteSize_32x64 || size == SpriteSize_64x64) return jerry_create_number(64);
	return jerry_create_number_nan();
}

FUNCTION(SpriteGraphic_remove) {
	NOT_REMOVED(thisValue);
	jerry_value_t typedArray = getInternal(thisValue, "data");
	jerry_length_t byteOffset, byteLength;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(typedArray, &byteOffset, &byteLength);
	u8 *gfxData = jerry_get_arraybuffer_pointer(arrayBuffer);
	jerry_release_value(arrayBuffer);
	jerry_release_value(typedArray);
	oamFreeGfx(SPRITE_ENGINE(thisValue), gfxData);
	jerry_set_internal_property(thisValue, ref_str_removed, JS_TRUE);
	return JS_UNDEFINED;
}

FUNCTION(SpriteAffineMatrix_set_hdx) {
	NOT_REMOVED(thisValue);
	SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].hdx = floatToFixed(jerry_get_number_value(args[0]), 8);
	return JS_UNDEFINED;
}
FUNCTION(SpriteAffineMatrix_get_hdx) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(fixedToFloat(SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].hdx, 8));
}
FUNCTION(SpriteAffineMatrix_set_hdy) {
	NOT_REMOVED(thisValue);
	SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].hdy = floatToFixed(jerry_get_number_value(args[0]), 8);
	return JS_UNDEFINED;
}
FUNCTION(SpriteAffineMatrix_get_hdy) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(fixedToFloat(SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].hdy, 8));
}
FUNCTION(SpriteAffineMatrix_set_vdx) {
	NOT_REMOVED(thisValue);
	SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].vdx = floatToFixed(jerry_get_number_value(args[0]), 8);
	return JS_UNDEFINED;
}
FUNCTION(SpriteAffineMatrix_get_vdx) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(fixedToFloat(SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].vdx, 8));
}
FUNCTION(SpriteAffineMatrix_set_vdy) {
	NOT_REMOVED(thisValue);
	SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].vdy = floatToFixed(jerry_get_number_value(args[0]), 8);
	return JS_UNDEFINED;
}
FUNCTION(SpriteAffineMatrix_get_vdy) {
	NOT_REMOVED(thisValue);
	return jerry_create_number(fixedToFloat(SPRITE_ENGINE(thisValue)->oamRotationMemory[getID(thisValue)].vdy, 8));
}

FUNCTION(SpriteAffineMatrix_rotateScale) {
	NOT_REMOVED(thisValue);
	REQUIRE(3);
	int angle = degreesToAngle(jerry_value_as_int32(args[0]));
	int sx = floatToFixed(jerry_get_number_value(args[1]), 8);
	int sy = floatToFixed(jerry_get_number_value(args[2]), 8);
	oamRotateScale(SPRITE_ENGINE(thisValue), getID(thisValue), angle, sx, sy);
	return JS_UNDEFINED;
}

FUNCTION(SpriteAffineMatrix_remove) {
	NOT_REMOVED(thisValue);
	u8 usageMask = (testInternal(thisValue, ref_str_main) ? USAGE_MATRIX_MAIN : USAGE_MATRIX_SUB);
	spriteUsage[getID(thisValue)] ^= ~usageMask;
	jerry_set_internal_property(thisValue, ref_str_removed, JS_TRUE);
	return JS_UNDEFINED;
}

FUNCTION(SpriteEngine_init) {
	SpriteMapping mapping;
	bool allowBitmaps = argCount > 0 && jerry_get_boolean_value(args[0]);
	bool use2DMapping = argCount > 1 && jerry_get_boolean_value(args[1]);
	int boundarySizeSet = argCount > 2;
	int boundarySize = boundarySizeSet ? jerry_value_as_int32(args[2]) : 0;
	bool useExternalPalettes = argCount > 3 && jerry_get_boolean_value(args[3]);
	if (allowBitmaps) {
		if (!boundarySizeSet || boundarySize == 128) mapping = use2DMapping ? SpriteMapping_Bmp_2D_128 : SpriteMapping_Bmp_1D_128;
		else if (boundarySize == 256) mapping = use2DMapping ? SpriteMapping_Bmp_2D_256 : SpriteMapping_Bmp_1D_256;
		else return TypeError("Boundary size for bitmap sprites should be 128 or 256.");
	}
	else if (use2DMapping) {
		if (!boundarySizeSet || boundarySize == 32) mapping = SpriteMapping_2D;
		else return TypeError("Boundary size for 2D sprite tiles should be 32.");
	}
	else if (!boundarySizeSet || boundarySize == 32) mapping = SpriteMapping_1D_32;
	else if (boundarySize == 64) mapping = SpriteMapping_1D_64;
	else if (boundarySize == 128) mapping = SpriteMapping_1D_128;
	else if (boundarySize == 256) mapping = SpriteMapping_1D_256;
	else return TypeError("Boundary size for 1D sprite tiles should be 32, 64, 128, or 256.");
	bool isMain = testInternal(thisValue, ref_str_main);
	oamInit(isMain ? &oamMain : &oamSub, mapping, useExternalPalettes);
	if (isMain) spriteUpdateMain = true;
	else spriteUpdateSub = true;
	return JS_UNDEFINED;
}

FUNCTION(SpriteEngine_enable) {
	bool isMain = testInternal(thisValue, ref_str_main);
	oamEnable(isMain ? &oamMain : &oamSub);
	if (isMain) spriteUpdateMain = true;
	else spriteUpdateSub = true;
	return JS_UNDEFINED;
}
FUNCTION(SpriteEngine_disable) {
	bool isMain = testInternal(thisValue, ref_str_main);
	oamDisable(isMain ? &oamMain : &oamSub);
	if (isMain) spriteUpdateMain = false;
	else spriteUpdateSub = false;
	return JS_UNDEFINED;
}

FUNCTION(SpriteEngine_addSprite) {
	REQUIRE(3);
	EXPECT(isInstance(args[2], ref_SpriteGraphic), SpriteGraphic);
	NOT_REMOVED(args[2]);
	OamState *engine = SPRITE_ENGINE(thisValue);
	if (SPRITE_ENGINE(args[2]) != engine) return TypeError("Given SpriteGraphic was from the wrong engine.");
	bool setsAffine = argCount > 8 && !jerry_value_is_undefined(args[8]) && !jerry_value_is_null(args[8]);
	if (setsAffine) {
		EXPECT(isInstance(args[8], ref_SpriteAffineMatrix), SpriteAffineMatrix);
		NOT_REMOVED(args[8]);
		if (SPRITE_ENGINE(args[8]) != engine) return TypeError("Given SpriteAffineMatrix was from the wrong engine.");
	}

	int id = -1;
	u8 usageMask = engine == &oamMain ? USAGE_SPRITE_MAIN : USAGE_SPRITE_SUB;
	for (int i = 0; i < SPRITE_COUNT; i++) {
		if ((spriteUsage[i] & usageMask) == 0) {
			spriteUsage[i] |= usageMask;
			id = i;
			break;
		}
	}
	if (id == -1) return Error("Out of sprite slots.");

	jerry_value_t sizeNum = getInternal(args[2], "size");
	jerry_value_t bppNum = getInternal(args[2], "colorFormat");
	SpriteSize size = (SpriteSize) jerry_value_as_uint32(sizeNum);
	int bpp = jerry_value_as_int32(bppNum);
	jerry_release_value(sizeNum);
	jerry_release_value(bppNum);
	jerry_value_t typedArray = getInternal(args[2], "data");
	jerry_length_t byteOffset, arrayBufferLen;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(typedArray, &byteOffset, &arrayBufferLen);
	u8 *gfxData = jerry_get_arraybuffer_pointer(arrayBuffer);
	jerry_release_value(arrayBuffer);
	jerry_release_value(typedArray);

	int x = jerry_value_as_int32(args[0]);
	int y = jerry_value_as_int32(args[1]);
	int paletteOrAlpha = argCount > 3 ? jerry_value_as_int32(args[3]) : 0;
	int priority = argCount > 4 ? jerry_value_as_int32(args[4]) : 0;
	bool hide = argCount > 5 && jerry_get_boolean_value(args[5]);
	bool flipH = !setsAffine && argCount > 6 && jerry_get_boolean_value(args[6]);
	bool flipV = !setsAffine && argCount > 7 && jerry_get_boolean_value(args[7]);
	int affineIndex = setsAffine ? getID(args[8]) : -1;
	bool sizeDouble = argCount > 9 && jerry_get_boolean_value(args[9]);
	bool mosaic = argCount > 10 && jerry_get_boolean_value(args[10]);

	oamSet(
		engine, id, x, y,
		BOUND(priority, 0, 3),
		BOUND(paletteOrAlpha, 0, 15),
		size,
		bpp == 4 ? SpriteColorFormat_16Color : bpp == 8 ? SpriteColorFormat_256Color : SpriteColorFormat_Bmp,
		gfxData,
		hide ? -1 : affineIndex,
		sizeDouble, false, flipH, flipV, mosaic
	);
	// set hidden flag manually, because oamSet ignores the other parameters if hide is true.
	if (hide) engine->oamMemory[id].isHidden = true;

	jerry_value_t spriteObj = jerry_create_object();
	setInternal(spriteObj, "id", (double) id);
	defReadonly(spriteObj, ref_str_main, jerry_get_internal_property(thisValue, ref_str_main));
	setPrototype(spriteObj, bpp == 16 ? ref_BitmapSprite.prototype : ref_PalettedSprite.prototype);
	setInternal(spriteObj, "flipH", JS_FALSE);
	setInternal(spriteObj, "flipV", JS_FALSE);
	setInternal(spriteObj, "affine", setsAffine ? args[8] : JS_NULL);
	setInternal(spriteObj, "sizeDouble", jerry_create_boolean(sizeDouble));
	return spriteObj;
}

FUNCTION(SpriteEngine_addGraphic) {
	REQUIRE(3);
	if (argCount > 3) EXPECT(jerry_get_typedarray_type(args[3]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	int width = jerry_value_as_int32(args[0]);
	int height = jerry_value_as_int32(args[1]);
	int bpp = jerry_value_as_int32(args[2]);

	SpriteSize size;
	if (width > 64) return TypeError("Sprite width is above the limit of 64.");
	if (height > 64) return TypeError("Sprite height is above the limit of 64.");
	if (width <= 8) {
		if (height <= 8) size = SpriteSize_8x8;
		else if (height <= 16) size = SpriteSize_8x16;
		else if (height <= 32) size = SpriteSize_8x32;
		else size = SpriteSize_32x64;
	}
	else if (width <= 16) {
		if (height <= 8) size = SpriteSize_16x8;
		else if (height <= 16) size = SpriteSize_16x16;
		else if (height <= 32) size = SpriteSize_16x32;
		else size = SpriteSize_32x64;
	}
	else if (width <= 32) {
		if (height <= 8) size = SpriteSize_32x8;
		else if (height <= 16) size = SpriteSize_32x16;
		else if (height <= 32) size = SpriteSize_32x32;
		else size = SpriteSize_32x64;
	}
	else if (height <= 32) size = SpriteSize_64x32;
	else size = SpriteSize_64x64;

	SpriteColorFormat format;
	if (bpp == 4) format = SpriteColorFormat_16Color;
	else if (bpp == 8) format = SpriteColorFormat_256Color;
	else if (bpp == 16) format = SpriteColorFormat_Bmp;
	else return TypeError("Expected a bits-per-pixel value of either 4, 8, or 16.");
	
	OamState *engine = SPRITE_ENGINE(thisValue);
	u16 *gfxData = oamAllocateGfx(engine, size, format);
	if (engine->firstFree == -1) return Error("Out of sprite graphics memory.");
	u32 byteSize = SPRITE_SIZE_PIXELS(size);
	if (bpp == 4) byteSize /= 2;
	else if (bpp == 16) byteSize *= 2;

	if (argCount > 3) {
		jerry_length_t byteOffset, inputArrayBufferLen;
		jerry_value_t inputArrayBuffer = jerry_get_typedarray_buffer(args[3], &byteOffset, &inputArrayBufferLen);
		jerry_arraybuffer_read(inputArrayBuffer, byteOffset, (u8 *) gfxData, byteSize < inputArrayBufferLen ? byteSize : inputArrayBufferLen);
		jerry_release_value(inputArrayBuffer);
	}

	jerry_value_t arrayBuffer = jerry_create_arraybuffer_external(byteSize, (u8 *) gfxData, [](void * _){});
	jerry_value_t typedArray = jerry_create_typedarray_for_arraybuffer(JERRY_TYPEDARRAY_UINT8, arrayBuffer);
	jerry_release_value(arrayBuffer);
	
	jerry_value_t spriteGraphicObj = jerry_create_object();
	defReadonly(spriteGraphicObj, ref_str_main, jerry_get_internal_property(thisValue, ref_str_main));
	setPrototype(spriteGraphicObj, ref_SpriteGraphic.prototype);
	defReadonly(spriteGraphicObj, "colorFormat", args[2]);
	setInternal(spriteGraphicObj, "size", (double) size);
	defReadonly(spriteGraphicObj, "data", typedArray);
	jerry_release_value(typedArray);
	return spriteGraphicObj;
}

FUNCTION(SpriteEngine_addAffineMatrix) {
	OamState *engine = SPRITE_ENGINE(thisValue);

	int id = -1;
	u8 usageMask = engine == &oamMain ? USAGE_MATRIX_MAIN : USAGE_MATRIX_SUB;
	for (int i = 0; i < MATRIX_COUNT; i++) {
		if ((spriteUsage[i] & usageMask) == 0) {
			spriteUsage[i] |= usageMask;
			id = i;
			break;
		}
	}
	if (id == -1) return Error("Out of affine matrix slots.");

	int hdx = argCount > 0 ? floatToFixed(jerry_get_number_value(args[0]), 8) : (1 << 8);
	int hdy = argCount > 1 ? floatToFixed(jerry_get_number_value(args[1]), 8) : 0;
	int vdx = argCount > 2 ? floatToFixed(jerry_get_number_value(args[2]), 8) : 0;
	int vdy = argCount > 3 ? floatToFixed(jerry_get_number_value(args[3]), 8) : (1 << 8);
	oamAffineTransformation(engine, id, hdx, hdy, vdx, vdy);
	
	jerry_value_t affineObj = jerry_create_object();
	setInternal(affineObj, "id", (double) id);
	defReadonly(affineObj, ref_str_main, jerry_get_internal_property(thisValue, ref_str_main));
	setPrototype(affineObj, ref_SpriteAffineMatrix.prototype);
	return affineObj;
}

FUNCTION(SpriteEngine_setMosaic) {
	REQUIRE(2);
	bool isMain = testInternal(thisValue, ref_str_main);
	int dx = jerry_value_as_uint32(args[0]);
	int dy = jerry_value_as_uint32(args[1]);
	(isMain ? oamSetMosaic : oamSetMosaicSub)(BOUND(dx, 0, 15), BOUND(dy, 0, 15));
	return JS_UNDEFINED;
}

void exposeSpriteAPI(jerry_value_t global) {
	ref_str_main = String("main");
	ref_str_removed = String("removed");

	JS_class Sprite = createClass(global, "Sprite", IllegalConstructor);
	defGetterSetter(Sprite.prototype, "x", Sprite_get_x, Sprite_set_x);
	defGetterSetter(Sprite.prototype, "y", Sprite_get_y, Sprite_set_y);
	setMethod(Sprite.prototype, "setPosition", Sprite_setPosition);
	defGetterSetter(Sprite.prototype, "gfx", Sprite_get_gfx, Sprite_set_gfx);
	defGetterSetter(Sprite.prototype, "priority", Sprite_get_priority, Sprite_set_priority);
	defGetterSetter(Sprite.prototype, "hidden", Sprite_get_hidden, Sprite_set_hidden);
	defGetterSetter(Sprite.prototype, "flipH", Sprite_get_flipH, Sprite_set_flipH);
	defGetterSetter(Sprite.prototype, "flipV", Sprite_get_flipV, Sprite_set_flipV);
	defGetterSetter(Sprite.prototype, "affine", Sprite_get_affine, Sprite_set_affine);
	defGetterSetter(Sprite.prototype, "sizeDouble", Sprite_get_sizeDouble, Sprite_set_sizeDouble);
	defGetterSetter(Sprite.prototype, "mosaic", Sprite_get_mosaic, Sprite_set_mosaic);
	setMethod(Sprite.prototype, "remove", Sprite_remove);
	JS_class PalettedSprite = extendClass(global, "PalettedSprite", IllegalConstructor, Sprite.prototype);
	defGetterSetter(PalettedSprite.prototype, "palette", Sprite_get_palette, Sprite_set_palette);
	ref_PalettedSprite = PalettedSprite;
	JS_class BitmapSprite = extendClass(global, "BitmapSprite", IllegalConstructor, Sprite.prototype);
	defGetterSetter(BitmapSprite.prototype, "alpha", Sprite_get_palette, Sprite_set_palette);
	ref_BitmapSprite = BitmapSprite;
	jerry_value_t SpriteEngine = jerry_create_object();
	setMethod(SpriteEngine, "init", SpriteEngine_init);
	setMethod(SpriteEngine, "enable", SpriteEngine_enable);
	setMethod(SpriteEngine, "disable", SpriteEngine_disable);
	setMethod(SpriteEngine, "addSprite", SpriteEngine_addSprite);
	setMethod(SpriteEngine, "addGraphic", SpriteEngine_addGraphic);
	setMethod(SpriteEngine, "addAffineMatrix", SpriteEngine_addAffineMatrix);
	setMethod(SpriteEngine, "setMosaic", SpriteEngine_setMosaic);
	jerry_value_t main = createObject(Sprite.constructor, "main");
	jerry_set_internal_property(main, ref_str_main, JS_TRUE);
	jerry_value_t mainSpritePaletteArrayBuffer = jerry_create_arraybuffer_external(256 * sizeof(u16), (u8*) SPRITE_PALETTE, [](void * _){});
	jerry_value_t mainSpritePaletteTypedArray = jerry_create_typedarray_for_arraybuffer_sz(JERRY_TYPEDARRAY_UINT16, mainSpritePaletteArrayBuffer, 0, 256);
	defReadonly(main, "palette", mainSpritePaletteTypedArray);
	jerry_release_value(mainSpritePaletteTypedArray);
	jerry_release_value(mainSpritePaletteArrayBuffer);
	setPrototype(main, SpriteEngine);
	jerry_release_value(main);
	jerry_value_t sub = createObject(Sprite.constructor, "sub");
	jerry_set_internal_property(sub, ref_str_main, JS_FALSE);
	jerry_value_t subSpritePaletteArrayBuffer = jerry_create_arraybuffer_external(256 * sizeof(u16), (u8*) SPRITE_PALETTE_SUB, [](void * _){});
	jerry_value_t subSpritePaletteTypedArray = jerry_create_typedarray_for_arraybuffer_sz(JERRY_TYPEDARRAY_UINT16, subSpritePaletteArrayBuffer, 0, 256);
	defReadonly(sub, "palette", subSpritePaletteTypedArray);
	jerry_release_value(subSpritePaletteTypedArray);
	jerry_release_value(subSpritePaletteArrayBuffer);
	setPrototype(sub, SpriteEngine);
	jerry_release_value(sub);
	jerry_release_value(SpriteEngine);
	ref_Sprite = Sprite;

	JS_class SpriteGraphic = createClass(global, "SpriteGraphic", IllegalConstructor);
	defGetter(SpriteGraphic.prototype, "width", SpriteGraphic_get_width);
	defGetter(SpriteGraphic.prototype, "height", SpriteGraphic_get_height);
	setMethod(SpriteGraphic.prototype, "remove", SpriteGraphic_remove);
	ref_SpriteGraphic = SpriteGraphic;

	JS_class SpriteAffineMatrix = createClass(global, "SpriteAffineMatrix", IllegalConstructor);
	defGetterSetter(SpriteAffineMatrix.prototype, "hdx", SpriteAffineMatrix_get_hdx, SpriteAffineMatrix_set_hdx);
	defGetterSetter(SpriteAffineMatrix.prototype, "hdy", SpriteAffineMatrix_get_hdy, SpriteAffineMatrix_set_hdy);
	defGetterSetter(SpriteAffineMatrix.prototype, "vdx", SpriteAffineMatrix_get_vdx, SpriteAffineMatrix_set_vdx);
	defGetterSetter(SpriteAffineMatrix.prototype, "vdy", SpriteAffineMatrix_get_vdy, SpriteAffineMatrix_set_vdy);
	setMethod(SpriteAffineMatrix.prototype, "rotateScale", SpriteAffineMatrix_rotateScale);
	setMethod(SpriteAffineMatrix.prototype, "remove", SpriteAffineMatrix_remove);
	ref_SpriteAffineMatrix = SpriteAffineMatrix;
}

void releaseSpriteReferences() {
	jerry_release_value(ref_str_main);
	jerry_release_value(ref_str_removed);
	releaseClass(ref_Sprite);
	releaseClass(ref_PalettedSprite);
	releaseClass(ref_BitmapSprite);
	releaseClass(ref_SpriteGraphic);
	releaseClass(ref_SpriteAffineMatrix);
}