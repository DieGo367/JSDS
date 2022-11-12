#ifndef JSDS_ERROR_HPP
#define JSDS_ERROR_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



void handleError(jerry_value_t error, bool sync);

void handleRejectedPromises();

void setErrorHandlers();

#endif /* JSDS_ERROR_HPP */