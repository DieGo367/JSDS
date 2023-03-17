#ifndef JSDS_FILE_HPP
#define JSDS_FILE_HPP

#include <nds/ndstypes.h>
#include <vector>
#include "jerry/jerryscript.h"
#include "util/font.hpp"



extern jerry_object_native_info_t fileNativeInfo;
jerry_value_t newFile(FILE *file, jerry_value_t mode);
char *fileBrowse(NitroFont font, const char *message, const char *path, std::vector<char *> extensions, bool replText = false);

void storageLoad(const char *resourceName);
bool storageSave();

#endif /* JSDS_FILE_HPP */