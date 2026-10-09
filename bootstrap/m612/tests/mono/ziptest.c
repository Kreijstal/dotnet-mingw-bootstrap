/* Round trip through System.IO.Compression.Native.dll: deflate + inflate
 * (zlib-intel, level 6 and 1 = deflate_quick/SSE4.2 hash, crc32 = PCLMUL folding)
 * and brotli. Exit code 0 on success. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pal_zlib.h"
#include <brotli/encode.h>
#include <brotli/decode.h>

static int zround(int level, const uint8_t *in, uint32_t n)
{
    static uint8_t comp[1 << 20], out[1 << 20];
    PAL_ZStream s; memset(&s, 0, sizeof s);
    if (CompressionNative_DeflateInit2_(&s, level, 8, -15, 8, 0) != 0) return 1;
    s.nextIn = (uint8_t *)in; s.availIn = n; s.nextOut = comp; s.availOut = sizeof comp;
    if (CompressionNative_Deflate(&s, 4 /* Z_FINISH */) != 1 /* Z_STREAM_END */) return 2;
    uint32_t clen = sizeof comp - s.availOut;
    CompressionNative_DeflateEnd(&s);
    memset(&s, 0, sizeof s);
    if (CompressionNative_InflateInit2_(&s, -15) != 0) return 3;
    s.nextIn = comp; s.availIn = clen; s.nextOut = out; s.availOut = sizeof out;
    if (CompressionNative_Inflate(&s, 4) != 1) return 4;
    uint32_t olen = sizeof out - s.availOut;
    CompressionNative_InflateEnd(&s);
    printf("deflate level %d: %u -> %u -> %u bytes\n", level, n, clen, olen);
    return olen == n && memcmp(in, out, n) == 0 ? 0 : 5;
}

int main(void)
{
    static uint8_t buf[200000], comp[300000], out[200000];
    for (uint32_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)("the quick brown fox "[i % 20] ^ (i / 997));
    int rc = zround(6, buf, sizeof buf) | zround(1, buf, sizeof buf) | zround(9, buf, sizeof buf);
    uint32_t crc = CompressionNative_Crc32(0, (uint8_t *)"123456789", 9);
    printf("crc32(\"123456789\") = %08x (expect cbf43926)\n", crc);
    if (crc != 0xcbf43926u) rc |= 8;
    size_t clen = sizeof comp, olen = sizeof out;
    if (!BrotliEncoderCompress(5, 22, BROTLI_MODE_GENERIC, sizeof buf, buf, &clen, comp)) rc |= 16;
    if (BrotliDecoderDecompress(clen, comp, &olen, out) != BROTLI_DECODER_RESULT_SUCCESS || olen != sizeof buf || memcmp(buf, out, olen)) rc |= 32;
    printf("brotli: %u -> %u -> %u bytes\n", (unsigned)sizeof buf, (unsigned)clen, (unsigned)olen);
    printf("%s\n", rc ? "FAIL" : "OK");
    return rc;
}
