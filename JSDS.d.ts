/// <reference no-default-lib="true"/>
/// <reference lib="es2016"/>

declare var self: typeof globalThis;

declare function alert(message?: any): void;
declare function clearInterval(id: number | undefined): void;
declare function clearTimeout(id: number | undefined): void;
/** Stops the script and exits the app. */
declare function close(): void;
declare function confirm(message?: string): boolean;
declare function prompt(message?: string, defaultValue?: string): string | null;
declare function setInterval(handler: string | Function, timeout?: number, ...arguments: any[]): number;
declare function setTimeout(handler: string | Function, timeout?: number, ...arguments: any[]): number;

interface Console {
	log(...data: any[]): void;
	info(...data: any[]): void;
	warn(...data: any[]): void;
	error(...data: any[]): void;
	debug(...data: any[]): void;
	trace(...data: any[]): void;
	assert(condition?: boolean, ...data: any[]): void;
	count(label?: string): void;
	countReset(label?: string): void;
	time(label?: string): void;
	timeLog(label?: string, ...data: any[]): void;
	timeEnd(label?: string): void;
	group(...data: any[]): void;
	groupEnd(): void;
	dir(...data: any[]): void;
	table(tabularData?: any, properties?: string[]): void;
	clear(): void;
	set textColor(color: number | string);
	get textColor(): number;
	set textBackground(color: number | string);
	get textBackground(): number;
}
declare var console: Console;

interface Keyboard {
	/** Closes the on-screen keyboard. */
	hide(): void;
	/** Opens the on-screen keyboard. */
	show(): void;
	/** Allow buttons to control the keyboard. This is the case by default. */
	watchButtons(): void;
	/** Prevent buttons from controlling the keyboard. (Does not affect the keyboard during prompt()). */
	ignoreButtons(): void;
}
declare var keyboard: Keyboard;

/** Encode or decode text from binary data. */
interface Text {
	/**
	 * Encodes text into UTF-8 binary data.
	 * @param into A Uint8Array to encode the result into.
	 * A new one will be created if not provided.
	 * @throws If the size of the encoded text is too big for the provided Uint8Array.
	 * @returns The Uint8Array that was encoded into.
	 */
	encode(text: string, into?: Uint8Array): Uint8Array;
	/**
	 * Decodes a string from UTF-8 binary data.
	 * @throws If `data` is not valid UTF-8.
	 */
	decode(data: Uint8Array): string;
	/**
	 * Encodes text into UTF-16 binary data.
	 * @param into A Uint8Array to encode the result into.
	 * A new one will be created if not provided.
	 * @throws If the size of the encoded text is too big for the provided Uint8Array.
	 * @returns The Uint8Array that was encoded into.
	 */
	encodeUTF16(text: string, into?: Uint8Array): Uint8Array;
	/**
	 * Decodes a string from UTF-16 binary data.
	 * @throws If `data` is not valid UTF-16.
	 */
	decodeUTF16(data: Uint8Array): string;
}
declare var Text: Text;

/** Encode or decode binary data from base64 strings. */
interface Base64 {
	/** Encodes binary data into base64 ascii text. */
	encode(data: Uint8Array): string;
	/**
	 * Decodes binary data from a base64 string.
	 * @param into A Uint8Array to decode the result into.
	 * A new one will be created if not provided.
	 * @throws If `base64` is not a valid base64 string.
	 * @throws If the size of the decoded data is too big for the provided Uint8Array.
	 * @returns The Uint8Array that was decoded into.
	 */
	decode(base64: string, into?: Uint8Array): Uint8Array;
}
declare var Base64: Base64;

/** File open modes for File.open(). */
type FileMode = "r" | "w" | "a" | "r+" | "w+" | "a+";
/** Allowed seek modes for File.prototype.seek(). */
type SeekMode = "start" | "current" | "end";
/** Information about a file or directory. */
interface DirEntry {
	isDirectory: boolean;
	isFile: boolean;
	name: string;
}
interface FileBrowseOptions {
	path?: string;
	extensions?: string[];
	message?: string;
}
/**
 * For reading, writing, and managing files and directories.
 * 
 * This is nothing like the File class you may know from the web.
 */
interface File {
	/**
	 * Reads a certain amount of bytes from the file.
	 * @returns A `Uint8Array` with its size is determined by the number of bytes read,
	 * or `null` if the end-of-file was reached and no bytes were read.
	 * @throws If the current file mode doesn't support reading, or the read fails otherwise.
	 */
	read(bytes: number): Uint8Array | null;
	/**
	 * Writes bytes to the file.
	 * @returns The number of bytes written.
	 * @throws If the current file mode doesn't support writing, or the write fails otherwise.
	 */
	write(data: Uint8Array): number;
	/**
	 * Seeks to a position in the file.
	 * @param seekMode Determines whether that position is relative to the start of the file,
	 * end of the file, or current position.
	 * Defaults to `"start"`.
	 * @throws If the seek fails or a bad seek mode is given.
	 */
	seek(n: number, seekMode: SeekMode): void;
	/**
	 * Closes the file.
	 * @throws If failed.
	 */
	close(): void;
}
declare var File: {
	prototype: File;
	new(): File;
	/**
	 * Opens a file and returns a new File instance.
	 * 
	 * @param mode Determines whether to open the file with read and/or write access.
	 * Defaults to `"r"` (read-only access).
	 * @throws If the file fails top open or a bad file mode is given.
	 */
	open(path: string, mode?: FileMode): File;
	/**
	 * Copies the contents of a file to a new location.
	 * @throws If failed.
	 */
	copy(sourcePath: string, destPath: string): void;
	/**
	 * Renames (aka moves) a file or directory.
	 * @throws If failed.
	 */
	rename(currentPath: string, newPath: string): void;
	/**
	 * Deletes a file or directory.
	 * @throws If failed.
	 */
	remove(path: string): void;
	/**
	 * Reads the entire contents of a file into a `Uint8Array`.
	 * @throws If failed.
	 */
	read(path: string): Uint8Array;
	/**
	 * Returns the entire contents of a file as a string.
	 * @throws If failed.
	 */
	readText(path: string): string;
	/**
	 * Writes the contents of a `Uint8Array` to a file.
	 * @throws If failed.
	 */
	write(path: string, data: Uint8Array): number;
	/**
	 * Writes a string to a file.
	 * @throws If failed.
	 */
	writeText(path: string, text: string): number;
	/**
	 * Creates a new directory.
	 * @param recursive Whether to recursively create
	 * the nested directories containing the target directory.
	 * @throws If failed.
	 */
	makeDir(path: string, recursive?: boolean): void;
	/** Returns a list of the files and directories in the given path. */
	readDir(path: string): DirEntry[];
	/**
	 * Opens the file browser and asks the user to select a file.
	 * 
	 * @param options Controls various aspects of the browse menu. All of them are optional.
	 * 
	 * @param options.path The directory to start browsing from
	 * (the user may navigate away from this directory).
	 * Defaults to the current working directory.
	 * 
	 * @param options.extensions Array of extension names to filter the file list by.
	 * If the list is empty or not included, all file types will appear in the list.
	 * 
	 * @param options.message Changes the text at the top of the browse menu.
	 */
	browse(options?: FileBrowseOptions): string | null;
};

/**
 * Allows you to quickly and easily save and retrieve string data.
 * 
 * In JSDS, any JavaScript file you run, as distinguished by its filename (not the full path),
 * is given its own storage file in `/_nds/JSDS/`.
 */
interface Storage {
	/** The number of key/value pairs. */
	get length(): number;
	/**
	 * @returns Name of the key at the given `index`,
	 * or `null` if `index` doesn't match a key/value pair.
	 */
	key(index: number): string | null;
	/** @returns The value associated with the given key, or `null` if the given key does not exist. */
	getItem(key: string): string | null;
	/** Sets the value identified by the key, creating a new key/value pair if necessary. */
	setItem(key: string, value: string): void;
	/** Removes the key/value pair with the given key, if one exists. */
	removeItem(key: string): void;
	/** Removes all key/value pairs, if there are any. */
	clear(): void;
	/**
	 * Writes the current state of the storage object to the storage file.
	 * If the storage is empty, deletes the file.
	 * @returns `true` when successful, `false` otherwise.
	 */
	save(): boolean;
}
declare var storage: Storage;

interface EventListener {
	(evt: Event): void;
}
interface AddEventListenerOptions {
	once?: boolean;
}
/** Objects that can listen for and receive events. */
interface EventTarget {
	/**
	 * Appends an event listener for events whose type attribute value is `type`.
	 * The event listener is not appended if it has the same type and callback as an existing listener.
	 * 
	 * @param callback The callback that will be invoked when the event is dispatched.
	 *
	 * @param options Sets listener-specific options.
	 *
	 * @param options.once When `true`,
	 * indicates that the listener should be removed after being invoked once.
	 */
	addEventListener(type: string, callback: EventListener | null, options?: AddEventListenerOptions): void;
	/**
	 * Dispatches a synthetic event event to target.
	 * @returns `true` if either event's cancelable attribute value is `false`
	 * or its preventDefault() method was not invoked, and `false` otherwise.
	 */
	dispatchEvent(event: Event): boolean;
	/** Removes the event listener in target's event listener list with the same type and callback. */
	removeEventListener(type: string, callback: EventListener | null): void;
}
declare var EventTarget: {
	prototype: EventTarget;
	new(): EventTarget;
};

interface GlobalEventHandlersEventMap {
	"vblank": Event;
	"sleep": Event;
	"wake": Event;
	"error": ErrorEvent;
	"unhandledrejection": PromiseRejectionEvent;
	"keydown": KeyboardEvent;
	"keyup": KeyboardEvent;
	"buttondown": ButtonEvent;
	"buttonup": ButtonEvent;
	"touchstart": TouchEvent;
	"touchmove": TouchEvent;
	"touchend": TouchEvent;
}
/**
 * Appends an event listener for events whose type attribute value is `type`.
 * The event listener is not appended if it has the same type and callback as an existing listener.
 * 
 * @param callback The callback that will be invoked when the event is dispatched.
 *
 * @param options Sets listener-specific options.
 *
 * @param options.once When `true`,
 * indicates that the listener should be removed after being invoked once.
 */
declare function addEventListener<K extends keyof GlobalEventHandlersEventMap>(type: K, listener: (this: typeof globalThis, ev: GlobalEventHandlersEventMap[K]) => any, options?: boolean | AddEventListenerOptions): void;
declare function addEventListener(type: string, listener: EventListener, options?: AddEventListenerOptions): void;
/** Removes the event listener in target's event listener list with the same type and callback. */
declare function removeEventListener<K extends keyof GlobalEventHandlersEventMap>(type: K, listener: (this: typeof globalThis, ev: GlobalEventHandlersEventMap[K]) => any): void;
declare function removeEventListener(type: string, listener: EventListener): void;
/**
 * Dispatches a synthetic event event to target.
 * @returns `true` if either event's cancelable attribute value is `false`
 * or its preventDefault() method was not invoked, and `false` otherwise.
 */
declare function dispatchEvent(event: Event): boolean;

declare var onvblank: ((this: typeof globalThis, ev: Event) => any) | null;
declare var onsleep: ((this: typeof globalThis, ev: Event) => any) | null;
declare var onwake: ((this: typeof globalThis, ev: Event) => any) | null;
declare var onerror: ((this: typeof globalThis, ev: ErrorEvent) => any) | null;
declare var onunhandledrejection: ((this: typeof globalThis, ev: PromiseRejectionEvent) => any) | null;
declare var onkeydown: ((this: typeof globalThis, ev: KeyboardEvent) => any) | null;
declare var onkeyup: ((this: typeof globalThis, ev: KeyboardEvent) => any) | null;
declare var onbuttondown: ((this: typeof globalThis, ev: ButtonEvent) => any) | null;
declare var onbuttonup: ((this: typeof globalThis, ev: ButtonEvent) => any) | null;
declare var ontouchstart: ((this: typeof globalThis, ev: TouchEvent) => any) | null;
declare var ontouchmove: ((this: typeof globalThis, ev: TouchEvent) => any) | null;
declare var ontouchend: ((this: typeof globalThis, ev: TouchEvent) => any) | null;

interface EventInit {
	cancelable?: boolean;
}
interface Event {
	/** Can be canceled by invoking the preventDefault() method. */
	readonly cancelable: boolean;
	/** `true` if the event was canceled, and `false` otherwise. */
	readonly defaultPrevented: boolean;
	/** The object to which event is dispatched (its target). */
	readonly target: EventTarget | null;
	/** The event's timestamp as the number of seconds measured relative to the time origin. */
	readonly timeStamp: number;
	/** The type of event, e.g. "vblank", "buttondown", or "sleep". */
	readonly type: string;
	/** If the event is canceleable, cancels the operation that caused the event. */
	preventDefault(): void;
	/** Prevents the event from reaching any other registered event listeners after this one. */
	stopImmediatePropagation(): void;
}
declare var Event: {
	prototype: Event;
	new(type: string, eventInitDict?: EventInit): Event;
};

/** Event providing information related to an error in a timeout or event listener. */
interface ErrorEvent extends Event {
	readonly error: any;
	readonly filename: string;
	readonly lineno: number;
	readonly message: string;
}
/** Event providing information related to an unhandled promise rejection. */
interface PromiseRejectionEvent extends Event {
	readonly promise: Promise<any>;
	readonly reason: any;
}
/** Events that describe a user interaction with the on-screen keyboard. */
interface KeyboardEvent extends Event {
	/**
	 * For most keys, this will be the value input by pressing the key.
	 * For special keys, returns the name of the key's action instead.
	 */
	readonly code: string;
	/** The name of the key that was pressed. */
	readonly key: string;
	/** `true` when this event is fired as a result of holding the key down continually. */
	readonly repeat: boolean;
	/** `true` if the keyboard is in the shifted state. */
	readonly shifted: boolean;
	/** The name of the current keyboard layout. */
	readonly layout: string;
}
/** Events that describe a button press or release. */
interface ButtonEvent extends Event {
	readonly button: ButtonName;
}
/** Events that describe touch screen interaction. */
interface TouchEvent extends Event {
	readonly x: number;
	readonly y: number;
	readonly dx: number;
	readonly dy: number;
}

/** Values representing either the top or bottom screen. */
type Screen = "bottom" | "top";

/** Home for DS-specifc functionality. */
interface DS {
	/** `true` if running in DSi mode. */
	readonly isDSiMode: boolean;
	
	/**
	 * @returns Either a number or string corresponding to the battery level.
	 * In DSi Mode, returns `"charging"` while charging, or `0`-`4` otherwise.
	 * In DS Mode, returns `4` when okay and `1` when low.
	*/
	getBatteryLevel(): 0 | 1 | 2 | 3 | 4 | "charging";
	/** @returns The screen the main engine is currently on. */
	getMainScreen(): Screen;
	/**
	 * Sets the main engine to display on the given screen.
	 * @throws If a bad screen value is given.
	 */
	setMainScreen(screen: Screen): void;
	/** Switches the main and sub engine screens. */
	swapScreens(): void;
	/**
	 * Forces the DS to enter sleep mode.
	 * It will reawake when it is closed and opened again.
	 * 
	 * Note: Sleep mode will not be entered if the `sleep` Event is canceled.
	 */
	sleep(): void;
	/** Turns off the DS. */
	shutdown(): void;
}
declare var DS: DS;

/** User profile data. */
interface Profile {
	/** The hour (0-23) that the user's alarm is set to. */
	readonly alarmHour: number;
	/** The minute (0-59) that the user's alarm is set to. */
	readonly alarmMinute: number;
	/** The user's birth day (1-31). */
	readonly birthDay: number;
	/** The user's birth month (1-12). */
	readonly birthMonth: number;
	/** The user's name. */
	readonly name: string;
	/** The user's personal message. */
	readonly message: string;
	/** The user's theme color, as a BGR15 color value. */
	readonly color: number;
	/** `true` if the user has set their DS to autoboot the cartridge. */
	readonly autoMode: boolean;
	/** The screen the user has selected for GBA mode. */
	readonly gbaScreen: Screen;
	/** The user's system language (as represented in that language). */
	readonly language: string;
}
declare var Profile: Profile;

type ButtonName = "A" | "B" | "X" | "Y" | "L" | "R" | "Up" | "Down" | "Left" | "Right" | "START" | "SELECT";
interface ButtonState {
	/** Button was just pressed. */
	get pressed(): boolean;
	/** Button is held down. */
	get held(): boolean;
	/** Button was just released. */
	get released(): boolean;
}
/** Button pressed/held/released states. */
type Button = Record<ButtonName, ButtonState>;
declare var Button: Button;

/** Touch screen input data. */
interface Touch {
	/** `true` if a touch has just begun. */
	get start(): boolean;
	/** `true` if the screen is being touched. */
	get active(): boolean;
	/** `true` if a touch has just ended. */
	get end(): boolean;
	/**
	 * @returns The current touch position.
	 * If the screen is not being touched, both numbers will be `NaN`.
	*/
	getPosition(): {x: number, y: number};
}
declare var Touch: Touch;

interface VRAM_TYPES {
	readonly LCD: unique symbol;
	readonly ARM7_0: unique symbol;
	readonly ARM7_1: unique symbol;
	readonly MAIN_BG: unique symbol;
	readonly MAIN_BG_0x4000: unique symbol;
	readonly MAIN_BG_0x10000: unique symbol;
	readonly MAIN_BG_0x14000: unique symbol;
	readonly MAIN_BG_0x20000: unique symbol;
	readonly MAIN_BG_0x40000: unique symbol;
	readonly MAIN_BG_0x60000: unique symbol;
	readonly MAIN_SPRITE: unique symbol;
	readonly MAIN_SPRITE_0x4000: unique symbol;
	readonly MAIN_SPRITE_0x10000: unique symbol;
	readonly MAIN_SPRITE_0x14000: unique symbol;
	readonly MAIN_SPRITE_0x20000: unique symbol;
	readonly MAIN_BG_EXT_PALETTE_0: unique symbol;
	readonly MAIN_BG_EXT_PALETTE_2: unique symbol;
	readonly MAIN_SPRITE_EXT_PALETTE: unique symbol;
	readonly SUB_BG: unique symbol;
	readonly SUB_BG_0x8000: unique symbol;
	readonly SUB_SPRITE: unique symbol;
	readonly SUB_BG_EXT_PALETTE: unique symbol;
	readonly SUB_SPRITE_EXT_PALETTE: unique symbol;
	readonly TEXTURE_0: unique symbol;
	readonly TEXTURE_1: unique symbol;
	readonly TEXTURE_2: unique symbol;
	readonly TEXTURE_3: unique symbol;
	readonly TEXTURE_PALETTE_0: unique symbol;
	readonly TEXTURE_PALETTE_1: unique symbol;
	readonly TEXTURE_PALETTE_4: unique symbol;
	readonly TEXTURE_PALETTE_5: unique symbol;
}
interface VRAM_A_TYPES {
	LCD: VRAM_TYPES["LCD"];
	MAIN_BG: VRAM_TYPES["MAIN_BG"];
	MAIN_BG_0x20000: VRAM_TYPES["MAIN_BG_0x20000"];
	MAIN_BG_0x40000: VRAM_TYPES["MAIN_BG_0x40000"];
	MAIN_BG_0x60000: VRAM_TYPES["MAIN_BG_0x60000"];
	MAIN_SPRITE: VRAM_TYPES["MAIN_SPRITE"];
	MAIN_SPRITE_0x20000: VRAM_TYPES["MAIN_SPRITE_0x20000"];
	TEXTURE_0: VRAM_TYPES["TEXTURE_0"];
	TEXTURE_1: VRAM_TYPES["TEXTURE_1"];
	TEXTURE_2: VRAM_TYPES["TEXTURE_2"];
	TEXTURE_3: VRAM_TYPES["TEXTURE_3"];
}
interface VRAM_B_TYPES extends VRAM_A_TYPES {}
interface VRAM_C_TYPES {
	LCD: VRAM_TYPES["LCD"];
	ARM7_0: VRAM_TYPES["ARM7_0"];
	ARM7_1: VRAM_TYPES["ARM7_1"];
	MAIN_BG: VRAM_TYPES["MAIN_BG"];
	MAIN_BG_0x20000: VRAM_TYPES["MAIN_BG_0x20000"];
	MAIN_BG_0x40000: VRAM_TYPES["MAIN_BG_0x40000"];
	MAIN_BG_0x60000: VRAM_TYPES["MAIN_BG_0x60000"];
	SUB_BG: VRAM_TYPES["SUB_BG"];
	TEXTURE_0: VRAM_TYPES["TEXTURE_0"];
	TEXTURE_1: VRAM_TYPES["TEXTURE_1"];
	TEXTURE_2: VRAM_TYPES["TEXTURE_2"];
	TEXTURE_3: VRAM_TYPES["TEXTURE_3"];
}
interface VRAM_D_TYPES {
	LCD: VRAM_TYPES["LCD"];
	ARM7_0: VRAM_TYPES["ARM7_0"];
	ARM7_1: VRAM_TYPES["ARM7_1"];
	MAIN_BG: VRAM_TYPES["MAIN_BG"];
	MAIN_BG_0x20000: VRAM_TYPES["MAIN_BG_0x20000"];
	MAIN_BG_0x40000: VRAM_TYPES["MAIN_BG_0x40000"];
	MAIN_BG_0x60000: VRAM_TYPES["MAIN_BG_0x60000"];
	SUB_SPRITE: VRAM_TYPES["SUB_SPRITE"];
	TEXTURE_0: VRAM_TYPES["TEXTURE_0"];
	TEXTURE_1: VRAM_TYPES["TEXTURE_1"];
	TEXTURE_2: VRAM_TYPES["TEXTURE_2"];
	TEXTURE_3: VRAM_TYPES["TEXTURE_3"];
}
interface VRAM_E_TYPES {
	LCD: VRAM_TYPES["LCD"];
	MAIN_BG: VRAM_TYPES["MAIN_BG"];
	MAIN_SPRITE: VRAM_TYPES["MAIN_SPRITE"];
	MAIN_BG_EXT_PALETTE_0: VRAM_TYPES["MAIN_BG_EXT_PALETTE_0"];
	TEXTURE_PALETTE_0: VRAM_TYPES["TEXTURE_PALETTE_0"];
}
interface VRAM_F_TYPES {
	LCD: VRAM_TYPES["LCD"];
	MAIN_BG: VRAM_TYPES["MAIN_BG"];
	MAIN_BG_0x4000: VRAM_TYPES["MAIN_BG_0x4000"];
	MAIN_BG_0x10000: VRAM_TYPES["MAIN_BG_0x10000"];
	MAIN_BG_0x14000: VRAM_TYPES["MAIN_BG_0x14000"];
	MAIN_SPRITE: VRAM_TYPES["MAIN_SPRITE"];
	MAIN_SPRITE_0x4000: VRAM_TYPES["MAIN_SPRITE_0x4000"];
	MAIN_SPRITE_0x10000: VRAM_TYPES["MAIN_SPRITE_0x10000"];
	MAIN_SPRITE_0x20000: VRAM_TYPES["MAIN_SPRITE_0x20000"];
	MAIN_BG_EXT_PALETTE_0: VRAM_TYPES["MAIN_BG_EXT_PALETTE_0"];
	MAIN_BG_EXT_PALETTE_2: VRAM_TYPES["MAIN_BG_EXT_PALETTE_2"];
	MAIN_SPRITE_EXT_PALETTE: VRAM_TYPES["MAIN_SPRITE_EXT_PALETTE"];
	TEXTURE_PALETTE_0: VRAM_TYPES["TEXTURE_PALETTE_0"];
	TEXTURE_PALETTE_1: VRAM_TYPES["TEXTURE_PALETTE_1"];
	TEXTURE_PALETTE_4: VRAM_TYPES["TEXTURE_PALETTE_4"];
	TEXTURE_PALETTE_5: VRAM_TYPES["TEXTURE_PALETTE_5"];
}
interface VRAM_G_TYPES extends VRAM_F_TYPES {}
interface VRAM_H_TYPES {
	LCD: VRAM_TYPES["LCD"];
	SUB_BG: VRAM_TYPES["SUB_BG"];
	SUB_BG_EXT_PALETTE: VRAM_TYPES["SUB_BG_EXT_PALETTE"];
}
interface VRAM_I_TYPES {
	LCD: VRAM_TYPES["LCD"];
	SUB_BG_0x8000: VRAM_TYPES["SUB_BG_0x8000"];
	SUB_SPRITE: VRAM_TYPES["SUB_SPRITE"];
	SUB_SPRITE_EXT_PALETTE: VRAM_TYPES["SUB_SPRITE_EXT_PALETTE"];
}
type VRAM_A = VRAM_A_TYPES[keyof VRAM_A_TYPES];
type VRAM_B = VRAM_B_TYPES[keyof VRAM_B_TYPES];
type VRAM_C = VRAM_C_TYPES[keyof VRAM_C_TYPES];
type VRAM_D = VRAM_D_TYPES[keyof VRAM_D_TYPES];
type VRAM_E = VRAM_E_TYPES[keyof VRAM_E_TYPES];
type VRAM_F = VRAM_F_TYPES[keyof VRAM_F_TYPES];
type VRAM_G = VRAM_G_TYPES[keyof VRAM_G_TYPES];
type VRAM_H = VRAM_H_TYPES[keyof VRAM_H_TYPES];
type VRAM_I = VRAM_I_TYPES[keyof VRAM_I_TYPES];
declare var VRAM: {
	A: VRAM_A_TYPES;
	B: VRAM_B_TYPES;
	C: VRAM_C_TYPES;
	D: VRAM_D_TYPES;
	E: VRAM_E_TYPES;
	F: VRAM_F_TYPES;
	G: VRAM_G_TYPES;
	H: VRAM_H_TYPES;
	I: VRAM_I_TYPES;
	setBankA(mode: VRAM_A): void;
	setBankB(mode: VRAM_B): void;
	setBankC(mode: VRAM_C): void;
	setBankD(mode: VRAM_D): void;
	setBankE(mode: VRAM_E): void;
	setBankF(mode: VRAM_F): void;
	setBankG(mode: VRAM_G): void;
	setBankH(mode: VRAM_H): void;
	setBankI(mode: VRAM_I): void;
};

declare var Video: {
	main: {
		setBackdropColor(color: number | string): void;
		setMode(mode: 0 | 1 | 2 | 3 | 4 | 5 | 6, is3D?: boolean): void;
	};
	sub: {
		setBackdropColor(color: number | string): void;
		setMode(mode: 0 | 1 | 2 | 3 | 4 | 5): void;
	};
};

/** An object associated with some allocated graphics memory. Can supply its graphics to one or more sprites. */
interface SpriteGraphic {
	/** Bitdepth of the graphics. If the value is `16` the image is a bitmap, otherwise it is paletted. */
	readonly colorFormat: 4 | 8 | 16;
	/** Width in pixels of the graphics data. */
	readonly width: number;
	/** Height in pixels of the graphics data. */
	readonly height: number;
	/** A `Uint8Array` providing direct access to the allocated graphics memory. */
	readonly data: Uint8Array;
	/** Frees the graphics memory associated with this object. This object is rendered useless and will error upon further usage. */
	remove(): void;
}
declare var SpriteGraphic: {
	prototype: SpriteGraphic;
};
interface PalettedSpriteGraphic extends SpriteGraphic {readonly colorFormat: 4 | 8}
interface BitmapSpriteGraphic extends SpriteGraphic {readonly colorFormat: 16}
type SpriteGraphicFromColorFormat<C extends SpriteGraphic["colorFormat"]> = 
	C extends PalettedSpriteGraphic["colorFormat"] ? PalettedSpriteGraphic :
	C extends BitmapSpriteGraphic["colorFormat"] ? BitmapSpriteGraphic :
	SpriteGraphic;
/** An affine matrix object that controls sprite transformations. Can be applied to one or more sprites to transform them all together. */
interface SpriteAffineMatrix {
	/** X position for the horizontal unit vector. `1` in the identity matrix. */
	hdx: number;
	/** Y position for the horizontal unit vector. `0` in the identity matrix. */
	hdy: number;
	/** X position for the vertical unit vector. `1` in the identity matrix. */
	vdx: number;
	/** Y position for the vertical unit vector. `1` in the identity matrix. */
	vdy: number;
	/**
	 * Sets the matrix to a specific rotation and scale.
	 * @param angle Rotation angle in degrees.
	 * @param sx Inverse X scale.
	 * @param sy Inverse Y scale.
	 */
	rotateScale(angle: number, sx: number, sy: number): void;
	/** Clears the affine matrix slot associated with this object and marks it as open. This object is rendered useless and will error upon further usage. */
	remove(): void;
}
declare var SpriteAffineMatrix: {
	prototype: SpriteAffineMatrix;
};
/** An object asssociated with a given sprite slot. */
interface Sprite {
	/** Screen x position for the left edge of the sprite. */
	x: number;
	/** Screen y position for the top edge of the sprite. */
	y: number;
	setPosition(x: number, y: number): void;
	/** A graphics object linking to allocated graphics memory. */
	gfx: SpriteGraphic;
	/** Rendering priority relative to other sprites. 0 = top, 3 = bottom. */
	priority: 0 | 1 | 2 | 3;
	/** Makes the sprite invisible. */
	hidden: boolean;
	/** Flips sprite horizontally. This value is ignored if the sprite uses an affine matrix. */
	flipH: boolean;
	/** Flips sprite vertically. This value is ignored if the sprite uses an affine matrix. */
	flipV: boolean;
	/**
	 * An affine matrix object that can transform the sprite visual. May be shared between multiple sprites.
	 * 
	 * When set to `null`, the sprite is a standard sprite that does not use affine transformations.
	 */
	affine: SpriteAffineMatrix | null;
	/** Doubles the sprite's view window while using an affine matrix. */
	sizeDouble: boolean;
	/** Enables mosaic mode for this sprite. Uses the mosaic size from the corresponding sprite engine. */
	mosaic: boolean;
	/** Clears the sprite slot associated with this object and marks it as open. This object is rendered useless and will error upon further usage. */
	remove(): void;
}
/** Sprites using paletted graphics. */
interface PalettedSprite extends Sprite {
	/** Which sprite palette to use (0-15). */
	palette: number;
	gfx: PalettedSpriteGraphic;
}
declare var PalettedSprite: {
	prototype: PalettedSprite;
}
/** Sprites using bitmap graphics. */
interface BitmapSprite extends Sprite {
	/** Alpha value of the sprite (0 = transparent, nonzero = opaque). */
	alpha: number;
	gfx: BitmapSpriteGraphic;
}
declare var BitmapSprite: {
	prototype: BitmapSprite;
}
type SpriteFromGraphic<G extends SpriteGraphic> = Extract<PalettedSprite | BitmapSprite, Sprite & {gfx: Omit<G, "main">}>;
type MainEngine<B> = {main: B};
interface SpriteEngine<M> {
	/**
	 * Initializes this sprite engine to start displaying graphics.
	 * @param allowBitmaps Whether to allow bitmap sprite graphic data on this engine.
	 * @param use2DMapping Map tile graphics in 2D.
	 * @param boundarySize Offset size between graphics entries.
	 * For bitmap sprites, this should be `128` or `256`.
	 * For 2D-mapped paletted sprites, this must be `32`.
	 * For 1D-mapped paletted sprites, this should be `32`, `64`, `128`, or `256`.
	 * @param useExtendedPalettes Enable use of extended sprite palettes for 8 BPP graphics.
	 */
	init(allowBitmaps?: boolean, use2DMapping?: boolean, boundarySize?: 32 | 64 | 128 | 256, useExtendedPalettes?: boolean): void;
	/** Enables sprite rendering from this engine. */
	enable(): void;
	/** Disables sprite rendering from this engine. */
	disable(): void;
	/**
	 * Adds a new sprite on this sprite engine.
	 * @param x X position of the left edge of the sprite. 0 = left of the screen, 256 = right.
	 * @param y Y position of the top edge of the sprite. 0 = top of the screen, 192 = bottom.
	 * @param gfx A graphics object linking to allocated graphics memory. The color format of the graphics object will control whether this is a paletted or bitmap sprite.
	 * @param paletteOrAlpha For paletted sprites, the sprite palette number to use (0-15). For bitmap sprites, the alpha value of the sprite (0 = transparent, nonzero = opaque).
	 * @param priority Rendering priority relative to other sprites. 0 = top, 3 = bottom.
	 * @param hide Makes the sprite invisible.
	 * @param flipH Flips sprite horizontally. This value is ignored if the sprite uses an affine matrix.
	 * @param flipV Flips sprite vertically. This value is ignored if the sprite uses an affine matrix.
	 * @param affine An affine matrix object that can transform the sprite visual. By default or when set to `null`, the sprite will not use affine transformations.
	 * @param sizeDouble Doubles the sprite's view window while using an affine matrix.
	 * @param mosaic Enables mosaic mode for this sprite. You can set the mosaic values from `setMosaic` on this sprite engine.
	 * @returns A `Sprite` object associated with the newly created sprite which allows access to its parameters.
	 */
	addSprite<G extends SpriteGraphic & MainEngine<M>>(x: number, y: number, gfx: G, paletteOrAlpha?: number, priority?: Sprite["priority"], hide?: boolean, flipH?: boolean, flipV?: boolean, affine?: SpriteAffineMatrix | null, sizeDouble?: boolean, mosaic?: boolean): SpriteFromGraphic<G> & MainEngine<M>;
	/**
	 * Allocates some graphics memory and returns an object that can supply graphics to a sprite.
	 * @param width Width of the graphics. May be rounded up to the nearest valid size. Maximum is 64.
	 * @param height Height of the graphics. May be rounded up to the nearest valid size. Maximum is 64.
	 * @param colorFormat Bitdepth of the graphics. `16` bits per pixel graphics are bitmap, otherwise the graphics are paletted.
	 * @param data An optional `Uint8Array` that will be copied into the newly allocated graphics data.
	 * @returns A `SpriteGraphic` object which can be supplied when creating a sprite, or be assigned to an existing one. Can be shared across multiple sprites.
	 */
	addGraphic<C extends SpriteGraphic["colorFormat"]>(width: number, height: number, colorFormat: C, data?: Uint8Array): SpriteGraphicFromColorFormat<C> & MainEngine<M>;
	/**
	 * Adds a new affine matrix on this sprite engine that can transform sprites.
	 * @param hdx X position for the horizontal unit vector. Defaults to `1`.
	 * @param hdy Y position for the horizontal unit vector. Defaults to `0`.
	 * @param vdx X position for the vertical unit vector. Defaults to `0`.
	 * @param vdy Y position for the vertical unit vector. Defaults to `1`.
	 * @returns A `SpriteAffineMatrix` object which can be supplied when creating a sprite, or be assigned to an existing one. Can be shared across multiple sprites.
	 */
	addAffineMatrix(hdx?: number, hdy?: number, vdx?: number, vdy?: number): SpriteAffineMatrix & MainEngine<M>;
	/**
	 * Sets the engine-wide mosaic values for sprites that have mosaic mode enabled.
	 */
	setMosaic(dx: number, dy: number): void;
	/** Palette data for the sprites in this engine. */
	readonly palette: Uint16Array;
}
declare var Sprite: {
	prototype: Sprite;
	main: SpriteEngine<true>;
	sub: SpriteEngine<false>;
};

interface BetaAPI {
	gfxInit(): void;
	gfxRect(x: number, y: number, width: number, height: number, color: number): void;
	gfxPixel(x: number, y: number, color: number): void;
}
/** ⚠️ Temporary/unstable API(s) that are unfinished and will change later */
declare var beta: BetaAPI;