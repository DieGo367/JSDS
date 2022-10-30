#ifndef JSDS_STORAGE_H
#define JSDS_STORAGE_H

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



void storageLoad(const char *resourceName);
void storageRequestSave();
void storageUpdate();

jerry_value_t newStorage();

#endif /* JSDS_STORAGE_H */