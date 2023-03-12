#ifndef JSDS_STORAGE_HPP
#define JSDS_STORAGE_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



void storageLoad(const char *resourceName);
bool storageSave();

extern jerry_object_native_info_t fileNativeInfo;
jerry_value_t newFile(FILE *file, jerry_value_t mode);

#endif /* JSDS_STORAGE_HPP */