#include "io/keyboard.hpp"

#include <nds/arm9/background.h>
#include <nds/arm9/cache.h>
#include <nds/arm9/input.h>
#include <stdlib.h>

#include "io/console.hpp"
#include "keyboard_nftr.h"
#include "util/font.hpp"
#include "util/tonccpy.h"
#include "util/unicode.hpp"

#define lengthof(arr) sizeof(arr)/sizeof(*arr)



const int REPEAT_START = 30;
const int REPEAT_INTERVAL = 5;

const u8 KEYBOARD_HEIGHT = 80;
const u8 KEY_HEIGHT = 15;
const u8 TEXT_HEIGHT = 16;
const u8 SPACE_BAR_WIDTH = 95;
const u8 TALL_ENTER_HEIGHT = 31;

const u16 COLOR_KEYBOARD_BACKDROP = 0xB9CE;
const u16 COLOR_COMPOSING_BACKDROP = 0xFFFF;
const u16 PALETTE_FONT_KEY[] = {0, 0, 0, 0x9CE7};
const u16 PALETTE_FONT_COMPOSITION[] = {0, 0xCA52, 0xA108, 0x8000};
const u16 PALETTE_KEY_NORMAL[] = {0xEB5A, 0xFFFF, 0xAD6B};
const u16 PALETTE_KEY_SPECIAL[] = {0xD294, 0xF7BD, 0x98C6, 0xDEF7};
const u16 PALETTE_KEY_CANCEL[] = {0xDADD, 0xF7BE, 0xAD74};
enum KEY_STATES {NEUTRAL = 0, HIGHLIGHTED, PRESSED, ACTIVE};

enum ControlKeys {
	CAPS_LOCK = 1,
	SHIFT,
	HIRAGANA,
	KATAKANA,
	VOICED,
	SEMI_VOICED,
	SIZE_CHANGE,
	BACKSPACE,
	TAB,
	ENTER,
	INPUT_ALPHANUMERIC,
	INPUT_LATIN_ACCENTED,
	INPUT_KANA,
	INPUT_SYMBOL,
	INPUT_PICTOGRAM,
	CANCEL = 0x18
};

struct KeyDef {
	char name[20];
	char16_t lower;
	char16_t upper;
	u8 x;
	u8 y;
};

const KeyDef boardAlphanumeric[] = {
	{"InputAlphaNumeric", INPUT_ALPHANUMERIC, INPUT_ALPHANUMERIC, 1, 1},
	{"InputLatinAccented", INPUT_LATIN_ACCENTED, INPUT_LATIN_ACCENTED, 1, 17},
	{"InputKana", INPUT_KANA, INPUT_KANA, 1, 33},
	{"InputSymbol", INPUT_SYMBOL, INPUT_SYMBOL, 1, 49},
	{"InputPictogram", INPUT_PICTOGRAM, INPUT_PICTOGRAM, 1, 65},
	{"Backquote", '`', '~', 17, 1},
	{"Digit1", '1', '!', 33, 1},
	{"Digit2", '2', '@', 49, 1},
	{"Digit3", '3', '#', 65, 1},
	{"Digit4", '4', '$', 81, 1},
	{"Digit5", '5', '%', 97, 1},
	{"Digit6", '6', '^', 113, 1},
	{"Digit7", '7', '&', 129, 1},
	{"Digit8", '8', '*', 145, 1},
	{"Digit9", '9', '(', 161, 1},
	{"Digit0", '0', ')', 177, 1},
	{"Minus", '-', '_', 193, 1},
	{"Equal", '=', '+', 209, 1},
	{"Backspace", '\b', '\b', 225, 1},
	{"Tab", '\t', '\t', 17, 17},
	{"KeyQ", 'q', 'Q', 41, 17},
	{"KeyW", 'w', 'W', 57, 17},
	{"KeyE", 'e', 'E', 73, 17},
	{"KeyR", 'r', 'R', 89, 17},
	{"KeyT", 't', 'T', 105, 17},
	{"KeyY", 'y', 'Y', 121, 17},
	{"KeyU", 'u', 'U', 137, 17},
	{"KeyI", 'i', 'I', 153, 17},
	{"KeyO", 'o', 'O', 169, 17},
	{"KeyP", 'p', 'P', 185, 17},
	{"BracketLeft", '[', '{', 201, 17},
	{"BracketRight", ']', '}', 217, 17},
	{"Backslash", '\\', '|', 233, 17},
	{"CapsLock", CAPS_LOCK, CAPS_LOCK, 17, 33},
	{"KeyA", 'a', 'A', 45, 33},
	{"KeyS", 's', 'S', 61, 33},
	{"KeyD", 'd', 'D', 77, 33},
	{"KeyF", 'f', 'F', 93, 33},
	{"KeyG", 'g', 'G', 109, 33},
	{"KeyH", 'h', 'H', 125, 33},
	{"KeyJ", 'j', 'J', 141, 33},
	{"KeyK", 'k', 'K', 157, 33},
	{"KeyL", 'l', 'L', 173, 33},
	{"Semicolon", ';', ':', 189, 33},
	{"Quote", '\'', '"', 205, 33},
	{"Enter", '\n', '\n', 221, 33},
	{"ShiftLeft", SHIFT, SHIFT, 17, 49},
	{"KeyZ", 'z', 'Z', 53, 49},
	{"KeyX", 'x', 'X', 69, 49},
	{"KeyC", 'c', 'C', 85, 49},
	{"KeyV", 'v', 'V', 101, 49},
	{"KeyB", 'b', 'B', 117, 49},
	{"KeyN", 'n', 'N', 133, 49},
	{"KeyM", 'm', 'M', 149, 49},
	{"Comma", ',', '<', 165, 49},
	{"Period", '.', '>', 181, 49},
	{"Slash", '/', '?', 197, 49},
	{"ShiftRight", SHIFT, SHIFT, 213, 49},
	{"Space", ' ', ' ', 81, 65},
	{"Cancel", CANCEL, CANCEL, 213, 65}
};
const KeyDef boardLatinAccented[] = {
	{"InputAlphaNumeric", INPUT_ALPHANUMERIC, INPUT_ALPHANUMERIC, 1, 1},
	{"InputLatinAccented", INPUT_LATIN_ACCENTED, INPUT_LATIN_ACCENTED, 1, 17},
	{"InputKana", INPUT_KANA, INPUT_KANA, 1, 33},
	{"InputSymbol", INPUT_SYMBOL, INPUT_SYMBOL, 1, 49},
	{"InputPictogram", INPUT_PICTOGRAM, INPUT_PICTOGRAM, 1, 65},
	{"Keyà", u'à', 0, 33, 1},
	{"Keyá", u'á', 0, 49, 1},
	{"Keyâ", u'â', 0, 65, 1},
	{"Keyä", u'ä', 0, 81, 1},
	{"Keyè", u'è', 0, 97, 1},
	{"Keyé", u'é', 0, 113, 1},
	{"Keyê", u'ê', 0, 129, 1},
	{"Keyë", u'ë', 0, 145, 1},
	{"Keyì", u'ì', 0, 161, 1},
	{"Keyí", u'í', 0, 177, 1},
	{"Keyî", u'î', 0, 193, 1},
	{"Keyï", u'ï', 0, 209, 1},
	{"Keyò", u'ò', 0, 33, 17},
	{"Keyó", u'ó', 0, 49, 17},
	{"Keyô", u'ô', 0, 65, 17},
	{"Keyö", u'ö', 0, 81, 17},
	{"Keyœ", u'œ', 0, 97, 17},
	{"Keyù", u'ù', 0, 113, 17},
	{"Keyú", u'ú', 0, 129, 17},
	{"Keyû", u'û', 0, 145, 17},
	{"Keyü", u'ü', 0, 161, 17},
	{"Keyç", u'ç', 0, 177, 17},
	{"Keyñ", u'ñ', 0, 193, 17},
	{"Keyß", u'ß', 0, 209, 17},
	{"Backspace", '\b', 0, 225, 17},
	{"KeyÀ", u'À', 0, 33, 33},
	{"KeyÁ", u'Á', 0, 49, 33},
	{"KeyÂ", u'Â', 0, 65, 33},
	{"KeyÄ", u'Ä', 0, 81, 33},
	{"KeyÈ", u'È', 0, 97, 33},
	{"KeyÉ", u'É', 0, 113, 33},
	{"KeyÊ", u'Ê', 0, 129, 33},
	{"KeyË", u'Ë', 0, 145, 33},
	{"KeyÌ", u'Ì', 0, 161, 33},
	{"KeyÍ", u'Í', 0, 177, 33},
	{"KeyÎ", u'Î', 0, 193, 33},
	{"KeyÏ", u'Ï', 0, 209, 33},
	{"Enter", '\n', 0, 225, 33},
	{"KeyÒ", u'Ò', 0, 33, 49},
	{"KeyÓ", u'Ó', 0, 49, 49},
	{"KeyÔ", u'Ô', 0, 65, 49},
	{"KeyÖ", u'Ö', 0, 81, 49},
	{"KeyŒ", u'Œ', 0, 97, 49},
	{"KeyÙ", u'Ù', 0, 113, 49},
	{"KeyÚ", u'Ú', 0, 129, 49},
	{"KeyÛ", u'Û', 0, 145, 49},
	{"KeyÜ", u'Ü', 0, 161, 49},
	{"KeyÇ", u'Ç', 0, 177, 49},
	{"KeyÑ", u'Ñ', 0, 193, 49},
	{"Key¡", u'¡', 0, 33, 65},
	{"Key¿", u'¿', 0, 49, 65},
	{"Key€", u'€', 0, 65, 65},
	{"Key¢", u'¢', 0, 81, 65},
	{"Key£", u'£', 0, 97, 65},
	{"Space", ' ', 0, 225, 65},
	{"Cancel", CANCEL, CANCEL, 225, 1}
};
const KeyDef boardKana[] = {
	{"InputAlphaNumeric", INPUT_ALPHANUMERIC, INPUT_ALPHANUMERIC, 1, 1},
	{"InputLatinAccented", INPUT_LATIN_ACCENTED, INPUT_LATIN_ACCENTED, 1, 17},
	{"InputKana", INPUT_KANA, INPUT_KANA, 1, 33},
	{"InputSymbol", INPUT_SYMBOL, INPUT_SYMBOL, 1, 49},
	{"InputPictogram", INPUT_PICTOGRAM, INPUT_PICTOGRAM, 1, 65},
	{"Hiragana", HIRAGANA, HIRAGANA, 17, 1},
	{"Keyあ", u'あ', u'ア', 49, 1},
	{"Keyか", u'か', u'カ', 65, 1},
	{"Keyさ", u'さ', u'サ', 81, 1},
	{"Keyた", u'た', u'タ', 97, 1},
	{"Keyな", u'な', u'ナ', 113, 1},
	{"Keyは", u'は', u'ハ', 129, 1},
	{"Keyま", u'ま', u'マ', 145, 1},
	{"Keyや", u'や', u'ヤ', 161, 1},
	{"Keyら", u'ら', u'ラ', 177, 1},
	{"Keyわ", u'わ', u'ワ', 193, 1},
	{"LongVowel", u'ー', u'ー', 209, 1},
	{"Katakana", KATAKANA, KATAKANA, 17, 17},
	{"Keyい", u'い', u'イ', 49, 17},
	{"Keyき", u'き', u'キ', 65, 17},
	{"Keyし", u'し', u'シ', 81, 17},
	{"Keyち", u'ち', u'チ', 97, 17},
	{"Keyに", u'に', u'ニ', 113, 17},
	{"Keyひ", u'ひ', u'ヒ', 129, 17},
	{"Keyみ", u'み', u'ミ', 145, 17},
	{"Keyり", u'り', u'リ', 177, 17},
	{"FullExclamation", u'！', u'！', 209, 17},
	{"Backspace", '\b', '\b', 225, 17},
	{"Voiced", VOICED, VOICED, 17, 33},
	{"Keyう", u'う', u'ウ', 49, 33},
	{"Keyく", u'く', u'ク', 65, 33},
	{"Keyす", u'す', u'ス', 81, 33},
	{"Keyつ", u'つ', u'ツ', 97, 33},
	{"Keyぬ", u'ぬ', u'ヌ', 113, 33},
	{"Keyふ", u'ふ', u'フ', 129, 33},
	{"Keyむ", u'む', u'ム', 145, 33},
	{"Keyゆ", u'ゆ', u'ユ', 161, 33},
	{"Keyる", u'る', u'ル', 177, 33},
	{"Keyん", u'ん', u'ン', 193, 33},
	{"FullQuestion", u'？', u'？', 209, 33},
	{"Enter", '\n', '\n', 225, 33},
	{"SemiVoiced", SEMI_VOICED, SEMI_VOICED, 17, 49},
	{"Keyえ", u'え', u'エ', 49, 49},
	{"Keyけ", u'け', u'ケ', 65, 49},
	{"Keyせ", u'せ', u'セ', 81, 49},
	{"Keyて", u'て', u'テ', 97, 49},
	{"Keyね", u'ね', u'ネ', 113, 49},
	{"Keyへ", u'へ', u'ヘ', 129, 49},
	{"Keyめ", u'め', u'メ', 145, 49},
	{"Keyれ", u'れ', u'レ', 177, 49},
	{"FullComma", u'、', u'、', 209, 49},
	{"SizeChange", SIZE_CHANGE, SIZE_CHANGE, 17, 65},
	{"Keyお", u'お', u'オ', 49, 65},
	{"Keyこ", u'こ', u'コ', 65, 65},
	{"Keyそ", u'そ', u'ソ', 81, 65},
	{"Keyと", u'と', u'ト', 97, 65},
	{"Keyの", u'の', u'ノ', 113, 65},
	{"Keyほ", u'ほ', u'ホ', 129, 65},
	{"Keyも", u'も', u'モ', 145, 65},
	{"Keyよ", u'よ', u'ヨ', 161, 65},
	{"Keyろ", u'ろ', u'ロ', 177, 65},
	{"Keyを", u'を', u'ヲ', 193, 65},
	{"FullStop", u'。', u'。', 209, 65},
	{"FullSpace", u'　', u'　', 225, 65},
	{"Cancel", CANCEL, CANCEL, 225, 1}
};
const KeyDef boardSymbol[] = {
	{"InputAlphaNumeric", INPUT_ALPHANUMERIC, INPUT_ALPHANUMERIC, 1, 1},
	{"InputLatinAccented", INPUT_LATIN_ACCENTED, INPUT_LATIN_ACCENTED, 1, 17},
	{"InputKana", INPUT_KANA, INPUT_KANA, 1, 33},
	{"InputSymbol", INPUT_SYMBOL, INPUT_SYMBOL, 1, 49},
	{"InputPictogram", INPUT_PICTOGRAM, INPUT_PICTOGRAM, 1, 65},
	{"Exclamation", '!', 0, 33, 1},
	{"Question", '?', 0, 49, 1},
	{"Ampersand", '&', 0, 65, 1},
	{"DoublePrime", u'″', 0, 81, 1},
	{"Apostrophe", '\'', 0, 97, 1},
	{"FullTilde", u'～', 0, 113, 1},
	{"Colon", ':', 0, 129, 1},
	{"Semicolon", ';', 0, 145, 1},
	{"At", '@', 0, 161, 1},
	{"Tilde", '~', 0, 177, 1},
	{"Underscore", '_', 0, 193, 1},
	{"Plus", '+', 0, 33, 17},
	{"Minus", '-', 0, 49, 17},
	{"Asterisk", '*', 0, 65, 17},
	{"Slash", '/', 0, 81, 17},
	{"Multiply", u'×', 0, 97, 17},
	{"Divide", u'÷', 0, 113, 17},
	{"Equals", '=', 0, 129, 17},
	{"RightArrow", u'→', 0, 145, 17},
	{"LeftArrow", u'←', 0, 161, 17},
	{"UpArrow", u'↑', 0, 177, 17},
	{"DownArrow", u'↓', 0, 193, 17},
	{"Backspace", '\b', 0, 225, 17},
	{"CornerBracketLeft", u'「', 0, 33, 33},
	{"CornerBracketRight", u'」', 0, 49, 33},
	{"QuoteLeft", u'“', 0, 65, 33},
	{"QuoteRight", u'”', 0, 81, 33},
	{"ParenthesisLeft", '(', 0, 97, 33},
	{"ParenthesisRight", ')', 0, 113, 33},
	{"LessThan", '<', 0, 129, 33},
	{"GreaterThan", '>', 0, 145, 33},
	{"CurlyBracketLeft", '{', 0, 161, 33},
	{"CurlyBracketRight", '}', 0, 177, 33},
	{"Bullet", u'•', 0, 193, 33},
	{"Enter", '\n', 0, 225, 33},
	{"Percent", '%', 0, 33, 49},
	{"Reference", u'※', 0, 49, 49},
	{"Postal", u'〒', 0, 65, 49},
	{"Number", '#', 0, 81, 49},
	{"Flat", u'♭', 0, 97, 49},
	{"EigthNote", u'♪', 0, 113, 49},
	{"PlusMinus", u'±', 0, 129, 49},
	{"Dollar", '$', 0, 145, 49},
	{"Cent", u'¢', 0, 161, 49},
	{"Pound", u'£', 0, 177, 49},
	{"Backslash", '\\', 0, 193, 49},
	{"Circumflex", '^', 0, 33, 65},
	{"Degree", u'°', 0, 49, 65},
	{"VerticalLine", u'｜', 0, 65, 65},
	{"Solidus", u'／', 0, 81, 65},
	{"ReverseSolidus", u'＼', 0, 97, 65},
	{"Infinity", u'∞', 0, 113, 65},
	{"Therefore", u'∴', 0, 129, 65},
	{"Ellipsis", u'…', 0, 145, 65},
	{"TradeMark", u'™', 0, 161, 65},
	{"Copyright", u'©', 0, 177, 65},
	{"Registered", u'®', 0, 193, 65},
	{"Space", ' ', 0, 225, 65},
	{"Cancel", CANCEL, CANCEL, 225, 1}
};
const KeyDef boardPictogram[] = {
	{"InputAlphaNumeric", INPUT_ALPHANUMERIC, INPUT_ALPHANUMERIC, 1, 1},
	{"InputLatinAccented", INPUT_LATIN_ACCENTED, INPUT_LATIN_ACCENTED, 1, 17},
	{"InputKana", INPUT_KANA, INPUT_KANA, 1, 33},
	{"InputSymbol", INPUT_SYMBOL, INPUT_SYMBOL, 1, 49},
	{"InputPictogram", INPUT_PICTOGRAM, INPUT_PICTOGRAM, 1, 65},
	{"Digit1", '1', 0, 33, 1},
	{"Digit2", '2', 0, 49, 1},
	{"Digit3", '3', 0, 65, 1},
	{"Digit4", '4', 0, 81, 1},
	{"Digit5", '5', 0, 97, 1},
	{"Digit6", '6', 0, 113, 1},
	{"Digit7", '7', 0, 129, 1},
	{"Digit8", '8', 0, 145, 1},
	{"Digit9", '9', 0, 161, 1},
	{"Digit0", '0', 0, 177, 1},
	{"Equals", '=', 0, 193, 1},
	{"PictoHappy", u'', 0, 33, 17},
	{"PictoAngry", u'', 0, 49, 17},
	{"PictoSad", u'', 0, 65, 17},
	{"PictoExpressionless", u'', 0, 81, 17},
	{"Sun", u'', 0, 97, 17},
	{"Cloud", u'', 0, 113, 17},
	{"Umbrella", u'', 0, 129, 17},
	{"Snowman", u'', 0, 145, 17},
	{"Envelope", u'', 0, 161, 17},
	{"Phone", u'', 0, 177, 17},
	{"AlarmClock", u'', 0, 193, 17},
	{"Backspace", '\b', 0, 225, 17},
	{"ButtonA", u'', 0, 33, 33},
	{"ButtonB", u'', 0, 49, 33},
	{"ButtonX", u'', 0, 65, 33},
	{"ButtonY", u'', 0, 81, 33},
	{"ButtonL", u'', 0, 97, 33},
	{"ButtonR", u'', 0, 113, 33},
	{"DPad", u'', 0, 129, 33},
	{"SuitSpade", u'', 0, 145, 33},
	{"SuitDiamond", u'', 0, 161, 33},
	{"SuitHeart", u'', 0, 177, 33},
	{"SuitClub", u'', 0, 193, 33},
	{"Enter", '\n', 0, 225, 33},
	{"SquaredExclamation", u'', 0, 33, 49},
	{"SquaredQuestion", u'', 0, 49, 49},
	{"Plus", '+', 0, 65, 49},
	{"Minus", '-', 0, 81, 49},
	{"StarWhite", u'☆', 0, 97, 49},
	{"CircleWhite", u'○', 0, 113, 49},
	{"DiamondWhite", u'◇', 0, 129, 49},
	{"SquareWhite", u'□', 0, 145, 49},
	{"TriangleUpWhite", u'△', 0, 161, 49},
	{"TriangleDownWhite", u'▽', 0, 177, 49},
	{"Bullseye", u'◎', 0, 193, 49},
	{"Right", u'', 0, 33, 65},
	{"Left", u'', 0, 49, 65},
	{"Up", u'', 0, 65, 65},
	{"Down", u'', 0, 81, 65},
	{"StarBlack", u'★', 0, 97, 65},
	{"CircleBlack", u'●', 0, 113, 65},
	{"DiamondBlack", u'◆', 0, 129, 65},
	{"SquareBlack", u'■', 0, 145, 65},
	{"TriangleUpBlack", u'▲', 0, 161, 65},
	{"TriangleDownBlack", u'▼', 0, 177, 65},
	{"Cross", u'', 0, 193, 65},
	{"Space", ' ', 0, 225, 65},
	{"Cancel", CANCEL, CANCEL, 225, 1}
};

const KeyDef* boards[5] = {boardAlphanumeric, boardLatinAccented, boardKana, boardSymbol, boardPictogram};
const u8 boardSizes[5] = {lengthof(boardAlphanumeric), lengthof(boardLatinAccented), lengthof(boardKana), lengthof(boardSymbol), lengthof(boardPictogram)};

// kana modifier key conversion maps
const char16_t voiceable[] = u"かきくけこさしすせそたちつってとはひふへほぱぴぷぺぽカヵキクケヶコサシスセソタチツッテトハヒフヘホパピプペポウゥ";
const char16_t voiced[]    = u"がぎぐげござじずぜぞだぢづづでどばびぶべぼばびぶべぼガガギグゲゲゴザジズゼゾダヂヅヅデドバビブベボバビブベボヴヴ";
const char16_t semivoiceable[] = u"はひふへほばびぶべぼハヒフヘホバビブベボ";
const char16_t semivoiced[]    = u"ぱぴぷぺぽぱぴぷぺぽパピプペポパピプペポ";
const char16_t shrinkable[] = u"あいうえおつづやゆよわアイウヴエオツヅヤユヨワカガケゲ";
const char16_t shrunk[]     = u"ぁぃぅぇぉっっゃゅょゎァィゥゥェォッッャュョヮヵヵヶヶ";

static u16 gfxKbdBuffer[SCREEN_WIDTH * KEYBOARD_HEIGHT] = {0};
static u16 gfxCmpBuffer[SCREEN_WIDTH * TEXT_HEIGHT] = {0};
char16_t compositionBuffer[256] = {0};
NitroFont keyFont = {0};
NitroFont compositionFont = {0};

bool showing = false;
u8 currentBoard = 0;
bool cancelEnabled = false, buttonControls = true;
bool shiftToggle = false, capsToggle = false;
enum HoldMode { NO_HOLD, TOUCHING, A_PRESS, B_PRESS, D_PAD_PRESS};
HoldMode heldMode = NO_HOLD;
int heldTime = 0;
int heldKeyIdx = -1;
int heldDir = 0;
int highlightedKeyIdx = -1;
ComposeStatus composing = KEYBOARD_INACTIVE;
bool closeOnAccept = false;
char16_t *compCursor = compositionBuffer;
const char16_t *compEnd = compositionBuffer + 255;
bool (*onPress) (const char16_t codepoint, const char *name, bool shift, int layout, bool repeat) = NULL;
bool (*onRelease) (const char16_t codepoint, const char *name, bool shift, int layout) = NULL;



u8 calcKeyWidth(KeyDef key, KeyDef nextKey) {
	if (currentBoard == 0) {
		if (key.lower == ' ') return SPACE_BAR_WIDTH;
		if (key.lower == '\\') return SCREEN_WIDTH - key.x - 1;
	}
	if (key.x == 1 || (key.lower > ' ' && key.lower != u'　')) return KEY_HEIGHT;
	if (nextKey.x > key.x) return nextKey.x - key.x - 1;
	return SCREEN_WIDTH - key.x - 1;
}
u8 calcKeyHeight(KeyDef key) {
	return key.lower == ENTER && currentBoard > 0 ? TALL_ENTER_HEIGHT : KEY_HEIGHT;
}
bool inKeyBounds(u8 x, u8 y, KeyDef key, u8 keyWidth, u8 keyHeight) {
	return x >= key.x && x < key.x + keyWidth && y >= key.y + (SCREEN_HEIGHT - KEYBOARD_HEIGHT) && y < key.y + (SCREEN_HEIGHT - KEYBOARD_HEIGHT) + keyHeight;
}
bool keyIsActive(KeyDef key) {
	return (shiftToggle && (key.lower == SHIFT || key.lower == KATAKANA))
		|| (capsToggle && key.lower == CAPS_LOCK)
		|| (!shiftToggle && key.lower == HIRAGANA)
		|| (currentBoard == key.lower - INPUT_ALPHANUMERIC);
}

void renderKey(KeyDef key, u8 keyWidth, u8 keyHeight, u8 palIdx) {
	if (key.lower == CANCEL && !cancelEnabled) return;
	u16 color = (key.lower == CANCEL ? PALETTE_KEY_CANCEL : (key.lower < '!' || key.lower == u'　' ? PALETTE_KEY_SPECIAL : PALETTE_KEY_NORMAL))[palIdx];
	for (u8 y = 0; y < keyHeight && key.y + y < KEYBOARD_HEIGHT-1; y++) {
		toncset16(gfxKbdBuffer + key.x + (key.y + y) * SCREEN_WIDTH, color, keyWidth);
	}
	char16_t codepoint = shiftToggle != capsToggle ? key.upper : key.lower;
	if (codepoint == '\n' && currentBoard > 0) {
		// hardcoded set of extra tiles that form the tall enter graphic
		fontPrintCodePoint(keyFont, PALETTE_FONT_KEY, 0x1E, gfxKbdBuffer, SCREEN_WIDTH, key.x + (keyWidth - fontGetCodePointWidth(keyFont, 0x1E)) / 2, key.y);
		fontPrintCodePoint(keyFont, PALETTE_FONT_KEY, 0x1F, gfxKbdBuffer, SCREEN_WIDTH, key.x + (keyWidth - fontGetCodePointWidth(keyFont, 0x1F)) / 2, key.y + keyFont.tileHeight);
	}
	else {
		u8 charWidth = fontGetCodePointWidth(keyFont, codepoint);
		fontPrintCodePoint(keyFont, PALETTE_FONT_KEY, codepoint, gfxKbdBuffer, SCREEN_WIDTH, key.x + (keyWidth - charWidth) / 2, key.y);
	}
}
void drawSingleKey(KeyDef key, u8 keyWidth, u8 keyHeight, u8 palIdx) {
	renderKey(key, keyWidth, keyHeight, palIdx);
	DC_FlushRange(gfxKbdBuffer, sizeof(gfxKbdBuffer));
	for (u8 y = 0; y < keyHeight && key.y + y < KEYBOARD_HEIGHT-1; y++) {
		dmaCopy(gfxKbdBuffer + key.x + (key.y + y) * SCREEN_WIDTH, bgGetGfxPtr(7) + key.x + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT + (key.y + y))), keyWidth * sizeof(u16));
	}
}
void drawSelectedBoard() {
	toncset16(gfxKbdBuffer, COLOR_KEYBOARD_BACKDROP, SCREEN_WIDTH * KEYBOARD_HEIGHT);
	for (int i = 0; i < boardSizes[currentBoard]; i++) {
		KeyDef key = boards[currentBoard][i];
		u8 keyWidth = calcKeyWidth(key, boards[currentBoard][(i + 1) % boardSizes[currentBoard]]);
		u8 keyHeight = calcKeyHeight(key);
		renderKey(key, keyWidth, keyHeight, i == highlightedKeyIdx ? HIGHLIGHTED : (keyIsActive(key) ? ACTIVE : NEUTRAL));
	}
	DC_FlushRange(gfxKbdBuffer, sizeof(gfxKbdBuffer));
	dmaCopy(gfxKbdBuffer, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT)), sizeof(gfxKbdBuffer));
}
void drawComposedText() {
	toncset16(gfxCmpBuffer, COLOR_COMPOSING_BACKDROP, SCREEN_WIDTH * TEXT_HEIGHT);
	compCursor[0] = 0;
	fontPrintUnicode(compositionFont, PALETTE_FONT_COMPOSITION, compositionBuffer, gfxCmpBuffer, SCREEN_WIDTH, 0, 0, SCREEN_WIDTH, true);
	DC_FlushRange(gfxCmpBuffer, sizeof(gfxCmpBuffer));
	dmaCopy(gfxCmpBuffer, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT - TEXT_HEIGHT)), sizeof(gfxCmpBuffer));
}

void composeKey(char16_t codepoint) {
	char16_t *lastChar = compCursor - 1;
	if (codepoint == ENTER) {
		composing = KEYBOARD_FINISHED;
		return;
	}
	else if (codepoint == CANCEL) {
		keyboardComposeCancel();
		return;
	}
	else if (codepoint == BACKSPACE) {
		if (compCursor == compositionBuffer) return;
		*(--compCursor) = 0;
	}
	else if (codepoint == VOICED) {
		if (compCursor == compositionBuffer) return;
		for (u32 i = 0; i < lengthof(voiceable); i++) {
			if (voiceable[i] == *lastChar) {
				*lastChar = voiced[i];
				break;
			}
			else if (voiced[i] == *lastChar) {
				*lastChar = voiceable[i];
				break;
			}
		}
	}
	else if (codepoint == SEMI_VOICED) {
		if (compCursor == compositionBuffer) return;
		for (u32 i = 0; i < lengthof(semivoiceable); i++) {
			if (semivoiceable[i] == *lastChar) {
				*lastChar = semivoiced[i];
				break;
			}
			else if (semivoiced[i] == *lastChar) {
				*lastChar = semivoiceable[i];
				break;
			}
		}
	}
	else if (codepoint == SIZE_CHANGE) {
		if (compCursor == compositionBuffer) return;
		for (u32 i = 0; i < lengthof(shrinkable); i++) {
			if (shrinkable[i] == *lastChar) {
				*lastChar = shrunk[i];
				break;
			}
			else if (shrunk[i] == *lastChar) {
				*lastChar = shrinkable[i];
				break;
			}
		}
	}
	else if (codepoint == TAB || codepoint >= ' ') {
		if (compCursor == compEnd) return;
		*(compCursor++) = codepoint;
	}
	drawComposedText();
}

void pressKey(KeyDef key, u8 keyWidth, u8 keyHeight, int idx, HoldMode mode) {
	char16_t codepoint = shiftToggle != capsToggle ? key.upper : key.lower;
	if (onPress != NULL) onPress(codepoint, key.name, shiftToggle != capsToggle, currentBoard, false);
	heldKeyIdx = idx;
	heldTime = 1;
	heldMode = mode;
	if (mode != B_PRESS) drawSingleKey(key, keyWidth, keyHeight, PRESSED);
}
void holdKey() {
	KeyDef key = boards[currentBoard][heldKeyIdx];
	if (++heldTime >= REPEAT_START && (heldTime - REPEAT_START) % REPEAT_INTERVAL == 0) {
		char16_t codepoint = shiftToggle != capsToggle ? key.upper : key.lower;
		bool canceled = false;
		if (onPress != NULL) canceled = onPress(codepoint, key.name, shiftToggle != capsToggle, currentBoard, true);
		if (!canceled && composing == KEYBOARD_COMPOSING) composeKey(codepoint);
		heldTime = REPEAT_START;
	}
}
void releaseKey() {
	KeyDef key = boards[currentBoard][heldKeyIdx];
	KeyDef nextKey = boards[currentBoard][(heldKeyIdx + 1) % boardSizes[currentBoard]];
	bool updateBoard = true;
	if (key.lower == SHIFT) shiftToggle = !shiftToggle;
	else if (key.lower == CAPS_LOCK) capsToggle = !capsToggle;
	else if (key.lower == HIRAGANA) shiftToggle = false;
	else if (key.lower == KATAKANA) shiftToggle = true;
	else if (key.lower >= INPUT_ALPHANUMERIC && key.lower <= INPUT_PICTOGRAM) {
		currentBoard = key.lower - INPUT_ALPHANUMERIC;
		shiftToggle = capsToggle = false;
	}
	else updateBoard = false;
	char16_t codepoint = shiftToggle != capsToggle ? key.upper : key.lower;
	bool canceled = false;
	if (onRelease != NULL) canceled = onRelease(codepoint, key.name, shiftToggle != capsToggle, currentBoard);
	if (!canceled && composing == KEYBOARD_COMPOSING && heldTime < REPEAT_START) composeKey(codepoint);
	if (currentBoard == 0 && key.lower != SHIFT) {
		shiftToggle = false;
		updateBoard = true;
	}
	if (updateBoard) drawSelectedBoard();
	else if (heldMode != B_PRESS) drawSingleKey(key, calcKeyWidth(key, nextKey), calcKeyHeight(key), heldKeyIdx == highlightedKeyIdx ? HIGHLIGHTED : NEUTRAL);
	heldKeyIdx = -1;
	heldTime = 0;
	heldMode = NO_HOLD;
}
void moveHighlight() {
	KeyDef key = boards[currentBoard][highlightedKeyIdx];
	u8 keyWidth = calcKeyWidth(key, boards[currentBoard][(highlightedKeyIdx + 1) % boardSizes[currentBoard]]);
	u8 keyHeight = calcKeyHeight(key);
	u8 x = key.x + keyWidth / 2;
	u8 y = SCREEN_HEIGHT - KEYBOARD_HEIGHT + key.y + keyHeight / 2;
	while (true) {
		if (heldDir == KEY_DOWN) y += KEY_HEIGHT, x++;
		else if (heldDir == KEY_UP) y -= KEY_HEIGHT, x--;
		else if (heldDir == KEY_RIGHT) x += KEY_HEIGHT, (y%2 ? 0 : y--);
		else if (heldDir == KEY_LEFT) x -= KEY_HEIGHT, (y%2 ? 0 : y--);
		if (y < SCREEN_HEIGHT - KEYBOARD_HEIGHT) y += KEYBOARD_HEIGHT;
		else if (y > SCREEN_HEIGHT) y -= KEYBOARD_HEIGHT;
		for (int i = 0; i < boardSizes[currentBoard]; i++) {
			if (i == highlightedKeyIdx) continue;
			KeyDef testKey = boards[currentBoard][i];
			if (testKey.lower == CANCEL && !cancelEnabled) continue;
			u8 testKeyWidth = calcKeyWidth(testKey, boards[currentBoard][(i + 1) % boardSizes[currentBoard]]);
			u8 testKeyHeight = calcKeyHeight(testKey);
			if (inKeyBounds(x, y, testKey, testKeyWidth, testKeyHeight)) {
				highlightedKeyIdx = i;
				drawSingleKey(key, keyWidth, keyHeight, keyIsActive(key) ? ACTIVE : NEUTRAL);
				drawSingleKey(testKey, testKeyWidth, testKeyHeight, HIGHLIGHTED);
				return;
			}
		}
	}
}



void keyboardInit(NitroFont composeFont) {
	compositionFont = composeFont;
	keyFont = fontLoad(keyboard_nftr);
}
void keyboardUpdate() {
	if (!showing) return;
	if (heldMode == NO_HOLD) {
		u32 down = keysDown();
		if (down & KEY_TOUCH) {
			touchPosition pos;
			touchRead(&pos);

			for (int i = 0; i < boardSizes[currentBoard]; i++) {
				KeyDef key = boards[currentBoard][i];
				if (key.lower == CANCEL && !cancelEnabled) continue;
				u8 keyWidth = calcKeyWidth(key, boards[currentBoard][(i + 1) % boardSizes[currentBoard]]);
				u8 keyHeight = calcKeyHeight(key);
				if (inKeyBounds(pos.px, pos.py, key, keyWidth, keyHeight)) {
					pressKey(key, keyWidth, keyHeight, i, TOUCHING);
					if (highlightedKeyIdx != -1) {
						KeyDef highlightedKey = boards[currentBoard][highlightedKeyIdx];
						u8 highlightWidth = calcKeyWidth(highlightedKey, boards[currentBoard][(highlightedKeyIdx + 1) % boardSizes[currentBoard]]);
						u8 highlightHeight = calcKeyHeight(highlightedKey);
						drawSingleKey(highlightedKey, highlightWidth, highlightHeight, keyIsActive(highlightedKey) ? ACTIVE : NEUTRAL);
						highlightedKeyIdx = -1;
					}
				}
			}
		}
		else if (!buttonControls) return;
		else if (down & KEY_B) {
			for (int i = 0; i < boardSizes[currentBoard]; i++) {
				KeyDef key = boards[currentBoard][i];
				if (key.lower == BACKSPACE) return pressKey(key, 0, 0, i, B_PRESS);
			}
		}
		else if (highlightedKeyIdx == -1) {
			if (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
				heldMode = D_PAD_PRESS;
				heldTime = 0;
				heldDir = (down & KEY_DOWN) ? KEY_DOWN : ((down & KEY_UP) ? KEY_UP : ((down & KEY_RIGHT) ? KEY_RIGHT : KEY_LEFT));
				highlightedKeyIdx = 0;
				drawSingleKey(boards[currentBoard][0], calcKeyWidth(boards[currentBoard][0], boards[currentBoard][1]), KEY_HEIGHT, HIGHLIGHTED);
			}
		}
		else if (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
			heldMode = D_PAD_PRESS;
			heldTime = 0;
			heldDir = (down & KEY_DOWN) ? KEY_DOWN : ((down & KEY_UP) ? KEY_UP : ((down & KEY_RIGHT) ? KEY_RIGHT : KEY_LEFT));
			moveHighlight();
		}
		else if (down & KEY_A) {
			KeyDef key = boards[currentBoard][highlightedKeyIdx];
			u8 keyWidth = calcKeyWidth(key, boards[currentBoard][(highlightedKeyIdx + 1) % boardSizes[currentBoard]]);
			u8 keyHeight = calcKeyHeight(key);
			pressKey(key, keyWidth, keyHeight, highlightedKeyIdx, A_PRESS);
		}
	}
	else if (heldMode == TOUCHING) {
		if (keysHeld() & KEY_TOUCH) {
			touchPosition pos;
			touchRead(&pos);
			
			KeyDef key = boards[currentBoard][heldKeyIdx];
			u8 keyWidth = calcKeyWidth(key, boards[currentBoard][(heldKeyIdx + 1) % boardSizes[currentBoard]]);
			u8 keyHeight = calcKeyHeight(key);
			if (inKeyBounds(pos.px, pos.py, key, keyWidth, keyHeight)) holdKey();
			else heldTime = 0;
		}
		else releaseKey();
	}
	else if (heldMode == A_PRESS) {
		if (keysHeld() & KEY_A) {
			holdKey();
			if (heldTime == REPEAT_START && boards[currentBoard][highlightedKeyIdx].lower == CANCEL) {
				releaseKey();
				highlightedKeyIdx = -1;
			}
		}
		else {
			releaseKey();
			if (boards[currentBoard][highlightedKeyIdx].lower == CANCEL) highlightedKeyIdx = -1;
		}
	}
	else if (heldMode == B_PRESS) {
		if (keysHeld() & KEY_B) holdKey();
		else releaseKey();
	}
	else if (heldMode == D_PAD_PRESS) {
		u32 heldButtons = keysHeld();
		u32 down = keysDown();
		if (heldButtons & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
			if (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
				heldDir = (down & KEY_DOWN) ? KEY_DOWN : ((down & KEY_UP) ? KEY_UP : ((down & KEY_RIGHT) ? KEY_RIGHT : KEY_LEFT));
				heldTime = 0;
				moveHighlight();
			}
			else if (keysUp() & heldDir) {
				heldDir = (heldButtons & KEY_DOWN) ? KEY_DOWN : ((heldButtons & KEY_UP) ? KEY_UP : ((heldButtons & KEY_RIGHT) ? KEY_RIGHT : KEY_LEFT));
				heldTime = 0;
				moveHighlight();
			}
			else if (++heldTime >= REPEAT_START && (heldTime - REPEAT_START) % REPEAT_INTERVAL == 0) {
				heldTime = REPEAT_START;
				moveHighlight();
			}
		}
		else heldMode = NO_HOLD;
		// allow initiating A and B presses during D-Pad movement
		HoldMode tempMode = heldMode;
		int tempTime = heldTime;
		if (down & KEY_A) {
			KeyDef key = boards[currentBoard][highlightedKeyIdx];
			u8 keyWidth = calcKeyWidth(key, boards[currentBoard][(highlightedKeyIdx + 1) % boardSizes[currentBoard]]);
			u8 keyHeight = calcKeyHeight(key);
			pressKey(key, keyWidth, keyHeight, highlightedKeyIdx, A_PRESS);
			releaseKey();
		}
		else if (down & KEY_B) {
			for (int i = 0; i < boardSizes[currentBoard]; i++) {
				KeyDef key = boards[currentBoard][i];
				if (key.lower == BACKSPACE) {
					pressKey(key, 0, 0, i, B_PRESS);
					releaseKey();
				}
			}
		}
		heldMode = tempMode;
		heldTime = tempTime;
	}
}

bool keyboardShow() {
	if (showing) return false;
	consoleShrink();
	drawSelectedBoard();
	showing = true;
	return true;
}
bool keyboardHide() {
	if (!showing) return false;
	dmaFillHalfWords(0, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT)), sizeof(gfxKbdBuffer));
	consoleExpand();
	showing = false;
	return true;
}
bool keyboardButtonControls(bool enabled) {
	bool wasEnabled = buttonControls;
	buttonControls = enabled;
	return wasEnabled;
}

void keyboardSetPressHandler(bool (*handler) (const char16_t codepoint, const char *name, bool shift, int layout, bool repeat)) {
	onPress = handler;
}
void keyboardSetReleaseHandler(bool (*handler) (const char16_t codepoint, const char *name, bool shift, int layout)) {
	onRelease = handler;
}

void keyboardCompose(bool allowCancel) {
	composing = KEYBOARD_COMPOSING;
	closeOnAccept = !showing;
	if (cancelEnabled != allowCancel) {
		showing = false;
		cancelEnabled = allowCancel;
	}
	keyboardShow();
	compCursor = compositionBuffer;
	drawComposedText();
}
ComposeStatus keyboardComposeStatus() {
	return composing;
}
void keyboardComposeAccept(char **strPtr, u32 *strSize) {
	*strPtr = UTF16toUTF8(compositionBuffer, compCursor - compositionBuffer, strSize);
	keyboardComposeCancel();
}
void keyboardComposeCancel() {
	composing = KEYBOARD_INACTIVE;
	cancelEnabled = false;
	if (closeOnAccept) keyboardHide();
	else drawSelectedBoard();
	dmaFillHalfWords(0, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT - TEXT_HEIGHT)), sizeof(gfxCmpBuffer));
}