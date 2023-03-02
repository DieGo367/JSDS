#include "keyboard.hpp"

#include <nds/arm9/background.h>
#include <nds/arm9/input.h>
#include <stdlib.h>
#include <string.h>

#include "font.hpp"

#define lengthof(arr) sizeof(arr)/sizeof(*arr)



const int REPEAT_START = 30;
const int REPEAT_INTERVAL = 5;

const u8 KEYBOARD_HEIGHT = 80;
const u8 KEY_HEIGHT = 15;
const u8 TEXT_HEIGHT = 16;

const u16 COLOR_KEYBOARD_BACKDROP = 0xDAD6;
const u16 COLOR_KEY_NORMAL = 0xFFFF;
const u16 COLOR_COMPOSING_BACKDROP = 0xFFFF;

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
	u16 lower;
	u16 upper;
	u8 x;
	u8 y;
	u8 width;
};

const KeyDef boardModeSelect[] = {
	{"InputAlphaNumeric", INPUT_ALPHANUMERIC, INPUT_ALPHANUMERIC, 1, 1, 15},
	{"InputLatinAccented", INPUT_LATIN_ACCENTED, INPUT_LATIN_ACCENTED, 1, 17, 15},
	{"InputKana", INPUT_KANA, INPUT_KANA, 1, 33, 15},
	{"InputSymbol", INPUT_SYMBOL, INPUT_SYMBOL, 1, 49, 15},
	{"InputPictogram", INPUT_PICTOGRAM, INPUT_PICTOGRAM, 1, 65, 15}
};

const KeyDef boardAlphanumeric[] = {
	{"Backquote", '`', '~', 17, 1, 15},
	{"Digit1", '1', '!', 33, 1, 15},
	{"Digit2", '2', '@', 49, 1, 15},
	{"Digit3", '3', '#', 65, 1, 15},
	{"Digit4", '4', '$', 81, 1, 15},
	{"Digit5", '5', '%', 97, 1, 15},
	{"Digit6", '6', '^', 113, 1, 15},
	{"Digit7", '7', '&', 129, 1, 15},
	{"Digit8", '8', '*', 145, 1, 15},
	{"Digit9", '9', '(', 161, 1, 15},
	{"Digit0", '0', ')', 177, 1, 15},
	{"Minus", '-', '_', 193, 1, 15},
	{"Equal", '=', '+', 209, 1, 15},
	{"Backspace", '\b', '\b', 225, 1, 30},
	{"Tab", '\t', '\t', 17, 17, 23},
	{"KeyQ", 'q', 'Q', 41, 17, 15},
	{"KeyW", 'w', 'W', 57, 17, 15},
	{"KeyE", 'e', 'E', 73, 17, 15},
	{"KeyR", 'r', 'R', 89, 17, 15},
	{"KeyT", 't', 'T', 105, 17, 15},
	{"KeyY", 'y', 'Y', 121, 17, 15},
	{"KeyU", 'u', 'U', 137, 17, 15},
	{"KeyI", 'i', 'I', 153, 17, 15},
	{"KeyO", 'o', 'O', 169, 17, 15},
	{"KeyP", 'p', 'P', 185, 17, 15},
	{"BracketLeft", '[', '{', 201, 17, 15},
	{"BracketRight", ']', '}', 217, 17, 15},
	{"Backslash", '\\', '|', 233, 17, 22},
	{"CapsLock", CAPS_LOCK, CAPS_LOCK, 17, 33, 27},
	{"KeyA", 'a', 'A', 45, 33, 15},
	{"KeyS", 's', 'S', 61, 33, 15},
	{"KeyD", 'd', 'D', 77, 33, 15},
	{"KeyF", 'f', 'F', 93, 33, 15},
	{"KeyG", 'g', 'G', 109, 33, 15},
	{"KeyH", 'h', 'H', 125, 33, 15},
	{"KeyJ", 'j', 'J', 141, 33, 15},
	{"KeyK", 'k', 'K', 157, 33, 15},
	{"KeyL", 'l', 'L', 173, 33, 15},
	{"Semicolon", ';', ':', 189, 33, 15},
	{"Quote", '\'', '"', 205, 33, 15},
	{"Enter", '\n', '\n', 221, 33, 34},
	{"ShiftLeft", SHIFT, SHIFT, 17, 49, 35},
	{"KeyZ", 'z', 'Z', 53, 49, 15},
	{"KeyX", 'x', 'X', 69, 49, 15},
	{"KeyC", 'c', 'C', 85, 49, 15},
	{"KeyV", 'v', 'V', 101, 49, 15},
	{"KeyB", 'b', 'B', 117, 49, 15},
	{"KeyN", 'n', 'N', 133, 49, 15},
	{"KeyM", 'm', 'M', 149, 49, 15},
	{"Comma", ',', '<', 165, 49, 15},
	{"Period", '.', '>', 181, 49, 15},
	{"Slash", '/', '?', 197, 49, 15},
	{"ShiftRight", SHIFT, SHIFT, 213, 49, 42},
	{"Space", ' ', ' ', 81, 65, 95}
};
const KeyDef boardLatinAccented[] = {
	{"Keyà", u'à', 0, 33, 1, 15},
	{"Keyá", u'á', 0, 49, 1, 15},
	{"Keyâ", u'â', 0, 65, 1, 15},
	{"Keyä", u'ä', 0, 81, 1, 15},
	{"Keyè", u'è', 0, 97, 1, 15},
	{"Keyé", u'é', 0, 113, 1, 15},
	{"Keyê", u'ê', 0, 129, 1, 15},
	{"Keyë", u'ë', 0, 145, 1, 15},
	{"Keyì", u'ì', 0, 161, 1, 15},
	{"Keyí", u'í', 0, 177, 1, 15},
	{"Keyî", u'î', 0, 193, 1, 15},
	{"Keyï", u'ï', 0, 209, 1, 15},
	{"Keyò", u'ò', 0, 33, 17, 15},
	{"Keyó", u'ó', 0, 49, 17, 15},
	{"Keyô", u'ô', 0, 65, 17, 15},
	{"Keyö", u'ö', 0, 81, 17, 15},
	{"Keyœ", u'œ', 0, 97, 17, 15},
	{"Keyù", u'ù', 0, 113, 17, 15},
	{"Keyú", u'ú', 0, 129, 17, 15},
	{"Keyû", u'û', 0, 145, 17, 15},
	{"Keyü", u'ü', 0, 161, 17, 15},
	{"Keyç", u'ç', 0, 177, 17, 15},
	{"Keyñ", u'ñ', 0, 193, 17, 15},
	{"Keyß", u'ß', 0, 209, 17, 15},
	{"Backspace", '\b', 0, 225, 17, 30},
	{"KeyÀ", u'À', 0, 33, 33, 15},
	{"KeyÁ", u'Á', 0, 49, 33, 15},
	{"KeyÂ", u'Â', 0, 65, 33, 15},
	{"KeyÄ", u'Ä', 0, 81, 33, 15},
	{"KeyÈ", u'È', 0, 97, 33, 15},
	{"KeyÉ", u'É', 0, 113, 33, 15},
	{"KeyÊ", u'Ê', 0, 129, 33, 15},
	{"KeyË", u'Ë', 0, 145, 33, 15},
	{"KeyÌ", u'Ì', 0, 161, 33, 15},
	{"KeyÍ", u'Í', 0, 177, 33, 15},
	{"KeyÎ", u'Î', 0, 193, 33, 15},
	{"KeyÏ", u'Ï', 0, 209, 33, 15},
	{"Enter", '\n', 0, 225, 33, 30},
	{"KeyÒ", u'Ò', 0, 33, 49, 15},
	{"KeyÓ", u'Ó', 0, 49, 49, 15},
	{"KeyÔ", u'Ô', 0, 65, 49, 15},
	{"KeyÖ", u'Ö', 0, 81, 49, 15},
	{"KeyŒ", u'Œ', 0, 97, 49, 15},
	{"KeyÙ", u'Ù', 0, 113, 49, 15},
	{"KeyÚ", u'Ú', 0, 129, 49, 15},
	{"KeyÛ", u'Û', 0, 145, 49, 15},
	{"KeyÜ", u'Ü', 0, 161, 49, 15},
	{"KeyÇ", u'Ç', 0, 177, 49, 15},
	{"KeyÑ", u'Ñ', 0, 193, 49, 15},
	{"Key¡", u'¡', 0, 33, 65, 15},
	{"Key¿", u'¿', 0, 49, 65, 15},
	{"Key€", u'€', 0, 65, 65, 15},
	{"Key¢", u'¢', 0, 81, 65, 15},
	{"Key£", u'£', 0, 97, 65, 15},
	{"Space", ' ', 0, 225, 65, 30}
};
const KeyDef boardKana[] = {
	{"Hiragana", HIRAGANA, HIRAGANA, 17, 1, 31},
	{"Keyあ", u'あ', u'ア', 49, 1, 15},
	{"Keyか", u'か', u'カ', 65, 1, 15},
	{"Keyさ", u'さ', u'サ', 81, 1, 15},
	{"Keyた", u'た', u'タ', 97, 1, 15},
	{"Keyな", u'な', u'ナ', 113, 1, 15},
	{"Keyは", u'は', u'ハ', 129, 1, 15},
	{"Keyま", u'ま', u'マ', 145, 1, 15},
	{"Keyや", u'や', u'ヤ', 161, 1, 15},
	{"Keyら", u'ら', u'ラ', 177, 1, 15},
	{"Keyわ", u'わ', u'ワ', 193, 1, 15},
	{"LongVowel", u'ー', u'ー', 209, 1, 15},
	{"Katakana", KATAKANA, KATAKANA, 17, 17, 31},
	{"Keyい", u'い', u'イ', 49, 17, 15},
	{"Keyき", u'き', u'キ', 65, 17, 15},
	{"Keyし", u'し', u'シ', 81, 17, 15},
	{"Keyち", u'ち', u'チ', 97, 17, 15},
	{"Keyに", u'に', u'ニ', 113, 17, 15},
	{"Keyひ", u'ひ', u'ヒ', 129, 17, 15},
	{"Keyみ", u'み', u'ミ', 145, 17, 15},
	{"Keyり", u'り', u'リ', 177, 17, 15},
	{"FullExclamation", u'！', u'！', 209, 17, 15},
	{"Backspace", '\b', '\b', 225, 17, 30},
	{"Voiced", VOICED, VOICED, 17, 33, 31},
	{"Keyう", u'う', u'ウ', 49, 33, 15},
	{"Keyく", u'く', u'ク', 65, 33, 15},
	{"Keyす", u'す', u'ス', 81, 33, 15},
	{"Keyつ", u'つ', u'ツ', 97, 33, 15},
	{"Keyぬ", u'ぬ', u'ヌ', 113, 33, 15},
	{"Keyふ", u'ふ', u'フ', 129, 33, 15},
	{"Keyむ", u'む', u'ム', 145, 33, 15},
	{"Keyゆ", u'ゆ', u'ユ', 161, 33, 15},
	{"Keyる", u'る', u'ル', 177, 33, 15},
	{"Keyん", u'ん', u'ン', 193, 33, 15},
	{"FullQuestion", u'？', u'？', 209, 33, 15},
	{"Enter", '\n', '\n', 225, 33, 30},
	{"SemiVoiced", SEMI_VOICED, SEMI_VOICED, 17, 49, 31},
	{"Keyえ", u'え', u'エ', 49, 49, 15},
	{"Keyけ", u'け', u'ケ', 65, 49, 15},
	{"Keyせ", u'せ', u'セ', 81, 49, 15},
	{"Keyて", u'て', u'テ', 97, 49, 15},
	{"Keyね", u'ね', u'ネ', 113, 49, 15},
	{"Keyへ", u'へ', u'ヘ', 129, 49, 15},
	{"Keyめ", u'め', u'メ', 145, 49, 15},
	{"Keyれ", u'れ', u'レ', 177, 49, 15},
	{"FullComma", u'、', u'、', 209, 49, 15},
	{"SizeChange", SIZE_CHANGE, SIZE_CHANGE, 17, 65, 31},
	{"Keyお", u'お', u'オ', 49, 65, 15},
	{"Keyこ", u'こ', u'コ', 65, 65, 15},
	{"Keyそ", u'そ', u'ソ', 81, 65, 15},
	{"Keyと", u'と', u'ト', 97, 65, 15},
	{"Keyの", u'の', u'ノ', 113, 65, 15},
	{"Keyほ", u'ほ', u'ホ', 129, 65, 15},
	{"Keyも", u'も', u'モ', 145, 65, 15},
	{"Keyよ", u'よ', u'ヨ', 161, 65, 15},
	{"Keyろ", u'ろ', u'ロ', 177, 65, 15},
	{"Keyを", u'を', u'ヲ', 193, 65, 15},
	{"FullStop", u'。', u'。', 209, 65, 15},
	{"FullSpace", u'　', u'　', 225, 65, 30}
};
const KeyDef boardSymbol[] = {
	{"Exclamation", '!', 0, 33, 1, 15},
	{"Question", '?', 0, 49, 1, 15},
	{"Ampersand", '&', 0, 65, 1, 15},
	{"DoublePrime", u'″', 0, 81, 1, 15},
	{"Apostrophe", '\'', 0, 97, 1, 15},
	{"FullTilde", u'～', 0, 113, 1, 15},
	{"Colon", ':', 0, 129, 1, 15},
	{"Semicolon", ';', 0, 145, 1, 15},
	{"At", '@', 0, 161, 1, 15},
	{"Tilde", '~', 0, 177, 1, 15},
	{"Underscore", '_', 0, 193, 1, 15},
	{"Plus", '+', 0, 33, 17, 15},
	{"Minus", '-', 0, 49, 17, 15},
	{"Asterisk", '*', 0, 65, 17, 15},
	{"Slash", '/', 0, 81, 17, 15},
	{"Multiply", u'×', 0, 97, 17, 15},
	{"Divide", u'÷', 0, 113, 17, 15},
	{"Equals", '=', 0, 129, 17, 15},
	{"RightArrow", u'→', 0, 145, 17, 15},
	{"LeftArrow", u'←', 0, 161, 17, 15},
	{"UpArrow", u'↑', 0, 177, 17, 15},
	{"DownArrow", u'↓', 0, 193, 17, 15},
	{"Backspace", '\b', 0, 225, 17, 30},
	{"CornerBracketLeft", u'「', 0, 33, 33, 15},
	{"CornerBracketRight", u'」', 0, 49, 33, 15},
	{"QuoteLeft", u'“', 0, 65, 33, 15},
	{"QuoteRight", u'”', 0, 81, 33, 15},
	{"ParenthesisLeft", '(', 0, 97, 33, 15},
	{"ParenthesisRight", ')', 0, 113, 33, 15},
	{"LessThan", '<', 0, 129, 33, 15},
	{"GreaterThan", '>', 0, 145, 33, 15},
	{"CurlyBracketLeft", '{', 0, 161, 33, 15},
	{"CurlyBracketRight", '}', 0, 177, 33, 15},
	{"Bullet", u'•', 0, 193, 33, 15},
	{"Enter", '\n', 0, 225, 33, 30},
	{"Percent", '%', 0, 33, 49, 15},
	{"Reference", u'※', 0, 49, 49, 15},
	{"Postal", u'〒', 0, 65, 49, 15},
	{"Number", '#', 0, 81, 49, 15},
	{"Flat", u'♭', 0, 97, 49, 15},
	{"EigthNote", u'♪', 0, 113, 49, 15},
	{"PlusMinus", u'±', 0, 129, 49, 15},
	{"Dollar", '$', 0, 145, 49, 15},
	{"Cent", u'¢', 0, 161, 49, 15},
	{"Pound", u'£', 0, 177, 49, 15},
	{"Backslash", '\\', 0, 193, 49, 15},
	{"Circumflex", '^', 0, 33, 65, 15},
	{"Degree", u'°', 0, 49, 65, 15},
	{"VerticalLine", u'｜', 0, 65, 65, 15},
	{"Solidus", u'／', 0, 81, 65, 15},
	{"ReverseSolidus", u'＼', 0, 97, 65, 15},
	{"Infinity", u'∞', 0, 113, 65, 15},
	{"Therefore", u'∴', 0, 129, 65, 15},
	{"Ellipsis", u'…', 0, 145, 65, 15},
	{"TradeMark", u'™', 0, 161, 65, 15},
	{"Copyright", u'©', 0, 177, 65, 15},
	{"Registered", u'®', 0, 193, 65, 15},
	{"Space", ' ', 0, 225, 65, 30}
};
const KeyDef boardPictogram[] = {
	{"Digit1", '1', 0, 33, 1, 15},
	{"Digit2", '2', 0, 49, 1, 15},
	{"Digit3", '3', 0, 65, 1, 15},
	{"Digit4", '4', 0, 81, 1, 15},
	{"Digit5", '5', 0, 97, 1, 15},
	{"Digit6", '6', 0, 113, 1, 15},
	{"Digit7", '7', 0, 129, 1, 15},
	{"Digit8", '8', 0, 145, 1, 15},
	{"Digit9", '9', 0, 161, 1, 15},
	{"Digit0", '0', 0, 177, 1, 15},
	{"Equals", '=', 0, 193, 1, 15},
	{"PictoHappy", u'', 0, 33, 17, 15},
	{"PictoAngry", u'', 0, 49, 17, 15},
	{"PictoSad", u'', 0, 65, 17, 15},
	{"PictoExpressionless", u'', 0, 81, 17, 15},
	{"Sun", u'', 0, 97, 17, 15},
	{"Cloud", u'', 0, 113, 17, 15},
	{"Umbrella", u'', 0, 129, 17, 15},
	{"Snowman", u'', 0, 145, 17, 15},
	{"Envelope", u'', 0, 161, 17, 15},
	{"Phone", u'', 0, 177, 17, 15},
	{"AlarmClock", u'', 0, 193, 17, 15},
	{"Backspace", '\b', 0, 225, 17, 30},
	{"ButtonA", u'', 0, 33, 33, 15},
	{"ButtonB", u'', 0, 49, 33, 15},
	{"ButtonX", u'', 0, 65, 33, 15},
	{"ButtonY", u'', 0, 81, 33, 15},
	{"ButtonL", u'', 0, 97, 33, 15},
	{"ButtonR", u'', 0, 113, 33, 15},
	{"D-Pad", u'', 0, 129, 33, 15},
	{"SuitSpade", u'', 0, 145, 33, 15},
	{"SuitDiamond", u'', 0, 161, 33, 15},
	{"SuitHeart", u'', 0, 177, 33, 15},
	{"SuitClub", u'', 0, 193, 33, 15},
	{"Enter", '\n', 0, 225, 33, 30},
	{"SquaredExclamation", u'', 0, 33, 49, 15},
	{"SquaredQuestion", u'', 0, 49, 49, 15},
	{"Plus", '+', 0, 65, 49, 15},
	{"Minus", '-', 0, 81, 49, 15},
	{"StarWhite", u'☆', 0, 97, 49, 15},
	{"CircleWhite", u'○', 0, 113, 49, 15},
	{"DiamondWhite", u'◇', 0, 129, 49, 15},
	{"SquareWhite", u'□', 0, 145, 49, 15},
	{"TriangleUpWhite", u'△', 0, 161, 49, 15},
	{"TriangleDownWhite", u'▽', 0, 177, 49, 15},
	{"Bullseye", u'◎', 0, 193, 49, 15},
	{"Right", u'', 0, 33, 65, 15},
	{"Left", u'', 0, 49, 65, 15},
	{"Up", u'', 0, 65, 65, 15},
	{"Down", u'', 0, 81, 65, 15},
	{"StarBlack", u'★', 0, 97, 65, 15},
	{"CircleBlack", u'●', 0, 113, 65, 15},
	{"DiamondBlack", u'◆', 0, 129, 65, 15},
	{"SquareBlack", u'■', 0, 145, 65, 15},
	{"TriangleUpBlack", u'▲', 0, 161, 65, 15},
	{"TriangleDownBlack", u'▼', 0, 177, 65, 15},
	{"Cross", u'', 0, 193, 65, 15},
	{"Space", ' ', 0, 225, 65, 30}
};

const KeyDef* boards[5] = {boardAlphanumeric, boardLatinAccented, boardKana, boardSymbol, boardPictogram};
const u8 boardSizes[5] = {lengthof(boardAlphanumeric), lengthof(boardLatinAccented), lengthof(boardKana), lengthof(boardSymbol), lengthof(boardPictogram)};

// kana modifier key conversion maps
const u16 voiceable[] = {u'か', u'き', u'く', u'け', u'こ', u'さ', u'し', u'す', u'せ', u'そ', u'た', u'ち', u'つ', u'っ', u'て', u'と', u'は', u'ひ', u'ふ', u'へ', u'ほ', u'ぱ', u'ぴ', u'ぷ', u'ぺ', u'ぽ', u'カ', u'ヵ', u'キ', u'ク', u'ケ', u'ヶ', u'コ', u'サ', u'シ', u'ス', u'セ', u'ソ', u'タ', u'チ', u'ツ', u'ッ', u'テ', u'ト', u'ハ', u'ヒ', u'フ', u'ヘ', u'ホ', u'パ', u'ピ', u'プ', u'ペ', u'ポ', u'ウ', u'ゥ'};
const u16 voiced[]    = {u'が', u'ぎ', u'ぐ', u'げ', u'ご', u'ざ', u'じ', u'ず', u'ぜ', u'ぞ', u'だ', u'ぢ', u'づ', u'づ', u'で', u'ど', u'ば', u'び', u'ぶ', u'べ', u'ぼ', u'ば', u'び', u'ぶ', u'べ', u'ぼ', u'ガ', u'ガ', u'ギ', u'グ', u'ゲ', u'ゲ', u'ゴ', u'ザ', u'ジ', u'ズ', u'ゼ', u'ゾ', u'ダ', u'ヂ', u'ヅ', u'ヅ', u'デ', u'ド', u'バ', u'ビ', u'ブ', u'ベ', u'ボ', u'バ', u'ビ', u'ブ', u'ベ', u'ボ', u'ヴ', u'ヴ'};
const u16 semivoiceable[] = {u'は', u'ひ', u'ふ', u'へ', u'ほ', u'ば', u'び', u'ぶ', u'べ', u'ぼ', u'ハ', u'ヒ', u'フ', u'ヘ', u'ホ', u'バ', u'ビ', u'ブ', u'ベ', u'ボ'};
const u16 semivoiced[]    = {u'ぱ', u'ぴ', u'ぷ', u'ぺ', u'ぽ', u'ぱ', u'ぴ', u'ぷ', u'ぺ', u'ぽ', u'パ', u'ピ', u'プ', u'ペ', u'ポ', u'パ', u'ピ', u'プ', u'ペ', u'ポ'};
const u16 shrinkable[] = {u'あ', u'い', u'う', u'え', u'お', u'つ', u'づ', u'や', u'ゆ', u'よ', u'わ', u'ア', u'イ', u'ウ', u'ヴ', u'エ', u'オ', u'ツ', u'ヅ', u'ヤ', u'ユ', u'ヨ', u'ワ', u'カ', u'ガ', u'ケ', u'ゲ'};
const u16 shrunk[]     = {u'ぁ', u'ぃ', u'ぅ', u'ぇ', u'ぉ', u'っ', u'っ', u'ゃ', u'ゅ', u'ょ', u'ゎ', u'ァ', u'ィ', u'ゥ', u'ゥ', u'ェ', u'ォ', u'ッ', u'ッ', u'ャ', u'ュ', u'ョ', u'ヮ', u'ヵ', u'ヵ', u'ヶ', u'ヶ'};

static u16 gfxKbdBuffer[SCREEN_WIDTH * KEYBOARD_HEIGHT] = {0};
static u16 gfxCmpBuffer[SCREEN_WIDTH * TEXT_HEIGHT] = {0};
u16 compositionBuffer[256] = {0};
u16 keyFontPalette[4] = {0, 0xCA52, 0xA108, 0x8000};

bool showing = false;
u8 currentBoard = 0;
KeyDef heldKey = {0};
int keyHeldTime = 0;
bool shiftToggle = false, ctrlToggle = false, altToggle = false, metaToggle = false, capsToggle = false;
ComposeStatus composing = INACTIVE;
bool closeOnAccept = false;
u16 *compCursor = compositionBuffer;
const u16 *compEnd = compositionBuffer + 255;
void (*onPress) (const u16 codepoint, const char *name, bool shift, bool ctrl, bool alt, bool meta, bool caps) = NULL;
void (*onRelease) (const u16 codepoint, const char *name, bool shift, bool ctrl, bool alt, bool meta, bool caps) = NULL;



void drawSelectedBoard() {
	for (u8 i = 0; i < boardSizes[currentBoard]; i++) {
		KeyDef key = boards[currentBoard][i];
		for (u8 y = 0; y < KEY_HEIGHT && key.y + y < KEYBOARD_HEIGHT-1; y++) {
			for (u8 x = 0; x < key.width; x++) {
				gfxKbdBuffer[key.x + x + (key.y + y) * SCREEN_WIDTH] = COLOR_KEY_NORMAL;
			}
		}
		u16 codepoint = shiftToggle != capsToggle ? key.upper : key.lower;
		fontPrintChar(defaultFont, keyFontPalette, codepoint, gfxKbdBuffer, SCREEN_WIDTH, key.x, key.y - 1);
	}
}
void drawComposedText() {
	for (int i = 0; i < SCREEN_WIDTH * TEXT_HEIGHT; i++) gfxCmpBuffer[i] = COLOR_COMPOSING_BACKDROP;
	
	int x = 0;
	for (u16 *codePtr = compositionBuffer; codePtr != compCursor; codePtr++) {
		int width = fontGetCharWidth(defaultFont, *codePtr);
		int diff = x + width - SCREEN_WIDTH;
		if (diff <= 0) {
			fontPrintChar(defaultFont, keyFontPalette, *codePtr, gfxCmpBuffer, SCREEN_WIDTH, x, 0);
			x += width;
		}
		else {
			for (int j = 0; j < TEXT_HEIGHT; j++) {
				memmove(gfxCmpBuffer + j * SCREEN_WIDTH, gfxCmpBuffer + j * SCREEN_WIDTH + diff, (SCREEN_WIDTH - diff) * sizeof(u16));
				u16 *px = gfxCmpBuffer + (j + 1) * SCREEN_WIDTH - width;
				for (int k = 0; k < width; k++) *(px++) = COLOR_COMPOSING_BACKDROP;
			}
			fontPrintChar(defaultFont, keyFontPalette, *codePtr, gfxCmpBuffer, SCREEN_WIDTH, SCREEN_WIDTH - width, 0);
			x = SCREEN_WIDTH;
		}
	}
}

void composeKey(u16 codepoint) {
	u16 *lastChar = compCursor - 1;
	if (codepoint == ENTER) {
		composing = FINISHED;
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
	dmaCopy(gfxCmpBuffer, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT - TEXT_HEIGHT)), sizeof(gfxCmpBuffer));
}

void keyboardInit() {
	videoSetModeSub(MODE_3_2D);
	vramSetBankC(VRAM_C_SUB_BG);
	bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	if (defaultFont.tileWidth == 0) fontLoadDefault();

	for (int i = 0; i < SCREEN_WIDTH * KEYBOARD_HEIGHT; i++) gfxKbdBuffer[i] = COLOR_KEYBOARD_BACKDROP;

	for (u8 i = 0; i < lengthof(boardModeSelect); i++) {
		KeyDef key = boardModeSelect[i];
		for (u8 y = 0; y < KEY_HEIGHT && key.y + y < KEYBOARD_HEIGHT-1; y++) {
			for (u8 x = 0; x < key.width; x++) {
				gfxKbdBuffer[key.x + x + (key.y + y) * SCREEN_WIDTH] = COLOR_KEY_NORMAL;
			}
		}
	}

	drawSelectedBoard();
}

void keyboardUpdate() {
	if (!showing) return;
	if (keysDown() & KEY_TOUCH) {
		touchPosition pos;
		touchRead(&pos);

		for (u8 i = 0; i < lengthof(boardModeSelect); i++) {
			KeyDef key = boardModeSelect[i];
			if (pos.px >= key.x && pos.px < key.x + key.width
			&& pos.py >= key.y + (SCREEN_HEIGHT - KEYBOARD_HEIGHT)
			&& pos.py < key.y + (SCREEN_HEIGHT - KEYBOARD_HEIGHT) + KEY_HEIGHT
			) {
				currentBoard = i;
				shiftToggle = ctrlToggle = altToggle = metaToggle = capsToggle = false;

				for (int i = 0; i < SCREEN_WIDTH * KEYBOARD_HEIGHT; i++) if (i % SCREEN_WIDTH > 16) gfxKbdBuffer[i] = COLOR_KEYBOARD_BACKDROP;

				drawSelectedBoard();
				dmaCopy(gfxKbdBuffer, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT)), sizeof(gfxKbdBuffer));
				return;
			}
		}

		for (u8 i = 0; i < boardSizes[currentBoard]; i++) {
			KeyDef key = boards[currentBoard][i];
			if (pos.px >= key.x && pos.px < key.x + key.width
			&& pos.py >= key.y + (SCREEN_HEIGHT - KEYBOARD_HEIGHT)
			&& pos.py < key.y + (SCREEN_HEIGHT - KEYBOARD_HEIGHT) + KEY_HEIGHT
			) {
				u16 codepoint = shiftToggle != capsToggle ? key.upper : key.lower;
				if (onPress != NULL) onPress(codepoint, key.name, shiftToggle, ctrlToggle, altToggle, metaToggle, capsToggle);
				heldKey = key;
				keyHeldTime = 1;
				return;
			}
		}
	}
	else if (keysHeld() & KEY_TOUCH) {
		if (keyHeldTime == 0) return;
		touchPosition pos;
		touchRead(&pos);
		
		if (pos.px >= heldKey.x && pos.px < heldKey.x + heldKey.width
			&& pos.py >= heldKey.y + (SCREEN_HEIGHT - KEYBOARD_HEIGHT)
			&& pos.py < heldKey.y + (SCREEN_HEIGHT - KEYBOARD_HEIGHT) + KEY_HEIGHT
		) {
			if (++keyHeldTime >= REPEAT_START && (keyHeldTime - REPEAT_START) % REPEAT_INTERVAL == 0) {
				u16 codepoint = shiftToggle != capsToggle ? heldKey.upper : heldKey.lower;
				if (onPress != NULL) onPress(codepoint, heldKey.name, shiftToggle, ctrlToggle, altToggle, metaToggle, capsToggle);
				if (composing == COMPOSING) composeKey(codepoint);
				keyHeldTime = REPEAT_START;
			}
		}
		else keyHeldTime = 1;
	}
	else if (keyHeldTime > 0) {
		bool shifted = false;
		bool updateBoard = true;
		if (heldKey.lower == SHIFT) {
			shiftToggle = !shiftToggle;
			shifted = true;
		}
		else if (heldKey.lower == CAPS_LOCK) {
			capsToggle = !capsToggle;
		}
		else if (heldKey.lower == HIRAGANA) {
			shiftToggle = false;
		}
		else if (heldKey.lower == KATAKANA) {
			shiftToggle = true;
		}
		else updateBoard = shiftToggle;

		u16 codepoint = shiftToggle != capsToggle ? heldKey.upper : heldKey.lower;
		if (onRelease != NULL) onRelease(codepoint, heldKey.name, shiftToggle, ctrlToggle, altToggle, metaToggle, capsToggle);
		if (composing == COMPOSING && keyHeldTime < REPEAT_START) composeKey(codepoint);

		if (currentBoard == 0 && !shifted) shiftToggle = false;
		if (updateBoard) {
			drawSelectedBoard();
			dmaCopy(gfxKbdBuffer, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT)), sizeof(gfxKbdBuffer));
		}

		keyHeldTime = 0;
		return;
	}
}

bool keyboardShow() {
	if (showing) return false;
	dmaCopy(gfxKbdBuffer, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT)), sizeof(gfxKbdBuffer));
	showing = true;
	return true;
}
bool keyboardHide() {
	if (!showing) return false;
	dmaFillHalfWords(0, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT)), sizeof(gfxKbdBuffer));
	showing = false;
	return true;
}

void keyboardSetPressHandler(void (*handler) (const u16 codepoint, const char *name, bool shift, bool ctrl, bool alt, bool meta, bool caps)) {
	onPress = handler;
}
void keyboardSetReleaseHandler(void (*handler) (const u16 codepoint, const char *name, bool shift, bool ctrl, bool alt, bool meta, bool caps)) {
	onRelease = handler;
}

void keyboardCompose() {
	composing = COMPOSING;
	closeOnAccept = keyboardShow();
	compCursor = compositionBuffer;
	drawComposedText();
	dmaCopy(gfxCmpBuffer, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT - TEXT_HEIGHT)), sizeof(gfxCmpBuffer));
}
ComposeStatus keyboardComposeStatus() {
	return composing;
}
void keyboardComposeAccept(char **strPtr, int *strSize) {
	composing = INACTIVE;
	if (closeOnAccept) keyboardHide();
	char *str = (char *) malloc((compCursor - compositionBuffer) * 3 + 1);
	*strPtr = str;
	int size = 0;
	for (u16 *codePtr = compositionBuffer; codePtr != compCursor; codePtr++) {
		u16 codepoint = *codePtr;
		if (codepoint < 0x80) {
			*(str++) = codepoint;
			size++;
		}
		else if (codepoint < 0x800) {
			*(str++) = 0xC0 | codepoint >> 6;
			*(str++) = BIT(7) | (codepoint & 0x3F);
			size += 2;
		}
		else {
			*(str++) = 0xE0 | codepoint >> 12;
			*(str++) = BIT(7) | (codepoint >> 6 & 0x3F);
			*(str++) = BIT(7) | (codepoint & 0x3F);
			size += 3;
		}
		*str = 0;
	}
	*strSize = size;
	dmaFillHalfWords(0, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT - TEXT_HEIGHT)), sizeof(gfxCmpBuffer));
}
void keyboardComposeCancel() {
	composing = INACTIVE;
	if (closeOnAccept) keyboardHide();
	dmaFillHalfWords(0, bgGetGfxPtr(7) + (SCREEN_WIDTH * (SCREEN_HEIGHT - KEYBOARD_HEIGHT - TEXT_HEIGHT)), sizeof(gfxCmpBuffer));
}