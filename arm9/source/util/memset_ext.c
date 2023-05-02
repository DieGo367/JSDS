#include <nds/ndstypes.h>
#include <ndsabi.h>

// Implementation of memset for words that uses the head/tail handling of toncset but uses ndsabi for the main stint.
void memset_ext(void *dest, u32 src, u32 size) {
	if (dest == NULL || size == 0) return;

	u8 leftOffset = (u32) dest & 0b11;
	u32 *aligned = (u32 *)(dest - leftOffset);

	// handle unaligned destination
	if (leftOffset) {
		if (leftOffset + size < 4) {
			u32 srcMask = ((1 << (size * 8)) - 1) << (leftOffset * 8);
			*aligned = (*aligned & ~srcMask) | (src & srcMask);
			return;
		}
		
		u32 keepMask = ((1 << (leftOffset * 8)) - 1);
		*aligned = (*aligned & keepMask) | (src & ~keepMask);
		aligned++;
		size -= (4 - leftOffset);
	}

	// have ndsabi do the bulk of the work
	__ndsabi_wordset4(aligned, size, src);

	// fix the trailing bytes
	u32 remaining = size & 0b11;
	if (remaining) {
		aligned += size / 4;
		u32 srcMask = ((1 << (remaining * 8)) - 1);
		*aligned = (*aligned & ~srcMask) | (src & srcMask);
	}
}