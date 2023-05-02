#ifndef JSDS_MEMSET_EXT_H
#define JSDS_MEMSET_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <nds/ndstypes.h>

void memset_ext(void *dest, u32 src, u32 size);

static inline void memset16(void *dest, u16 src, u32 size) {
	memset_ext(dest, (src << 16) | src, size * 2);
}
static inline void memset32(void *dest, u32 src, u32 size) {
	memset_ext(dest, src, size * 4);
}

#ifdef __cplusplus
}
#endif

#endif /* JSDS_MEMSET_EXT_H */