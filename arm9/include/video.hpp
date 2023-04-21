#ifndef JSDS_VIDEO_HPP
#define JSDS_VIDEO_HPP

#include "jerry/jerryscript.h"

void exposeVideoAPI(jerry_value_t global);
void releaseVideoReferences();

#endif /* JSDS_VIDEO_HPP */