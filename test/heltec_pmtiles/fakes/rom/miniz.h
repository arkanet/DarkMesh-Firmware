// Host-only adapter: exercise the production gzip framing with system zlib.
#pragma once
#include <cstddef>
#include <cstdint>
#include <zlib.h>
using mz_uint8 = uint8_t;
struct tinfl_decompressor {
};
enum tinfl_status { TINFL_STATUS_FAILED = -1, TINFL_STATUS_DONE = 0 };
constexpr unsigned TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF = 4;
inline void tinfl_init(tinfl_decompressor *) {}
inline tinfl_status tinfl_decompress(tinfl_decompressor *, const uint8_t *input, size_t *inputBytes, uint8_t *, uint8_t *output,
                                     size_t *outputBytes, unsigned)
{
    z_stream stream{};
    stream.next_in = const_cast<uint8_t *>(input);
    stream.avail_in = *inputBytes;
    stream.next_out = output;
    stream.avail_out = *outputBytes;
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return TINFL_STATUS_FAILED;
    const int result = inflate(&stream, Z_FINISH);
    *inputBytes = stream.total_in;
    *outputBytes = stream.total_out;
    inflateEnd(&stream);
    return result == Z_STREAM_END ? TINFL_STATUS_DONE : TINFL_STATUS_FAILED;
}
