#include "abortsignal.hpp"

#include <stdlib.h>

#include "inline.hpp"

jerry_value_t newAbortSignal(bool aborted) {
	jerry_value_t signal = jerry_create_object();
	jerry_value_t AbortSignal = getProperty(ref_global, "AbortSignal");
	jerry_value_t AbortSignalPrototype = jerry_get_property(AbortSignal, ref_str_prototype);
	jerry_release_value(jerry_set_prototype(signal, AbortSignalPrototype));
	jerry_release_value(AbortSignalPrototype);
	jerry_release_value(AbortSignal);

	jerry_value_t eventListeners = jerry_create_object();
	setInternalProperty(signal, "eventListeners", eventListeners);
	jerry_release_value(eventListeners);
	jerry_value_t abortAlgorithms = jerry_create_array(0);
	setInternalProperty(signal, "abortAlgorithms", abortAlgorithms);
	jerry_release_value(abortAlgorithms);
	jerry_value_t abortedVal = jerry_create_boolean(aborted);
	setReadonly(signal, "aborted", abortedVal);
	jerry_release_value(abortedVal);
	defEventAttribute(signal, "onabort");
	return signal;
}

void abortSignalAddAlgorithm(jerry_value_t signal, jerry_value_t handler, jerry_value_t thisValue, const jerry_value_t *args, u32 argCount) {
	jerry_value_t alg = jerry_create_object();
	setProperty(alg, "handler", handler);
	setProperty(alg, "thisValue", thisValue);
	jerry_value_t argsArray = jerry_create_array(argCount);
	for (u32 i = 0; i < argCount; i++) jerry_set_property_by_index(argsArray, i, args[i]);
	setProperty(alg, "args", argsArray);
	jerry_release_value(argsArray);
	
	jerry_value_t algs = getInternalProperty(signal, "abortAlgorithms");
	jerry_value_t pushFunc = getProperty(algs, "push");
	jerry_release_value(jerry_call_function(pushFunc, algs, &alg, 1));
	jerry_release_value(pushFunc);
	jerry_release_value(algs);
	jerry_release_value(alg);
}

void abortSignalRunAlgorithms(jerry_value_t signal) {
	jerry_value_t algs = getInternalProperty(signal, "abortAlgorithms");
	u32 length = jerry_get_array_length(algs);
	for (u32 i = 0; i < length; i++) {
		jerry_value_t alg = jerry_get_property_by_index(algs, i);
		jerry_value_t handler = getProperty(alg, "handler");
		jerry_value_t thisValue = getProperty(alg, "thisValue");
		jerry_value_t argArray = getProperty(alg, "args");
		u32 argCount = jerry_get_array_length(argArray);
		jerry_value_t args[argCount];
		for (u32 j = 0; j < argCount; j++) {
			args[j] = jerry_get_property_by_index(argArray, j);
		}
		jerry_release_value(jerry_call_function(handler, thisValue, args, argCount));
		for (u32 j = 0; j < argCount; j++) jerry_release_value(args[j]);
		jerry_release_value(argArray);
		jerry_release_value(thisValue);
		jerry_release_value(handler);
		jerry_release_value(alg);
	}
	jerry_release_value(algs);

	algs = jerry_create_array(0);
	setInternalProperty(signal, "abortAlgorithms", algs);
	jerry_release_value(algs);
}
