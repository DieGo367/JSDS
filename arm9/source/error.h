#ifndef JSDS_ERROR_H
#define JSDS_ERROR_H

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



void handleError(jerry_value_t error, bool sync);

void handleRejectedPromises();

void setErrorHandlers();

#endif /* JSDS_ERROR_H */