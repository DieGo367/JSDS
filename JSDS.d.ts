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
	assert(condition?: boolean, ...data: any[]): void;
	clear(): void;
	count(label?: string): void;
	countReset(label?: string): void;
	debug(...data: any[]): void;
	dir(...data: any[]): void;
	error(...data: any[]): void;
	group(...data: any[]): void;
	groupEnd(): void;
	info(...data: any[]): void;
	log(...data: any[]): void;
	table(tabularData?: any, properties?: string[]): void;
	time(label?: string): void;
	timeLog(label?: string, ...data: any[]): void;
	timeEnd(label?: string): void;
	trace(...data: any[]): void;
	warn(...data: any[]): void;
}
declare var console: Console;

interface Keyboard {
	/** Closes the on-screen keyboard. */
	hide(): void;
	/** Opens the on-screen keyboard. */
	show(): void;
}
declare var keyboard: Keyboard;

/** Encode or decode text from binary data. */
interface Text {
	encode(text: string, into?: Uint8Array): Uint8Array;
	decode(data: Uint8Array): string;
	encodeUTF16(text: string, into?: Uint8Array): Uint8Array;
	decodeUTF16(data: Uint8Array): string;
}
declare var Text: Text;

/** Encode or decode binary data from base64 strings. */
interface Base64 {
	encode(data: Uint8Array): string;
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
	 * Returns a `Uint8Array` with its size is determined by the number of bytes read.
	 * Returns `null` if the end-of-file was reached and no bytes were read.
	 */
	read(bytes: number): Uint8Array | null;
	/** Writes bytes to the file. Returns the number of bytes written. */
	write(data: Uint8Array): number;
	/**
	 * Seeks to a position in the file.
	 * The `seekMode` determines whether that position is relative to the start of the file, end of the file, or current position.
	 * Defaults to the start (aka "seekTo").
	 */
	seek(n: number, seekMode?: SeekMode): void;
	/** Closes the file. */
	close(): void;
}
declare var File: {
	prototype: File;
	new(): File;
	/**
	 * Opens a file and returns a new File instance.
	 * 
	 * `mode` determines whether to open the file with read and/or write access.
	 * Defaults to `"r"`, or read access.
	 */
	open(path: string, mode?: FileMode): File;
	/** Copies the contents of a file to a new location. */
	copy(sourcePath: string, destPath: string): void;
	/** Renames (aka moves) a file or directory. */
	rename(currentPath: string, newPath: string): void;
	/** Deletes a file or directory. */
	remove(path: string): void;
	/** Reads the entire contents of a file into a `Uint8Array`. */
	read(path: string): Uint8Array;
	/** Returns the entire contents of a file as a string. */
	readText(path: string): string;
	/** Writes the contents of a `Uint8Array` to a file. */
	write(path: string, data: Uint8Array): number;
	/** Writes a string to a file. */
	writeText(path: string, text: string): number;
	/** Creates a new directory. */
	makeDir(path: string, recursive?: boolean): void;
	/** Returns a list of the files and directories in the given path. */
	readDir(path: string): DirEntry[];
	/**
	 * Opens the file browser and asks the user to select a file.
	 * 
	 * The options argument can control various aspects of the browse menu. All of them are optional.
	 * 
	 * Option's path sets the directory to start browsing from (the user may navigate away from this directory). Defaults to the current working directory.
	 * 
	 * Option's extensions is an array of extension names to filter the file list by. If the list is empty or not included, all file types will appear in the list.
	 * 
	 * Option's message changes the text at the top of the browse menu.
	 */
	browse(options?: FileBrowseOptions): string | null;
};

/**
 * A storage interface that allows you to quickly and easily save and retrieve string data.
 * 
 * In JSDS, any JavaScript file you run, as distinguished by its filename (not the full path), is given its own storage file in `/_nds/JSDS/`.
 */
interface Storage {
	/** Returns the number of key/value pairs. */
	get length(): number;
	/** Returns the name of the nth key, or null if n is greater than or equal to the number of key/value pairs. */
	key(index: number): string | null;
	/** Returns the current value associated with the given key, or null if the given key does not exist. */
	getItem(key: string): string | null;
	/** Sets the value of the pair identified by key to value, creating a new key/value pair if none existed for key previously. */
	setItem(key: string, value: string): void;
	/** Removes the key/value pair with the given key, if a key/value pair with the given key exists. */
	removeItem(key: string): void;
	/** Removes all key/value pairs, if there are any. */
	clear(): void;
	/** Writes the current state of the storage object to the storage file. If the storage is empty, deletes the file. Returns `true` when successful. */
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
	 * Appends an event listener for events whose type attribute value is type. The callback argument sets the callback that will be invoked when the event is dispatched.
	 *
	 * The options argument sets listener-specific options.
	 *
	 * When set to `true`, options's once indicates that the callback will only be invoked once after which the event listener will be removed.
	 *
	 * The event listener is appended to target's event listener list and is not appended if it has the same type and callback.
	 */
	addEventListener(type: string, callback: EventListener | null, options?: AddEventListenerOptions): void;
	/** Dispatches a synthetic event event to target and returns `true` if either event's cancelable attribute value is `false` or its preventDefault() method was not invoked, and `false` otherwise. */
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
 * Appends an event listener for events whose type attribute value is type. The callback argument sets the callback that will be invoked when the event is dispatched.
 *
 * The options argument sets listener-specific options.
 *
 * When set to `true`, options's once indicates that the callback will only be invoked once after which the event listener will be removed.
 *
 * The event listener is appended to target's event listener list and is not appended if it has the same type and callback.
 */
declare function addEventListener<K extends keyof GlobalEventHandlersEventMap>(type: K, listener: (this: typeof globalThis, ev: GlobalEventHandlersEventMap[K]) => any, options?: boolean | AddEventListenerOptions): void;
declare function addEventListener(type: string, listener: EventListener, options?: AddEventListenerOptions): void;
/** Removes the event listener in target's event listener list with the same type and callback. */
declare function removeEventListener<K extends keyof GlobalEventHandlersEventMap>(type: K, listener: (this: typeof globalThis, ev: GlobalEventHandlersEventMap[K]) => any): void;
declare function removeEventListener(type: string, listener: EventListener): void;
/** Dispatches a synthetic event event to target and returns `true` if either event's cancelable attribute value is `false` or its preventDefault() method was not invoked, and `false` otherwise. */
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
	/** Returns `true` or `false` depending on how event was initialized. Its return value does not always carry meaning, but true can indicate that part of the operation during which event was dispatched, can be canceled by invoking the preventDefault() method. */
	readonly cancelable: boolean;
	/** Returns `true` if preventDefault() was invoked successfully to indicate cancelation, and `false` otherwise. */
	readonly defaultPrevented: boolean;
	/** Returns the object to which event is dispatched (its target). */
	readonly target: EventTarget | null;
	/** Returns the event's timestamp as the number of seconds measured relative to the time origin. */
	readonly timeStamp: number;
	/** Returns the type of event, e.g. "vblank", "buttondown", or "sleep". */
	readonly type: string;
	/** If invoked when the cancelable attribute value is `true`, signals to the operation that caused event to be dispatched that it needs to be canceled. */
	preventDefault(): void;
	/** Invoking this method prevents event from reaching any registered event listeners after the current one finishes running. */
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
	/** For most keys, this will be the value input by pressing the key. For special keys, returns the name of the key's action instead. */
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
	readonly button: string;
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
	/** Whether the user has set their DS to autoboot the cartridge. */
	readonly autoMode: boolean;
	/** The screen the user has selected for GBA mode. */
	readonly gbaScreen: Screen;
	/** The user's system language (as represented in that language). */
	readonly language: string;
}
interface ButtonsState {
	get A(): boolean;
	get B(): boolean;
	get X(): boolean;
	get Y(): boolean;
	get L(): boolean;
	get R(): boolean;
	get Up(): boolean;
	get Down(): boolean;
	get Left(): boolean;
	get Right(): boolean;
	get START(): boolean;
	get SELECT(): boolean;
}
interface Buttons {
	/** Buttons that were just pressed. */
	pressed: ButtonsState;
	/** Buttons that are currently held down. */
	held: ButtonsState;
	/** Buttons that were just released. */
	released: ButtonsState;
}
interface Touch {
	/** `true` if a touch has just begun. */
	get start(): boolean;
	/** `true` if the screen is being touched. */
	get active(): boolean;
	/** `true` if a touch has just ended. */
	get end(): boolean;
	/**
	 * Returns the current touch position.
	 * If the screen is not being touched, both numbers will be `NaN`.
	*/
	getPosition(): {x: number, y: number};
}
/** Home for DS-specifc functionality. */
interface DS {
	/** `true` if running in DSi mode. */
	readonly isDSiMode: boolean;
	
	/**
	 * Returns either a number or string corresponding to the battery level.
	 * In DSi Mode, returns `"charging"` while charging, or `0`-`4` otherwise.
	 * In DS Mode, returns `4` when okay and `1` when low.
	*/
	getBatteryLevel(): 0 | 1 | 2 | 3 | 4 | "charging";
	/** Returns which screen the main engine is currently on. */
	getMainScreen(): Screen;
	/** Sets the main engine to display on the given screen. */
	setMainScreen(screen: Screen): void;
	/** Switches the main and sub engine screens. */
	swapScreens(): void;
	/**
	 * Forces the DS to enter sleep mode.
	 * It will reawake when it is closed and opened again.
	 * Note: Sleep mode will not be entered if the `sleep` Event is canceled.
	 */
	sleep(): void;
	/** Turns off the DS. */
	shutdown(): void;

	/** User profile data. */
	profile: Profile;
	/** Button pressed/held/released states. */
	buttons: Buttons;
	/** Touch screen input data. */
	touch: Touch;
}
declare var DS: DS;

interface BetaAPI {
	gfxInit(): void;
	gfxRect(x: number, y: number, width: number, height: number, color: number): void;
	gfxPixel(x: number, y: number, color: number): void;
}
/** ⚠️ Temporary/unstable API(s) that are unfinished and will change later */
declare var beta: BetaAPI;