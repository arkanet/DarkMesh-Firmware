#include "configuration.h"

#if defined(HELTEC_V4_R2_P04)
#include "graphics/HeltecSdRetry.h"
#include "graphics/common/SdCard.h"
#include <SPI.h>

SdFs SDFs;
ISdCard *sdCard = nullptr;

namespace
{
HeltecSdRetry retry;
bool mounted = false;

bool cardReady()
{
    return mounted && SDFs.card() && SDFs.fatType() != 0 && SDFs.sdErrorCode() == 0;
}
} // namespace

bool SdFsCard::init()
{
    if (cardReady())
        return true;
    if (!retry.ready())
        return false;
    mounted = SDFs.begin(SdSpiConfig(SDCARD_CS, SHARED_SPI | USER_SPI_BEGIN, SD_SPI_FREQUENCY, &SPI));
    if (cardReady()) {
        retry.success();
        return true;
    }
    SDFs.end();
    mounted = false;
    retry.failure();
    return false;
}

ISdCard::CardType SdFsCard::cardType()
{
    if (!cardReady())
        return CardType::eNone;
    if (SDFs.card()->type() == 3)
        return SDFs.fatType() == FAT_TYPE_EXFAT ? CardType::eSDXC : CardType::eSDHC;
    return CardType::eSD;
}

ISdCard::FatType SdFsCard::fatType()
{
    if (!cardReady())
        return FatType::eNA;
    switch (SDFs.fatType()) {
    case FAT_TYPE_EXFAT:
        return FatType::eExFat;
    case FAT_TYPE_FAT32:
        return FatType::eFat32;
    case FAT_TYPE_FAT16:
        return FatType::eFat16;
    default:
        return FatType::eNA;
    }
}

ISdCard::ErrorType SdFsCard::errorType()
{
    // The legacy UI offers formatting for FormatError/NoMbrError; P04 must never erase.
    return cardReady() ? ErrorType::eNoError : ErrorType::eCardError;
}

uint64_t SdFsCard::usedBytes()
{
    const uint64_t total = cardSize();
    const uint64_t free = freeBytes();
    return free <= total ? total - free : 0;
}

uint64_t SdFsCard::freeBytes()
{
    return cardReady() ? uint64_t(SDFs.freeClusterCount()) * SDFs.bytesPerCluster() : 0;
}

uint64_t SdFsCard::cardSize()
{
    return cardReady() ? uint64_t(SDFs.clusterCount()) * SDFs.bytesPerCluster() : 0;
}

bool SdFsCard::format()
{
    return false;
}

std::set<std::string> SdFsCard::loadMapStyles(const char *folder)
{
    std::set<std::string> styles;
    if (!cardReady() || !folder)
        return styles;
    FsFile maps = SDFs.open(folder);
    if (maps) {
        for (;;) {
            FsFile style = maps.openNextFile();
            if (!style)
                break;
            char name[64] = {};
            style.getName(name, sizeof(name));
            if (style.isDirectory() && name[0] && name[0] != '.')
                styles.insert(name);
            style.close();
        }
        maps.close();
    }
    if (styles.empty() && SDFs.exists("/map"))
        styles.insert("/map");
    updated = true;
    return styles;
}

std::string SdFsCard::getUrlProvider(const char *folder, const char *style)
{
    if (!cardReady() || !folder || !style)
        return {};
    const std::string path = std::string(folder) + "/" + style + "/.url";
    FsFile file = SDFs.open(path.c_str(), O_RDONLY);
    if (!file)
        return {};
    const String url = file.readStringUntil('\n');
    file.close();
    return std::string(url.c_str());
}
#endif
