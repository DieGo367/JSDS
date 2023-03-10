#ifndef JSDS_API_HPP
#define JSDS_API_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



extern jerry_value_t ref_global;
extern jerry_value_t ref_localStorage;
extern jerry_value_t ref_Event;
extern jerry_value_t ref_Error;
extern jerry_value_t ref_DS;
extern jerry_value_t ref_str_name;
extern jerry_value_t ref_str_constructor;
extern jerry_value_t ref_str_prototype;
extern jerry_value_t ref_str_backtrace;
extern jerry_value_t ref_sym_toStringTag;
extern jerry_value_t ref_proxyHandler_storage;
extern jerry_value_t ref_consoleCounters;
extern jerry_value_t ref_consoleTimers;

void exposeAPI();
void releaseReferences();

#endif /* JSDS_API_HPP */