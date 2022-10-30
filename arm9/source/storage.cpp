#include "storage.h"

#include <dirent.h>
#include <fat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "inline.h"



bool saveRequested = false;

void storageLoad(const char *resourceName) {
	const char *filename = strrchr(resourceName, '/');
	if (filename == NULL) filename = resourceName;
	else filename++; // skip first slash
	char storagePath[15 + strlen(filename)];
	sprintf(storagePath, "/_nds/JSDS/%s.ls", filename);
	
	jerry_value_t filePath = createString(storagePath);
	setInternalProperty(ref_localStorage, "filePath", filePath);
	jerry_release_value(filePath);

	if (access(storagePath, F_OK) == 0) {
		FILE *file = fopen(storagePath, "r");
		if (file) {
			fseek(file, 0, SEEK_END);
			long filesize = ftell(file);
			rewind(file);
			if (filesize > 0) {
				u32 keySize, valueSize, itemsRead = 0, bytesRead = 0, validTotalSize = 0;
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
					jerry_set_property(ref_localStorage, key, value);
					jerry_release_value(value);
					jerry_release_value(key);
					free(keyStr);
					free(valueStr);
					validTotalSize = bytesRead;
				}
				jerry_value_t totalSize = jerry_create_number(validTotalSize);
				setInternalProperty(ref_localStorage, "size", totalSize);
				jerry_release_value(totalSize);
			}
			fclose(file);
		}
	}
}

void storageUpdate() {
	if (!saveRequested) return;
	saveRequested = false;
	jerry_value_t filePath = getInternalProperty(ref_localStorage, "filePath");
	char *storagePath = getString(filePath);
	jerry_release_value(filePath);
	
	mkdir("/_nds", 0777);
	mkdir("/_nds/JSDS", 0777);

	jerry_value_t sizeVal = getInternalProperty(ref_localStorage, "size");
	u32 size = jerry_value_as_uint32(sizeVal);
	jerry_release_value(sizeVal);

	if (size == 0) remove(storagePath);
	else {
		FILE *file = fopen(storagePath, "w");
		if (file) {
			jerry_value_t keys = jerry_get_object_keys(ref_localStorage);
			u32 length = jerry_get_array_length(keys);
			u32 size;
			for (u32 i = 0; i < length; i++) {
				jerry_value_t key = jerry_get_property_by_index(keys, i);
				jerry_value_t value = jerry_get_property(ref_localStorage, key);
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
			jerry_release_value(keys);
			fclose(file);
		}
	}

	free(storagePath);
}

void storageRequestSave() {
	saveRequested = true;
}

jerry_value_t newStorage() {
	jerry_value_t storage = jerry_create_object();
	jerry_value_t Storage = getProperty(ref_global, "Storage");
	jerry_value_t StoragePrototype = jerry_get_property(Storage, ref_str_prototype);
	jerry_release_value(jerry_set_prototype(storage, StoragePrototype));
	jerry_release_value(StoragePrototype);
	jerry_release_value(Storage);
	jerry_value_t proxy = jerry_create_proxy(storage, ref_proxyHandler_storage);
	setInternalProperty(storage, "proxy", proxy);
	jerry_release_value(storage);
	jerry_value_t zero = jerry_create_number(0);
	setInternalProperty(proxy, "size", zero);
	jerry_release_value(zero);
	return proxy;
}

// Helpers for DS File API. Not really part of the Storage API,
// but it may as well go here.

static void onFileFree(void *file) {
	fclose((FILE *) file);
}
jerry_object_native_info_t fileNativeInfo = {.free_cb = onFileFree};

jerry_value_t newDSFile(FILE *file, jerry_value_t mode) {
	jerry_value_t fileObj = jerry_create_object();
	jerry_value_t DSFile = getProperty(ref_DS, "File");
	jerry_value_t DSFilePrototype = jerry_get_property(DSFile, ref_str_prototype);
	jerry_release_value(jerry_set_prototype(fileObj, DSFilePrototype));
	jerry_release_value(DSFilePrototype);
	jerry_release_value(DSFile);

	jerry_set_object_native_pointer(fileObj, file, &fileNativeInfo);
	setReadonly(fileObj, "mode", mode);
	return fileObj;
}