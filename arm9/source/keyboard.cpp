#include "keyboard.hpp"

#include <nds/arm9/background.h>
#include <nds/arm9/input.h>
#include <string.h>

#include "event.hpp"
#include "input.hpp"



const u8 KEYBOARD_HEIGHT = 80;
const u8 KEY_HEIGHT = 15;
const u8 TEXT_HEIGHT = 16;
static u16 gfxBuffer[SCREEN_WIDTH * KEYBOARD_HEIGHT] = {0};

const u16 COLOR_KEYBOARD_BACKDROP = 0xDAD6;//0xF7BD;
const u16 COLOR_KEY_NORMAL = 0xFFFF;//0xEB5A;

struct KeyDef {
	char code[13];
	char lower[13];
	char upper[13];
	u8 x;
	u8 y;
	u8 width;
};

const u8 KEY_CNT_MODE_SELECT = 5;
const KeyDef boardModeSelect[KEY_CNT_MODE_SELECT] = {
	{"AlphNum", "", "", 1, 1, 15},
	{"LatinAc", "", "", 1, 17, 15},
	{"Kana", "", "", 1, 33, 15},
	{"Punct", "", "", 1, 49, 15},
	{"Picture", "", "", 1, 65, 15}
};

const u8 KEY_CNT_ALPHANUMERIC = 53;
const KeyDef boardAlphanumeric[KEY_CNT_ALPHANUMERIC] = {
	{"Backquote", "`", "~", 17, 1, 15},
	{"Digit1", "1", "!", 33, 1, 15},
	{"Digit2", "2", "@", 49, 1, 15},
	{"Digit3", "3", "#", 65, 1, 15},
	{"Digit4", "4", "$", 81, 1, 15},
	{"Digit5", "5", "%", 97, 1, 15},
	{"Digit6", "6", "^", 113, 1, 15},
	{"Digit7", "7", "&", 129, 1, 15},
	{"Digit8", "8", "*", 145, 1, 15},
	{"Digit9", "9", "(", 161, 1, 15},
	{"Digit0", "0", ")", 177, 1, 15},
	{"Minus", "-", "_", 193, 1, 15},
	{"Equal", "=", "+", 209, 1, 15},
	{"Backspace", "Backspace", "Backspace", 225, 1, 30},
	{"Tab", "Tab", "Tab", 17, 17, 23},
	{"KeyQ", "q", "Q", 41, 17, 15},
	{"KeyW", "w", "W", 57, 17, 15},
	{"KeyE", "e", "E", 73, 17, 15},
	{"KeyR", "r", "R", 89, 17, 15},
	{"KeyT", "t", "T", 105, 17, 15},
	{"KeyY", "y", "Y", 121, 17, 15},
	{"KeyU", "u", "U", 137, 17, 15},
	{"KeyI", "i", "I", 153, 17, 15},
	{"KeyO", "o", "O", 169, 17, 15},
	{"KeyP", "p", "P", 185, 17, 15},
	{"BracketLeft", "[", "{", 201, 17, 15},
	{"BracketRight", "]", "}", 217, 17, 15},
	{"Backslash", "\\", "|", 233, 17, 22},
	{"CapsLock", "CapsLock", "CapsLock", 17, 33, 27},
	{"KeyA", "a", "A", 45, 33, 15},
	{"KeyS", "s", "S", 61, 33, 15},
	{"KeyD", "d", "D", 77, 33, 15},
	{"KeyF", "f", "F", 93, 33, 15},
	{"KeyG", "g", "G", 109, 33, 15},
	{"KeyH", "h", "H", 125, 33, 15},
	{"KeyJ", "j", "J", 141, 33, 15},
	{"KeyK", "k", "K", 157, 33, 15},
	{"KeyL", "l", "L", 173, 33, 15},
	{"Semicolon", ";", ":", 189, 33, 15},
	{"Quote", "'", "\"", 205, 33, 15},
	{"Enter", "Enter", "Enter", 221, 33, 34},
	{"ShiftLeft", "Shift", "Shift", 17, 49, 35},
	{"KeyZ", "z", "Z", 53, 49, 15},
	{"KeyX", "x", "X", 69, 49, 15},
	{"KeyC", "c", "C", 85, 49, 15},
	{"KeyV", "v", "V", 101, 49, 15},
	{"KeyB", "b", "B", 117, 49, 15},
	{"KeyN", "n", "N", 133, 49, 15},
	{"KeyM", "m", "M", 149, 49, 15},
	{"Comma", ",", "<", 165, 49, 15},
	{"Period", ".", ">", 181, 49, 15},
	{"Slash", "/", "?", 197, 49, 15},
	{"Space", " ", " ", 81, 65, 95},
};

const u8 KEY_CNT_LATIN_ACCENTED = 55;
const KeyDef boardLatinAccented[KEY_CNT_LATIN_ACCENTED] = {
	{"Keyà", "à", "", 33, 1, 15},
	{"Keyá", "á", "", 49, 1, 15},
	{"Keyâ", "â", "", 65, 1, 15},
	{"Keyä", "ä", "", 81, 1, 15},
	{"Keyè", "è", "", 97, 1, 15},
	{"Keyé", "é", "", 113, 1, 15},
	{"Keyê", "ê", "", 129, 1, 15},
	{"Keyë", "ë", "", 145, 1, 15},
	{"Keyì", "ì", "", 161, 1, 15},
	{"Keyí", "í", "", 177, 1, 15},
	{"Keyî", "î", "", 193, 1, 15},
	{"Keyï", "ï", "", 209, 1, 15},
	{"Keyò", "ò", "", 33, 17, 15},
	{"Keyó", "ó", "", 49, 17, 15},
	{"Keyô", "ô", "", 65, 17, 15},
	{"Keyö", "ö", "", 81, 17, 15},
	{"Keyœ", "œ", "", 97, 17, 15},
	{"Keyù", "ù", "", 113, 17, 15},
	{"Keyú", "ú", "", 129, 17, 15},
	{"Keyû", "û", "", 145, 17, 15},
	{"Keyü", "ü", "", 161, 17, 15},
	{"Keyç", "ç", "", 177, 17, 15},
	{"Keyñ", "ñ", "", 193, 17, 15},
	{"Keyß", "ß", "", 209, 17, 15},
	{"Backspace", "Backspace", "", 225, 17, 30},
	{"KeyÀ", "À", "", 33, 33, 15},
	{"KeyÁ", "Á", "", 49, 33, 15},
	{"KeyÂ", "Â", "", 65, 33, 15},
	{"KeyÄ", "Ä", "", 81, 33, 15},
	{"KeyÈ", "È", "", 97, 33, 15},
	{"KeyÉ", "É", "", 113, 33, 15},
	{"KeyÊ", "Ê", "", 129, 33, 15},
	{"KeyË", "Ë", "", 145, 33, 15},
	{"KeyÌ", "Ì", "", 161, 33, 15},
	{"KeyÍ", "Í", "", 177, 33, 15},
	{"KeyÎ", "Î", "", 193, 33, 15},
	{"KeyÏ", "Ï", "", 209, 33, 15},
	{"Enter", "Enter", "", 225, 33, 30},
	{"KeyÒ", "Ò", "", 33, 49, 15},
	{"KeyÓ", "Ó", "", 49, 49, 15},
	{"KeyÔ", "Ô", "", 65, 49, 15},
	{"KeyÖ", "Ö", "", 81, 49, 15},
	{"KeyŒ", "Œ", "", 97, 49, 15},
	{"KeyÙ", "Ù", "", 113, 49, 15},
	{"KeyÚ", "Ú", "", 129, 49, 15},
	{"KeyÛ", "Û", "", 145, 49, 15},
	{"KeyÜ", "Ü", "", 161, 49, 15},
	{"KeyÇ", "Ç", "", 177, 49, 15},
	{"KeyÑ", "Ñ", "", 193, 49, 15},
	{"Key¡", "¡", "", 33, 65, 15},
	{"Key¿", "¿", "", 49, 65, 15},
	{"Key€", "€", "", 65, 65, 15},
	{"Key¢", "¢", "", 81, 65, 15},
	{"Key£", "£", "", 97, 65, 15},
	{"Space", "Space", "", 209, 65, 46},
};

const u8 KEY_CNT_KANA = 59;
const KeyDef boardKana[KEY_CNT_KANA] = {
	{"HiriganaMode", "HiraganaMode", "HiraganaMode", 17, 1, 31},
	{"Keyあ", "あ", "ア", 49, 1, 15},
	{"Keyか", "か", "カ", 65, 1, 15},
	{"Keyさ", "さ", "サ", 81, 1, 15},
	{"Keyた", "た", "タ", 97, 1, 15},
	{"Keyな", "な", "ナ", 113, 1, 15},
	{"Keyは", "は", "ハ", 129, 1, 15},
	{"Keyま", "ま", "マ", 145, 1, 15},
	{"Keyや", "や", "ヤ", 161, 1, 15},
	{"Keyら", "ら", "ラ", 177, 1, 15},
	{"Keyわ", "わ", "ワ", 193, 1, 15},
	{"Keyー", "ー", "ー", 209, 1, 15},
	{"KatakanaMode", "KatakanaMode", "KatakanaMode", 17, 17, 31},
	{"Keyい", "い", "イ", 49, 17, 15},
	{"Keyき", "き", "キ", 65, 17, 15},
	{"Keyし", "し", "シ", 81, 17, 15},
	{"Keyち", "ち", "チ", 97, 17, 15},
	{"Keyに", "に", "ニ", 113, 17, 15},
	{"Keyひ", "ひ", "ヒ", 129, 17, 15},
	{"Keyみ", "み", "ミ", 145, 17, 15},
	{"Keyり", "り", "リ", 177, 17, 15},
	{"Key！", "！", "！", 209, 17, 15},
	{"Backspace", "Backspace", "Backspace", 225, 17, 30},
	{"Dakuten", "Dakuten", "Dakuten", 17, 33, 31},
	{"Keyう", "う", "ウ", 49, 33, 15},
	{"Keyく", "く", "ク", 65, 33, 15},
	{"Keyす", "す", "ス", 81, 33, 15},
	{"Keyつ", "つ", "ツ", 97, 33, 15},
	{"Keyぬ", "ぬ", "ヌ", 113, 33, 15},
	{"Keyふ", "ふ", "フ", 129, 33, 15},
	{"Keyむ", "む", "ム", 145, 33, 15},
	{"Keyゆ", "ゆ", "ユ", 161, 33, 15},
	{"Keyる", "る", "ル", 177, 33, 15},
	{"Keyん", "ん", "ン", 193, 33, 15},
	{"Key？", "？", "？", 209, 33, 15},
	{"Enter", "Enter", "Enter", 225, 33, 30},
	{"Handakuten", "Handakuten", "Handakuten", 17, 49, 31},
	{"Keyえ", "え", "エ", 49, 49, 15},
	{"Keyけ", "け", "ケ", 65, 49, 15},
	{"Keyせ", "せ", "セ", 81, 49, 15},
	{"Keyて", "て", "テ", 97, 49, 15},
	{"Keyね", "ね", "ネ", 113, 49, 15},
	{"Keyへ", "へ", "ヘ", 129, 49, 15},
	{"Keyめ", "め", "メ", 145, 49, 15},
	{"Keyれ", "れ", "レ", 177, 49, 15},
	{"Key、", "、", "、", 209, 49, 15},
	{"ChangeSize", "ChangeSize", "ChangeSize", 17, 65, 31},
	{"Keyお", "お", "オ", 49, 65, 15},
	{"Keyこ", "こ", "コ", 65, 65, 15},
	{"Keyそ", "そ", "ソ", 81, 65, 15},
	{"Keyと", "と", "ト", 97, 65, 15},
	{"Keyの", "の", "ノ", 113, 65, 15},
	{"Keyほ", "ほ", "ホ", 129, 65, 15},
	{"Keyも", "も", "モ", 145, 65, 15},
	{"Keyよ", "よ", "ヨ", 161, 65, 15},
	{"Keyろ", "ろ", "ロ", 177, 65, 15},
	{"Keyを", "を", "ヲ", 193, 65, 15},
	{"Key。", "。", "。", 209, 65, 15},
	{"Space", "Space", "Space", 225, 65, 30},
};

const u8 KEY_CNT_SYMBOL = 0;
const KeyDef boardSymbol[KEY_CNT_SYMBOL] = {
	
};

const u8 KEY_CNT_PICTOGRAM = 0;
const KeyDef boardPictogram[KEY_CNT_PICTOGRAM] = {
	
};

const KeyDef* currentBoard = boardAlphanumeric;
u8 currentBoardKeyCount = KEY_CNT_ALPHANUMERIC;

void keyboardInit() {
	videoSetModeSub(MODE_3_2D);
	vramSetBankC(VRAM_C_SUB_BG);
	bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);

	for (int i = 0; i < SCREEN_WIDTH * KEYBOARD_HEIGHT; i++) gfxBuffer[i] = COLOR_KEYBOARD_BACKDROP;

	for (u8 i = 0; i < KEY_CNT_MODE_SELECT; i++) {
		KeyDef key = boardModeSelect[i];
		for (u8 y = 0; y < KEY_HEIGHT && key.y + y < KEYBOARD_HEIGHT-1; y++) {
			for (u8 x = 0; x < key.width; x++) {
				gfxBuffer[key.x + x + (key.y + y) * SCREEN_WIDTH] = COLOR_KEY_NORMAL;
			}
		}
	}

	for (u8 i = 0; i < currentBoardKeyCount; i++) {
		KeyDef key = currentBoard[i];
		for (u8 y = 0; y < KEY_HEIGHT && key.y + y < KEYBOARD_HEIGHT-1; y++) {
			for (u8 x = 0; x < key.width; x++) {
				gfxBuffer[key.x + x + (key.y + y) * SCREEN_WIDTH] = COLOR_KEY_NORMAL;
			}
		}
	}
}

void keyboardUpdate() {
	if (keysDown() & KEY_TOUCH) {
		touchPosition pos;
		touchRead(&pos);
		for (u8 i = 0; i < KEY_CNT_MODE_SELECT; i++) {
			KeyDef key = boardModeSelect[i];
			if (pos.px >= key.x && pos.px < key.x + key.width
			 && pos.py >= key.y + (SCREEN_HEIGHT - KEYBOARD_HEIGHT)
			 && pos.py < key.y + (SCREEN_HEIGHT - KEYBOARD_HEIGHT) + KEY_HEIGHT
			) {
				switch (i) {
					case 0:
						currentBoard = boardAlphanumeric;
						currentBoardKeyCount = KEY_CNT_ALPHANUMERIC;
						break;
					case 1:
						currentBoard = boardLatinAccented;
						currentBoardKeyCount = KEY_CNT_LATIN_ACCENTED;
						break;
					case 2:
						currentBoard = boardKana;
						currentBoardKeyCount = KEY_CNT_KANA;
						break;
					case 3:
						currentBoard = boardSymbol;
						currentBoardKeyCount = KEY_CNT_SYMBOL;
						break;
					case 4:
						currentBoard = boardPictogram;
						currentBoardKeyCount = KEY_CNT_PICTOGRAM;
						break;
				}

				for (int i = 0; i < SCREEN_WIDTH * KEYBOARD_HEIGHT; i++) if (i % SCREEN_WIDTH > 16) gfxBuffer[i] = COLOR_KEYBOARD_BACKDROP;

				for (u8 i = 0; i < currentBoardKeyCount; i++) {
					KeyDef key = currentBoard[i];
					for (u8 y = 0; y < KEY_HEIGHT && key.y + y < KEYBOARD_HEIGHT-1; y++) {
						for (u8 x = 0; x < key.width; x++) {
							gfxBuffer[key.x + x + (key.y + y) * SCREEN_WIDTH] = COLOR_KEY_NORMAL;
						}
					}
				}
				dmaCopy(gfxBuffer, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT)), sizeof(gfxBuffer));
				return;
			}
		}

		for (u8 i = 0; i < currentBoardKeyCount; i++) {
			KeyDef key = currentBoard[i];
			if (pos.px >= key.x && pos.px < key.x + key.width
			 && pos.py >= key.y + (SCREEN_HEIGHT - KEYBOARD_HEIGHT)
			 && pos.py < key.y + (SCREEN_HEIGHT - KEYBOARD_HEIGHT) + KEY_HEIGHT
			) {
				if (dependentEvents & keydown) {
					if (dispatchKeyboardEvent(true, key.lower, key.code, 0, false, false, false, false, false)) return;
				}
				return;
			}
		}
	}
}

bool kbdIsOpen = false;
void keyboardOpen(bool printInput) {
	if (kbdIsOpen) return;
	keyboardClearBuffer();
	dmaCopy(gfxBuffer, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT)), sizeof(gfxBuffer));
	kbdIsOpen = true;
}
void keyboardClose(bool clear) {
	if (!kbdIsOpen) return;
	dmaFillHalfWords(0, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT)), sizeof(gfxBuffer));
	kbdIsOpen = false;
}
bool isKeyboardOpen() {
	return kbdIsOpen;
}

const int keyboardBufferSize = 256;
char buf[keyboardBufferSize] = {0};
int idx = 0;
bool keyboardEnterPressed = false;
bool keyboardEscapePressed = false;
bool kbdPrintInput = true;
const char *keyboardBuffer() {
	return buf;
}
u8 keyboardBufferLen() {
	return idx;
}
void keyboardClearBuffer() {
	memset(buf, 0, keyboardBufferSize);
	idx = 0;
	keyboardEnterPressed = false;
	keyboardEscapePressed = false;
}

bool shiftToggle = false, ctrlToggle = false, altToggle = false, metaToggle = false, capsToggle = false;
