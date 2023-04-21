#ifndef JSDS_FILE_HPP
#define JSDS_FILE_HPP

#include <vector>
#include "jerry/jerryscript.h"
#include "util/font.hpp"

char *fileBrowse(NitroFont font, const char *message, const char *path, std::vector<char *> extensions, bool replText = false);

void storageLoad(const char *resourceName);

void exposeFileAPI(jerry_value_t global);
void releaseFileReferences();

#endif /* JSDS_FILE_HPP */