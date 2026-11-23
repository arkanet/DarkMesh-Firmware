#pragma once
#include <cstdint>
#include <fstream>
#include <string>
constexpr int O_RDONLY = 0;
inline int openHandles = 0;
inline bool failRead = false;
class FsFile
{
    std::ifstream stream;

  public:
    explicit FsFile(const std::string &path) : stream(path, std::ios::binary)
    {
        if (stream.is_open())
            ++openHandles;
    }
    FsFile(FsFile &&other) = default;
    ~FsFile() { close(); }
    explicit operator bool() const { return stream.is_open(); }
    bool seekSet(uint64_t offset)
    {
        stream.clear();
        return bool(stream.seekg(offset));
    }
    int read(uint8_t *dst, size_t bytes)
    {
        if (failRead)
            return -1;
        stream.read(reinterpret_cast<char *>(dst), bytes);
        return stream.gcount();
    }
    uint64_t fileSize()
    {
        const auto pos = stream.tellg();
        stream.seekg(0, std::ios::end);
        const auto length = stream.tellg();
        stream.seekg(pos);
        return length;
    }
    void close()
    {
        if (stream.is_open()) {
            --openHandles;
            stream.close();
        }
    }
};
struct SdFs {
    std::string root;
    FsFile open(const char *path, int) { return FsFile(root + path); }
};
struct ISdCard {
    bool ready = true;
    bool init() { return ready; }
};
extern SdFs SDFs;
extern ISdCard *sdCard;
