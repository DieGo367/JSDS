#ifndef JSDS_ABORTSIGNAL_H
#define JSDS_ABORTSIGNAL_H

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



jerry_value_t newAbortSignal(bool aborted);

void abortSignalAddAlgorithm(jerry_value_t signal, jerry_value_t handler, jerry_value_t thisValue, const jerry_value_t *args, u32 argCount);
void abortSignalRunAlgorithms(jerry_value_t signal);

#endif /* JSDS_ABORTSIGNAL_H */