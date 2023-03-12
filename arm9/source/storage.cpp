#include "storage.hpp"

#include <dirent.h>
#include <fat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "inline.hpp"



char storagePath[256];

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
					char *keyStr = (char *) malloc(keySize + 1);
					itemsRead = fread(keyStr, 1, keySize, file);
					if (itemsRead != keySize) {
						free(keyStr);
						break;
					}
					bytesRead += itemsRead;
					keyStr[keySize] = '\0';

					// read value size
					itemsRead = fread(&valueSize, sizeof(u32), 1, file);
					bytesRead += itemsRead * sizeof(u32);
					if (itemsRead != 1 || bytesRead + valueSize > (u32) filesize) {
						free(keyStr);
						break;
					}

					// read value string
					char *valueStr = (char *) malloc(valueSize + 1);
					itemsRead = fread(valueStr, 1, valueSize, file);
					if (itemsRead != valueSize) {
						free(keyStr);
						free(valueStr);
						break;
					}
					bytesRead += itemsRead;
					valueStr[valueSize] = '\0';

					jerry_value_t key = createString(keyStr);
					jerry_value_t value = createString(valueStr);
					jerry_set_property(ref_storage, key, value);
					jerry_release_value(value);
					jerry_release_value(key);
					free(keyStr);
					free(valueStr);
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
	jerry_value_t keys = jerry_get_object_keys(ref_storage);
	u32 length = jerry_get_array_length(keys);
	if (length == 0) {
		if (access(storagePath, F_OK) == 0)	success = remove(storagePath) == 0;
		else success = true;
	}
	else {
		FILE *file = fopen(storagePath, "w");
		if (file) {
			u32 size;
			for (u32 i = 0; i < length; i++) {
				jerry_value_t key = jerry_get_property_by_index(keys, i);
				jerry_value_t value = jerry_get_property(ref_storage, key);
				char *keyStr = getString(key, &size);
				fwrite(&size, sizeof(u32), 1, file);
				fwrite(keyStr, 1, size, file);
				free(keyStr);
				char *valueStr = getString(value, &size);
				fwrite(&size, sizeof(u32), 1, file);
				fwrite(valueStr, 1, size, file);
				free(valueStr);
				jerry_release_value(value);
				jerry_release_value(key);
			}
			fclose(file);
			success = true;
		}
	}
	jerry_release_value(keys);
	return success;
}

// Helpers for File API. Not really part of the Storage API,
// but it may as well go here.

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