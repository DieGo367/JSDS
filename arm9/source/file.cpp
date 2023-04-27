#include "file.hpp"

#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <fat.h>
#include <nds/arm9/background.h>
#include <nds/arm9/cache.h>
#include <nds/arm9/input.h>
#include <nds/interrupts.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>

#include "io/console.hpp"
#include "util/color.hpp"
#include "util/font.hpp"
#include "util/helpers.hpp"
#include "util/tonccpy.h"



JS_class ref_File;
jerry_value_t ref_storage;
char storagePath[PATH_MAX];

void onFileFree(void *file) {
	fclose((FILE *) file);
}
jerry_object_native_info_t fileNativeInfo = {.free_cb = onFileFree};


bool sortDirectoriesFirst(dirent left, dirent right) {
	return (left.d_type != right.d_type && left.d_type == DT_DIR) || tolower(left.d_name[0]) < tolower(right.d_name[0]);
}

char *fileBrowse(NitroFont font, const char *message, const char *path, std::vector<char *> extensions, bool replText) {
	char oldPath[PATH_MAX];
	getcwd(oldPath, PATH_MAX);
	if (chdir(path) != 0) return NULL;
	char curPath[PATH_MAX];
	getcwd(curPath, PATH_MAX);

	bool sdFound = access("sd:/", F_OK) == 0;
	bool fatFound = access("fat:/", F_OK) == 0;

	const u32 bufferLen = SCREEN_WIDTH * SCREEN_HEIGHT;
	const u32 bufferSize = bufferLen * sizeof(u16);
	u16 *gfx = (u16 *) malloc(2 * bufferSize);
	u16 *vramCopy = gfx + bufferLen;
	dmaCopyWords(0, bgGetGfxPtr(7), vramCopy, bufferSize);
	const u16 pal[] = {0, colorBlend(0, 0xFFFF, 20), colorBlend(0, 0xFFFF, 80), 0xFFFF};

	std::vector<dirent> dirContent;
	u32 selected = 0, scrolled = 0;
	bool first = true, dirValid = false, canceled = false;
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		u32 keys = keysDown();
		if (first) first = false;
		else if (keys & KEY_A) {
			dirent target = dirContent[selected];
			if (target.d_type == DT_DIR) {
				if (chdir(target.d_name) != 0) continue;
				getcwd(curPath, PATH_MAX);
				dirContent.clear();
				dirValid = false;
				selected = scrolled = 0;
			}
			else if (target.d_type == DT_REG) break;
		}
		else if (keys & KEY_B) {
			dirContent.clear();
			dirValid = false;
			selected = scrolled = 0;
			if (chdir("..") == 0) getcwd(curPath, PATH_MAX);
			else { // got to drive select
				curPath[0] = '\0';
				if (sdFound) {
					dirent sd = {.d_type = DT_DIR, .d_name = "sd:/"};
					dirContent.emplace_back(sd);
				}
				if (fatFound) {
					dirent fat = {.d_type = DT_DIR, .d_name = "fat:/"};
					dirContent.emplace_back(fat);
				}
				dirValid = true;
			}
		}
		else if (keys & KEY_X) {
			canceled = true;
			break;
		}
		else if (keys & KEY_UP) {
			if (selected != 0) selected--;
		}
		else if (keys & KEY_DOWN) {
			if (++selected == dirContent.size()) selected--;
		}
		else continue; // skip if nothing changed.

		if (!dirValid) {
			DIR *dir = opendir(".");
			dirent *entry = readdir(dir);
			while (entry != NULL) {
				if (strcmp(entry->d_name, ".") != 0) {
					if (entry->d_type == DT_REG && extensions.size() > 0) {
						auto it = extensions.begin();
						while (it != extensions.end()) {
							if (strcmp(*(it++), strrchr(entry->d_name, '.') + 1) == 0) {
								dirContent.emplace_back(*entry);
								break;
							}
						}
					}
					else dirContent.emplace_back(*entry);
				}
				entry = readdir(dir);
			}
			closedir(dir);
			std::stable_sort(dirContent.begin(), dirContent.end(), sortDirectoriesFirst);
			dirValid = true;
		}

		toncset16(gfx, pal[0], SCREEN_WIDTH * SCREEN_HEIGHT);
		fontPrintString(font, pal, message, gfx, SCREEN_WIDTH, 0, 0, SCREEN_WIDTH - 10);
		fontPrintString(font, pal, curPath, gfx, SCREEN_WIDTH, 0, font.tileHeight, SCREEN_WIDTH - 10);
		u32 printableLines = (SCREEN_HEIGHT / font.tileHeight) - 3;
		if (selected < scrolled) scrolled--;
		if (selected - scrolled >= printableLines) scrolled++;
		for (u32 i = 0; i < printableLines && scrolled + i < dirContent.size(); i++) {
			fontPrintString(font, pal, dirContent[scrolled + i].d_name, gfx, SCREEN_WIDTH, 16, (i + 2) * font.tileHeight, SCREEN_WIDTH - 16);
		}
		fontPrintCodePoint(font, pal, '>', gfx, SCREEN_WIDTH, 4, (selected - scrolled + 2) * font.tileHeight);
		fontPrintString(font, pal, replText ? "  Select,  Back,  Use REPL" : "  Select,  Back,  Cancel", gfx, SCREEN_WIDTH, 0, SCREEN_HEIGHT - font.tileHeight, SCREEN_WIDTH);
		dmaCopy(gfx, bgGetGfxPtr(7), SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(u16));
	}

	dmaCopyWords(0, vramCopy, bgGetGfxPtr(7), bufferSize);
	free(gfx);
	chdir(oldPath);

	if (canceled) return NULL;
	char *result = (char *) malloc(PATH_MAX + NAME_MAX);
	sprintf(result, "%s/%s", curPath, dirContent[selected].d_name);
	return result;
}

void storageLoad(const char *resourceName) {
	const char *filename = strrchr(resourceName, '/');
	if (filename == NULL) filename = resourceName;
	else filename++; // skip first slash
	sprintf(storagePath, "/_nds/JSDS/%s.ls", filename);

	if (access(storagePath, F_OK) == 0) {
		FILE *file = fopen(storagePath, "r");
		if (file) {
			fseek(file, 0, SEEK_END);
			long filesize = ftell(file);
			rewind(file);
			if (filesize > 0) {
				u32 keySize, valueSize, itemsRead = 0, bytesRead = 0;
				while (true) {
					/* Read for key value pairs, as many as can be found.
					* This loop is intentionally made to be paranoid, so it will 
					* hit the brakes as soon as anything doesn't seem right.
					*/

					// read key size
					itemsRead = fread(&keySize, sizeof(u32), 1, file);
					bytesRead += itemsRead * sizeof(u32);
					if (itemsRead != 1 || bytesRead + keySize > (u32) filesize) break;

					// read key string
					char *key = (char *) malloc(keySize + 1);
					itemsRead = fread(key, 1, keySize, file);
					if (itemsRead != keySize) {
						free(key);
						break;
					}
					bytesRead += itemsRead;
					key[keySize] = '\0';

					// read value size
					itemsRead = fread(&valueSize, sizeof(u32), 1, file);
					bytesRead += itemsRead * sizeof(u32);
					if (itemsRead != 1 || bytesRead + valueSize > (u32) filesize) {
						free(key);
						break;
					}

					// read value string
					char *value = (char *) malloc(valueSize + 1);
					itemsRead = fread(value, 1, valueSize, file);
					if (itemsRead != valueSize) {
						free(key);
						free(value);
						break;
					}
					bytesRead += itemsRead;
					value[valueSize] = '\0';

					jerry_value_t keyStr = String(key);
					jerry_value_t valueStr = String(value);
					jerry_set_property(ref_storage, keyStr, valueStr);
					jerry_release_value(valueStr);
					jerry_release_value(keyStr);
					free(key);
					free(value);
				}
			}
			fclose(file);
		}
	}
}

bool storageSave() {
	bool success = false;
	mkdir("/_nds", 0777);
	mkdir("/_nds/JSDS", 0777);
	jerry_value_t keysArr = jerry_get_object_keys(ref_storage);
	u32 length = jerry_get_array_length(keysArr);
	if (length == 0) {
		if (access(storagePath, F_OK) == 0)	success = remove(storagePath) == 0;
		else success = true;
	}
	else {
		FILE *file = fopen(storagePath, "w");
		if (file) {
			u32 size;
			for (u32 i = 0; i < length; i++) {
				jerry_value_t keyStr = jerry_get_property_by_index(keysArr, i);
				jerry_value_t valueStr = jerry_get_property(ref_storage, keyStr);
				char *key = rawString(keyStr, &size);
				fwrite(&size, sizeof(u32), 1, file);
				fwrite(key, 1, size, file);
				free(key);
				char *value = rawString(valueStr, &size);
				fwrite(&size, sizeof(u32), 1, file);
				fwrite(value, 1, size, file);
				free(value);
				jerry_release_value(valueStr);
				jerry_release_value(keyStr);
			}
			fclose(file);
			success = true;
		}
	}
	jerry_release_value(keysArr);
	return success;
}



FUNCTION(File_read) {
	REQUIRE(1);

	char *mode = getInternalString(thisValue, "mode");
	bool unable = mode[0] != 'r' && mode[1] != '+';
	free(mode);
	if (unable)	return Error("Unable to read in current file mode.");
	
	jerry_length_t bytesToRead = jerry_value_as_uint32(args[0]);
	jerry_value_t arrayBuffer = jerry_create_arraybuffer(bytesToRead);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer);
	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	u32 bytesRead = fread(buf, 1, bytesToRead, file);
	if (ferror(file)) {
		jerry_release_value(arrayBuffer);
		return Error("File read failed.");
	}
	else if (feof(file) && bytesRead == 0) {
		jerry_release_value(arrayBuffer);
		return JS_NULL;
	}
	else {
		jerry_value_t u8Array = jerry_create_typedarray_for_arraybuffer_sz(JERRY_TYPEDARRAY_UINT8, arrayBuffer, 0, bytesRead);
		jerry_release_value(arrayBuffer);
		return u8Array;
	}
}

FUNCTION(File_write) {
	REQUIRE(1);
	EXPECT(jerry_get_typedarray_type(args[0]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);

	char *mode = getInternalString(thisValue, "mode");
	bool unable = mode[0] != 'w' && mode[0] != 'a' && mode[1] != '+';
	free(mode);
	if (unable) return Error("Unable to write in current file mode.");
	
	jerry_length_t offset, bufSize;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[0], &offset, &bufSize);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer) + offset;
	jerry_release_value(arrayBuffer);
	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);

	u32 bytesWritten = fwrite(buf, 1, bufSize, file);
	if (ferror(file)) {
		return Error("File write failed.");
	}
	else return jerry_create_number(bytesWritten);
}

FUNCTION(File_seek) {
	REQUIRE(1);

	int mode = SEEK_SET;
	if (argCount > 1) {
		char *seekMode = toRawString(args[1]);
		if (strcmp(seekMode, "start") == 0) mode = SEEK_SET;
		else if (strcmp(seekMode, "current") == 0) mode = SEEK_CUR;
		else if (strcmp(seekMode, "end") == 0) mode = SEEK_END;
		else mode = 10;
		free(seekMode);
	}
	if (mode == 10) return TypeError("Invalid seek mode");

	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	int success = fseek(file, jerry_value_as_int32(args[0]), mode);
	if (success != 0) return Error("File seek failed.");
	return JS_UNDEFINED;
}

FUNCTION(File_close) {
	FILE *file;
	jerry_get_object_native_pointer(thisValue, (void**) &file, &fileNativeInfo);
	if (fclose(file) != 0) return Error("File close failed.");
	return JS_UNDEFINED;
}

FUNCTION(File_static_open) {
	REQUIRE(1);
	char *path = toRawString(args[0]);
	char defaultMode[] = "r";
	char *mode = defaultMode;
	if (argCount > 1) {
		mode = toRawString(args[1]);
		if (strcmp(mode, "r") != 0 && strcmp(mode, "r+") != 0
		 && strcmp(mode, "w") != 0 && strcmp(mode, "w+") != 0
		 && strcmp(mode, "a") != 0 && strcmp(mode, "a+") != 0
		) {
			free(mode);
			free(path);
			return TypeError("Invalid file mode");
		}
	}

	FILE *file = fopen(path, mode);
	if (file == NULL) {
		if (mode != defaultMode) free(mode);
		free(path);
		return Error("Unable to open file.");
	}
	else {
		jerry_value_t modeStr = String(mode);
		jerry_value_t fileObj = jerry_create_object();
		setPrototype(fileObj, ref_File.prototype);
		jerry_set_object_native_pointer(fileObj, file, &fileNativeInfo);
		defReadonly(fileObj, "mode", modeStr);
		jerry_release_value(modeStr);
		if (mode != defaultMode) free(mode);
		free(path);
		return fileObj;
	}
}

FUNCTION(File_static_copy) {
	REQUIRE(2);
	char *sourcePath = toRawString(args[0]);
	FILE *source = fopen(sourcePath, "r");
	free(sourcePath);
	if (source == NULL) return Error("Unable to open source file during copy.");
	
	fseek(source, 0, SEEK_END);
	u32 sourceSize = ftell(source);
	rewind(source);
	u8 *data = (u8 *) malloc(sourceSize);
	u32 bytesRead = fread(data, 1, sourceSize, source);
	if (ferror(source)) {
		free(data);
		fclose(source);
		return Error("Failed to read source file during copy.");
	}
	fclose(source);

	char *destPath = toRawString(args[1]);
	FILE *dest = fopen(destPath, "w");
	free(destPath);
	if (dest == NULL) {
		free(data);
		return Error("Unable to open destination file during copy.");
	}

	fwrite(data, 1, bytesRead, dest);
	free(data);
	if (ferror(dest)) {
		fclose(dest);
		return Error("Failed to write destination file during copy.");
	}
	fclose(dest);
	return JS_UNDEFINED;
}

FUNCTION(File_static_rename) {
	REQUIRE(2);
	char *sourcePath = toRawString(args[0]);
	char *destPath = toRawString(args[1]);
	int status = rename(sourcePath, destPath);
	free(sourcePath);
	free(destPath);
	if (status != 0) return Error("Failed to rename file.");
	return JS_UNDEFINED;
}

FUNCTION(File_static_remove) {
	REQUIRE(1);
	char *path = toRawString(args[0]);
	if (remove(path) != 0) return Error("Failed to delete file.");
	return JS_UNDEFINED;
}

FUNCTION(File_static_read) {
	REQUIRE(1);
	char *path = toRawString(args[0]);
	FILE *file = fopen(path, "r");
	free(path);
	if (file == NULL) return Error("Unable to open file.");

	fseek(file, 0, SEEK_END);
	u32 fileSize = ftell(file);
	rewind(file);

	jerry_value_t arrayBuffer = jerry_create_arraybuffer(fileSize);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer);
	u32 bytesRead = fread(buf, 1, fileSize, file);
	if (ferror(file)) {
		jerry_release_value(arrayBuffer);
		fclose(file);
		return Error("File read failed.");
	}
	fclose(file);
	jerry_value_t u8Array = jerry_create_typedarray_for_arraybuffer_sz(JERRY_TYPEDARRAY_UINT8, arrayBuffer, 0, bytesRead);
	jerry_release_value(arrayBuffer);
	return u8Array;
}

FUNCTION(File_static_readText) {
	REQUIRE(1);
	char *path = toRawString(args[0]);
	FILE *file = fopen(path, "r");
	free(path);
	if (file == NULL) return Error("Unable to open file.");

	fseek(file, 0, SEEK_END);
	u32 fileSize = ftell(file);
	rewind(file);

	char *buf = (char *) malloc(fileSize);
	u32 bytesRead = fread(buf, 1, fileSize, file);
	if (ferror(file)) {
		free(buf);
		fclose(file);
		return Error("File read failed.");
	}
	fclose(file);
	jerry_value_t str = StringSized(buf, bytesRead);
	free(buf);
	return str;
}

FUNCTION(File_static_write) {
	REQUIRE(2);
	EXPECT(jerry_get_typedarray_type(args[1]) == JERRY_TYPEDARRAY_UINT8, Uint8Array);
	char *path = toRawString(args[0]);
	FILE *file = fopen(path, "w");
	free(path);
	if (file == NULL) return Error("Unable to open file.");
	
	jerry_length_t offset, bufSize;
	jerry_value_t arrayBuffer = jerry_get_typedarray_buffer(args[1], &offset, &bufSize);
	u8 *buf = jerry_get_arraybuffer_pointer(arrayBuffer) + offset;
	jerry_release_value(arrayBuffer);

	u32 bytesWritten = fwrite(buf, 1, bufSize, file);
	if (ferror(file)) {
		fclose(file);
		return Error("File write failed.");
	}
	fclose(file);
	return jerry_create_number(bytesWritten);
}

FUNCTION(File_static_writeText) {
	REQUIRE(2);
	char *path = toRawString(args[0]);
	FILE *file = fopen(path, "w");
	free(path);
	if (file == NULL) return Error("Unable to open file.");
	
	jerry_size_t textSize;
	char *text = toRawString(args[1], &textSize);
	u32 bytesWritten = fwrite(text, 1, textSize, file);
	if (ferror(file)) {
		fclose(file);
		free(text);
		return Error("File write failed.");
	}
	fclose(file);
	free(text);
	return jerry_create_number(bytesWritten);
}

FUNCTION(File_static_makeDir) {
	REQUIRE(1);
	char *path = toRawString(args[0]);
	int status = -1;
	if (argCount > 1 && jerry_value_to_boolean(args[1])) {
		char *slash = strchr(path, '/');
		if (strchr(path, ':') != NULL || path == slash) slash = strchr(slash + 1, '/');
		while (slash != NULL) {
			slash[0] = '\0';
			mkdir(path, 0777);
			slash[0] = '/';
			slash = strchr(slash + 1, '/');
		}
		status = access(path, F_OK);
	}
	else status = mkdir(path, 0777);
	free(path);
	if (status != 0) return Error("Failed to make directory.");
	return JS_UNDEFINED;
}

FUNCTION(File_static_readDir) {
	REQUIRE(1);
	char *path = toRawString(args[0]);
	DIR *dir = opendir(path);
	free(path);
	if (dir == NULL) return Error("Unable to open directory.");

	jerry_value_t dirArr = jerry_create_array(0);
	dirent *entry = readdir(dir);
	while (entry != NULL) {
		jerry_value_t entryObj = jerry_create_object();
		setProperty(entryObj, "isDirectory", jerry_create_boolean(entry->d_type == DT_DIR));
		setProperty(entryObj, "isFile", jerry_create_boolean(entry->d_type == DT_REG));
		setProperty(entryObj, "name", entry->d_name);
		jerry_call_function(ref_func_push, dirArr, &entryObj, 1);
		jerry_release_value(entryObj);
		entry = readdir(dir);
	}
	closedir(dir);
	return dirArr;
}

FUNCTION(File_static_browse) {
	char browsePathDefault[] = ".";
	char messageDefault[] = "Select a file.";
	char *browsePath = browsePathDefault;
	char *message = messageDefault;
	std::vector<char *> extensions;
	if (argCount > 0 && jerry_value_is_object(args[0])) {
		jerry_value_t pathProp = String("path");
		jerry_value_t extensionsProp = String("extensions");
		jerry_value_t messageProp = String("message");
		if (jerry_has_property(args[0], pathProp)) {
			jerry_value_t pathVal = jerry_get_property(args[0], pathProp);
			if (pathVal != JS_UNDEFINED) browsePath = toRawString(pathVal);
			jerry_release_value(pathVal);
		}
		if (jerry_has_property(args[0], extensionsProp)) {
			jerry_value_t extensionsArr = jerry_get_property(args[0], extensionsProp);
			if (jerry_value_is_array(extensionsArr)) {
				u32 length = jerry_get_array_length(extensionsArr);
				for (u32 i = 0; i < length; i++) {
					jerry_value_t extVal = jerry_get_property_by_index(extensionsArr, i);
					extensions.emplace_back(toRawString(extVal));
					jerry_release_value(extVal);
				}
			}
			jerry_release_value(extensionsArr);
		}
		if (jerry_has_property(args[0], messageProp)) {
			jerry_value_t messageVal = jerry_get_property(args[0], messageProp);
			if (messageVal != JS_UNDEFINED) message = toRawString(messageVal);
			jerry_release_value(messageVal);
		}
		jerry_release_value(messageProp);
		jerry_release_value(extensionsProp);
		jerry_release_value(pathProp);
	}
	char *result = fileBrowse(consoleGetFont(), message, browsePath, extensions);
	jerry_value_t resultVal = result == NULL ? JS_NULL : String(result);
	free(result);
	for (u32 i = 0; i < extensions.size(); i++) free(extensions[i]);
	if (message != messageDefault) free(message);
	if (browsePath != browsePathDefault) free(browsePath);
	return resultVal;
}

FUNCTION(storage_length) {
	jerry_value_t keysArr = jerry_get_object_keys(ref_storage);
	u32 length = jerry_get_array_length(keysArr);
	jerry_release_value(keysArr);
	return jerry_create_number(length);
}

FUNCTION(storage_key) {
	REQUIRE(1);
	jerry_value_t keyVal = JS_NULL;
	jerry_value_t keysArr = jerry_get_object_keys(ref_storage);
	jerry_value_t nNum = jerry_value_to_number(args[0]);
	u32 n = jerry_value_as_uint32(nNum);
	if (n < jerry_get_array_length(keysArr)) {
		keyVal = jerry_get_property_by_index(keysArr, n);
	}
	jerry_release_value(nNum);
	jerry_release_value(keysArr);
	return keyVal;
}

FUNCTION(storage_getItem) {
	REQUIRE(1);
	jerry_value_t key = jerry_value_to_string(args[0]);
	jerry_value_t value = jerry_get_property(ref_storage, args[0]);
	jerry_release_value(key);
	return jerry_value_is_undefined(value) ? JS_NULL : value;
}

FUNCTION(storage_setItem) {
	REQUIRE(2);
	jerry_value_t key = jerry_value_to_string(args[0]);
	jerry_value_t value = jerry_value_to_string(args[1]);
	jerry_set_property(ref_storage, key, value);
	jerry_release_value(value);
	jerry_release_value(key);
	return JS_UNDEFINED;
}

FUNCTION(storage_removeItem) {
	REQUIRE(1);
	jerry_value_t key = jerry_value_to_string(args[0]);
	jerry_value_t hasOwnBool = jerry_has_own_property(ref_storage, key);
	if (jerry_get_boolean_value(hasOwnBool)) {
		jerry_delete_property(ref_storage, key);
	}
	jerry_release_value(hasOwnBool);
	jerry_release_value(key);
	return JS_UNDEFINED;
}

void exposeFileAPI(jerry_value_t global) {
	// Simple custom File class, nothing like the web version
	JS_class File = createClass(global, "File", IllegalConstructor);
	setMethod(File.prototype, "read", File_read);
	setMethod(File.prototype, "write", File_write);
	setMethod(File.prototype, "seek", File_seek);
	setMethod(File.prototype, "close", File_close);
	setMethod(File.constructor, "open", File_static_open);
	setMethod(File.constructor, "copy", File_static_copy);
	setMethod(File.constructor, "rename", File_static_rename);
	setMethod(File.constructor, "remove", File_static_remove);
	setMethod(File.constructor, "read", File_static_read);
	setMethod(File.constructor, "readText", File_static_readText);
	setMethod(File.constructor, "write", File_static_write);
	setMethod(File.constructor, "writeText", File_static_writeText);
	setMethod(File.constructor, "makeDir", File_static_makeDir);
	setMethod(File.constructor, "readDir", File_static_readDir);
	setMethod(File.constructor, "browse", File_static_browse);
	ref_File = File;

	ref_storage = jerry_create_object();
	jerry_value_t storage = createObject(global, "storage");
	defGetter(storage, "length", storage_length);
	setMethod(storage, "key", storage_key);
	setMethod(storage, "getItem", storage_getItem);
	setMethod(storage, "setItem", storage_setItem);
	setMethod(storage, "removeItem", storage_removeItem);
	setMethod(storage, "clear", VOID(jerry_release_value(ref_storage); ref_storage = jerry_create_object()));
	setMethod(storage, "save", RETURN(jerry_create_boolean(storageSave())));
	jerry_release_value(storage);
}
void releaseFileReferences() {
	releaseClass(ref_File);
	jerry_release_value(ref_storage);
}