// tinf (tiny inflate, zlib license) and libdeflate's decompressor (MIT, ext/libdeflate/LICENSE).
// HKZ1 data files and the rooms' art tiles are zlib streams; libdeflate unpacks them several times faster than tinf,
// which matters for the art tiles decoded while the camera scrolls.
#include "ext/tinflate.c"
#include "ext/tinfzlib.c"
#include "ext/adler32.c"
#include "ext/libdeflate/lib/utils.c"
#include "ext/libdeflate/lib/deflate_decompress.c"
#if defined(__x86_64__) || defined(__i386__)
#include "ext/libdeflate/lib/x86/cpu_features.c"
#endif

// zlib stream -> out (2-byte header and adler32 trailer skipped: the data is ours and the check costs time)
int hk_zlib_uncompress(void *out, unsigned int *outLen, const void *src, unsigned int srcLen) {
    static struct libdeflate_decompressor *d;
    if (!d) d = libdeflate_alloc_decompressor();
    if (!d || srcLen < 6) return tinf_zlib_uncompress(out, outLen, src, srcLen);
    size_t got = 0;
    enum libdeflate_result r = libdeflate_deflate_decompress(d, (const unsigned char *)src + 2, srcLen - 6, out, *outLen, &got);
    if (r != LIBDEFLATE_SUCCESS) return tinf_zlib_uncompress(out, outLen, src, srcLen);
    *outLen = (unsigned int)got;
    return 0;
}

// the same with a decompressor of the caller's own (the one above belongs to the main thread; room.c's tile worker
// thread has its own)
void *hk_inflater_new(void) { return libdeflate_alloc_decompressor(); }
int hk_zlib_uncompress_with(void *dd, void *out, unsigned int *outLen, const void *src, unsigned int srcLen) {
    struct libdeflate_decompressor *d = dd;
    if (!d || srcLen < 6) return tinf_zlib_uncompress(out, outLen, src, srcLen);
    size_t got = 0;
    enum libdeflate_result r = libdeflate_deflate_decompress(d, (const unsigned char *)src + 2, srcLen - 6, out, *outLen, &got);
    if (r != LIBDEFLATE_SUCCESS) return tinf_zlib_uncompress(out, outLen, src, srcLen);
    *outLen = (unsigned int)got;
    return 0;
}
