/* <z64.me> adapted from the official lz4 decoder at lz4/lib/lz4.c */

#define Z64DECOMPRESS

#define LZ4_MAX_INPUT_SIZE        0x7E000000   /* 2 113 929 216 bytes */
#define LZ4_COMPRESSBOUND(isize)  ((unsigned)(isize) > (unsigned)LZ4_MAX_INPUT_SIZE ? 0 : (isize) + ((isize)/255) + 16)
#define KIB(X) ((X) * 1024)
#define MAX_BUFFER_SIZE KIB(LZ4_BLOCK_SIZE_KIB)

#ifdef Z64DECOMPRESS
	#include <stdio.h>
	#include <stdint.h>
	#include <string.h>
	#include <stddef.h>
	#include <stddef.h>
	
	typedef uint8_t u8;
	typedef uint16_t u16;
	typedef uint64_t u64;
	
	#define PTR_t void *
	#define HEADER_SIZE 8
	#define LZ4_BLOCK_SIZE_KIB (1024 * 64) // gets expanded to a generous 64 MiB
	
	void DmaMgr_DmaRomToRam(PTR_t src, void *dst, unsigned sz)
	{
		memcpy(dst, src, sz);
	}
#else
	#define PTR_t uintptr_t
	#define HEADER_SIZE 8
	
	#include "global.h"
#endif

#ifndef LZ4_BLOCK_SIZE_KIB
#	error please define LZ4_BLOCK_SIZE_KIB e.g. -DLZ4_BLOCK_SIZE_KIB=64
#endif

#define MINMATCH       4
#define ML_BITS        4
#define LASTLITERALS   5
#define MFLIMIT        12

/**
 * LZ4 relies on memcpy with a constant size being inlined. In freestanding
 * environments, the compiler can't assume the implementation of memcpy() is
 * standard compliant, so it can't apply its specialized memcpy() inlining
 * logic. When possible, use __builtin_memcpy() to tell the compiler to analyze
 * memcpy() as if it were standard compliant, so it can inline it in freestanding
 * environments. This is needed when decompressing the Linux Kernel, for example.
 */
#if !defined(LZ4_memcpy)
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#	define LZ4_memcpy(dst, src, size) __builtin_memcpy(dst, src, size)
#  else
#	define LZ4_memcpy(dst, src, size) memcpy(dst, src, size)
#  endif
#endif

#if !defined(LZ4_memmove)
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#	define LZ4_memmove __builtin_memmove
#  else
#	define LZ4_memmove memmove
#  endif
#endif

/* variant for decompress_unsafe()
 * does not know end of input
 * presumes input is well formed
 * note : will consume at least one byte */
static size_t read_long_length_no_check(const u8** pp)
{
	size_t b, l = 0;
	do { b = **pp; (*pp)++; l += b; } while (b==255);
	return l;
}

static u16 LZ4_readLE16(const void* memPtr)
{
	const u8* p = memPtr;
	return (u16)((u16)p[0] + (p[1]<<8));
}

/* core decoder variant for LZ4_decompress_fast*()
 * for legacy support only : these entry points are deprecated.
 * - Presumes input is correctly formed (no defense vs malformed inputs)
 * - Does not know input size (presume input buffer is "large enough")
 * - Decompress a full block (only)
 * @return : nb of bytes read from input.
 * Note : this variant is not optimized for speed, just for maintenance.
 *		the goal is to remove support of decompress_fast*() variants by v2.0
**/
#define prefixSize 0
#define dictStart 0
#define dictSize 0

size_t LZ4_decompress_unsafe_generic(const uint8_t* const istart, uint8_t* const ostart, int compressedSize)
{
	const u8* ip = istart;
	u8* op = (u8*)ostart;
	//u8* const oend = ostart + decompressedSize;
	const u8* const prefixStart = ostart - prefixSize;

	while (1) {
		/* start new sequence */
		unsigned token = *ip++;

		/* literals */
		{   size_t ll = token >> ML_BITS;
			if (ll==15) {
				/* long literal length */
				ll += read_long_length_no_check(&ip);
			}
			//if ((size_t)(oend-op) < ll) return -1; /* output buffer overflow */
			LZ4_memmove(op, ip, ll); /* support in-place decompression */
			op += ll;
			ip += ll;
			//if ((size_t)(oend-op) < MFLIMIT) {
			//	if (op==oend) break;  /* end of block */
			//	//DEBUGLOG(5, "invalid: literals end at distance %zi from end of block", oend-op);
			//	/* incorrect end of block :
			//	 * last match must start at least MFLIMIT==12 bytes before end of output block */
			//	return -1;
			//}
			if (ip - istart == compressedSize) break;
		}

		/* match */
		{   size_t ml = token & 15;
			size_t const offset = LZ4_readLE16(ip);
			ip+=2;

			if (ml==15) {
				/* long literal length */
				ml += read_long_length_no_check(&ip);
			}
			ml += MINMATCH;

			//if ((size_t)(oend-op) < ml) return -1; /* output buffer overflow */

			{   const u8* match = op - offset;

				/* out of range */
				if (offset > (size_t)(op - prefixStart) + dictSize) {
					//DEBUGLOG(6, "offset out of range");
					return -1;
				}

				/* check special case : extDict */
				if (offset > (size_t)(op - prefixStart)) {
					/* extDict scenario */
					const u8* const dictEnd = dictStart + dictSize;
					const u8* extMatch = dictEnd - (offset - (size_t)(op-prefixStart));
					size_t const extml = (size_t)(dictEnd - extMatch);
					if (extml > ml) {
						/* match entirely within extDict */
						LZ4_memmove(op, extMatch, ml);
						op += ml;
						ml = 0;
					} else {
						/* match split between extDict & prefix */
						LZ4_memmove(op, extMatch, extml);
						op += extml;
						ml -= extml;
					}
					match = prefixStart;
				}

				/* match copy - slow variant, supporting overlap copy */
				{   size_t u;
					for (u=0; u<ml; u++) {
						op[u] = match[u];
			}   }   }
			op += ml;
			//if ((size_t)(oend-op) < LASTLITERALS) {
			//	//DEBUGLOG(5, "invalid: match ends at distance %zi from end of block", oend-op);
			//	/* incorrect end of block :
			//	 * last match must stop at least LASTLITERALS==5 bytes before end of output block */
			//	return -1;
			//}
		} /* match */
	} /* main loop */
	return (size_t)(op - ostart);
}

static void DmaRomToRam(PTR_t *src, void *dst, unsigned sz)
{
	DmaMgr_DmaRomToRam(*src, dst, sz);
	
	#ifdef Z64DECOMPRESS
	*src = ((uint8_t*)*src) + sz;
	#else
	*src += sz;
	#endif
}

unsigned int GetSize(PTR_t *src)
{
	uint8_t sizePtr[4];
	
	DmaRomToRam(src, sizePtr, sizeof(sizePtr));
	
	return (sizePtr[0] << 24) | (sizePtr[1] << 16) | (sizePtr[2] << 8) | (sizePtr[3] << 0);
}

size_t lz4hcdec(PTR_t src, void *dst_, size_t sz)
{
	// skip the header
	if (HEADER_SIZE > 0)
		GetSize(&src); // skip 'lz4h'
	
	uint8_t *dst = dst_;
	uint8_t *dstEnd = dst + (GetSize(&src) & 0xffffff);
	
	sz = dstEnd - dst;
	
	while (dst < dstEnd)
	{
		static uint8_t tmp[LZ4_COMPRESSBOUND(MAX_BUFFER_SIZE)];
		uint32_t blockSize = GetSize(&src);
		
		DmaRomToRam(&src, tmp, blockSize);
		
		dst += LZ4_decompress_unsafe_generic(tmp, dst, blockSize);
	}
	
	return sz;
}

