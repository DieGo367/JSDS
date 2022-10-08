#ifndef JSDS_API_H
#define JSDS_API_H

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



extern jerry_value_t ref_global;
extern jerry_value_t ref_Event;
extern jerry_value_t ref_Error;
extern jerry_value_t ref_DOMException;
extern jerry_value_t ref_task_reportError;
extern jerry_value_t ref_task_abortSignalTimeout;
extern jerry_value_t ref_str_name;
extern jerry_value_t ref_str_constructor;
extern jerry_value_t ref_str_prototype;
extern jerry_value_t ref_str_backtrace;
extern jerry_value_t ref_proxyHandler_storage;

void exposeAPI();
void releaseReferences();

#endif /* JSDS_API_H */