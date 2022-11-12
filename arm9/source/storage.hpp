#ifndef JSDS_STORAGE_HPP
#define JSDS_STORAGE_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



void storageLoad(const char *resourceName);
void storageRequestSave();
void storageUpdate();

jerry_value_t newStorage();

extern jerry_object_native_info_t fileNativeInfo;
jerry_value_t newDSFile(FILE *file, jerry_value_t mode);

#endif /* JSDS_STORAGE_HPP */