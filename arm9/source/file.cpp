#include "file.hpp"

#include <algorithm>
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

#include "helpers.hpp"
#include "io/console.hpp"
#include "io/font.hpp"
#include "tonccpy.h"



static void onFileFree(void *file) {
	fclose((FILE *) file);
}
jerry_object_native_info_t fileNativeInfo = {.free_cb = onFileFree};

jerry_value_t newFile(FILE *file, jerry_value_t mode) {
	jerry_value_t fileObj = jerry_create_object();
	jerry_value_t File = getProperty(ref_global, "File");
	jerry_value_t FilePrototype = jerry_get_property(File, ref_str_prototype);
	jerry_release_value(jerry_set_prototype(fileObj, FilePrototype));
	jerry_release_value(FilePrototype);
	jerry_release_value(File);

	jerry_set_object_native_pointer(fileObj, file, &fileNativeInfo);
	setReadonly(fileObj, "mode", mode);
	return fileObj;
}

bool sortDirectoriesFirst(dirent left, dirent right) {
	return left.d_type != right.d_type && left.d_type == DT_DIR;
}

char *fileBrowse(const char *message, const char *path, std::vector<char *> extensions) {
	char oldPath[PATH_MAX];
	getcwd(oldPath, PATH_MAX);
	if (chdir(path) != 0) return NULL;
	char curPath[PATH_MAX];
	getcwd(curPath, PATH_MAX);

	const u32 bufferLen = SCREEN_WIDTH * SCREEN_HEIGHT;
	const u32 bufferSize = bufferLen * sizeof(u16);
	u16 *gfx = (u16 *) malloc(2 * bufferSize);
	u16 *vramCopy = gfx + bufferLen;
	dmaCopyWords(0, bgGetGfxPtr(7), vramCopy, bufferSize);
	const u16 *pal = consolePalette();

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
			if (chdir("..") != 0) continue;
			getcwd(curPath, PATH_MAX);
			dirContent.clear();
			dirValid = false;
			selected = scrolled = 0;
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
		fontPrintString(defaultFont, pal, message, gfx, SCREEN_WIDTH, 0, 0, SCREEN_WIDTH - 10);
		fontPrintString(defaultFont, pal, curPath, gfx, SCREEN_WIDTH, 0, defaultFont.tileHeight, SCREEN_WIDTH - 10);
		u32 printableLines = (SCREEN_HEIGHT / defaultFont.tileHeight) - 3;
		if (selected < scrolled) scrolled--;
		if (selected - scrolled >= printableLines) scrolled++;
		for (u32 i = 0; i < printableLines && scrolled + i < dirContent.size(); i++) {
			fontPrintString(defaultFont, pal, dirContent[scrolled + i].d_name, gfx, SCREEN_WIDTH, 16, (i + 2) * defaultFont.tileHeight, SCREEN_WIDTH - 16);
		}
		fontPrintCodePoint(defaultFont, pal, '>', gfx, SCREEN_WIDTH, 4, (selected - scrolled + 2) * defaultFont.tileHeight);
		fontPrintString(defaultFont, pal, "  Select,  Back,  Cancel", gfx, SCREEN_WIDTH, 0, SCREEN_HEIGHT - defaultFont.tileHeight, SCREEN_WIDTH);
		dmaCopy(gfx, bgGetGfxPtr(7), SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(u16));
	}

	dmaCopyWords(0, vramCopy, bgGetGfxPtr(7), bufferSize);
	free(gfx);
	chdir(oldPath);

	if (canceled) return NULL;
	char *result = (char *) malloc(PATH_MAX + NAME_MAX);
	sprintf(result, "%s%s", curPath, dirContent[selected].d_name);
	return result;
}

char storagePath[PATH_MAX];

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
				char *key = getString(keyStr, &size);
				fwrite(&size, sizeof(u32), 1, file);
				fwrite(key, 1, size, file);
				free(key);
				char *value = getString(valueStr, &size);
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