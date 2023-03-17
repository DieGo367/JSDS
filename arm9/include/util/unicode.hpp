#ifndef JSDS_UNICODE_HPP
#define JSDS_UNICODE_HPP

#include <nds/ndstypes.h>
#include <stddef.h>

// Return value must be freed!
char *UTF16toUTF8(const char16_t *utf16, u32 utf16Length, u32 *utf8Length = NULL);
// Return value must be freed!
char16_t *UTF8toUTF16(const char *utf8, u32 utf8Length, u32 *utf16Length = NULL);

#endif /* JSDS_UNICODE_HPP */