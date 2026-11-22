#!/usr/bin/env python3
"""Compile the real P04 storage adapter against deterministic SD/Arduino doubles.

The adapter must survive absent/unready cards without repeated bus probes, reject
formatting, preserve SPI2, and retry across 32-bit clock wrap. Configuration checks
also keep the external harness separate from radio, GNSS and legacy TFT signals.
No connected device or PlatformIO toolchain is needed for the adapter tests.
"""

import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MODERN = (ROOT / "src/graphics/HeltecV4R2Storage.cpp").read_text().find(
    "FatFsStorage"
) >= 0


class BuildEnv:
    def __getitem__(self, key):
        assert key == "PIOENV"
        return "heltec-v4-r2-ili9488-p04"

    def AddBuildMiddleware(self, callback):
        self.middleware = callback


class SourceNode:
    def __init__(self, path):
        self.path = path

    def get_abspath(self):
        return self.path


build_env = BuildEnv()
scope = {"env": build_env, "Import": lambda name: None}
exec((ROOT / "extra_scripts/heltec_v4_r2_storage.py").read_text(), scope)
for replaced in scope["REPLACED"]:
    node = SourceNode("/repo/.pio/libdeps/p04/meshtastic-device-ui/" + replaced)
    assert build_env.middleware(node) is None
    node = SourceNode("/repo/src/" + replaced)
    assert build_env.middleware(node) is node
print("P04 native storage substitution, without dependency edits: PASS")

ARDUINO = r"""
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>
extern uint32_t nowMs;
inline uint32_t millis() { return nowMs; }
namespace Time { inline uint32_t getMillis() { return nowMs; } }
class String : public std::string {
public:
  using std::string::string;
};
"""

SPI = r"""
#pragma once
struct SPIClass { int begins = 0, ends = 0; };
extern SPIClass SPI;
"""

SD = r"""
#pragma once
#include "SPI.h"
#include <cassert>
#include <cstdint>
#define CARD_NONE 0
#define CARD_MMC 1
#define CARD_SD 2
#define CARD_SDHC 3
namespace fs {
class FS {};
class SDFS : public FS { protected: uint8_t _pdrv = 0; };
}
struct FakeSD : fs::SDFS {
  int begins = 0, ends = 0;
  bool beginOk = false, ready = false;
  bool live = false;
  bool begin(int cs, SPIClass& spi, uint32_t hz, const char*, int files, bool format) {
    assert(cs == 43 && &spi == &SPI && hz == SD_SPI_FREQUENCY && files == 6 && !format);
    ++begins;
    live = beginOk && ready;
    return beginOk;
  }
  int cardType() { return live ? CARD_SDHC : CARD_NONE; }
  uint64_t cardSize() { return 8ULL * 1024 * 1024 * 1024; }
  void end() { ++ends; live = false; }
};
extern FakeSD SD;
"""

FATFS = r"""
#pragma once
#include "SD.h"
#include <memory>
class IStorage {
public:
  enum class Status { Ok, SlotEmpty, CardError, NoFilesystem };
  virtual ~IStorage() = default;
  virtual Status mount() = 0;
  virtual bool format() { assert(false); return false; }
};
struct StorageInfo { enum CardType { eNone, eMMC, eSD, eSDHC, eSDXC, eUnknown }; };
class ISdBus {
public:
  virtual ~ISdBus() = default;
  virtual IStorage::Status mount(bool) = 0;
  virtual void unmount() = 0;
  virtual uint8_t drive() const = 0;
  virtual fs::FS* arduinoFS() = 0;
  virtual StorageInfo::CardType cardType() = 0;
};
class FatFsStorage : public IStorage {
  std::unique_ptr<ISdBus> bus;
public:
  explicit FatFsStorage(std::unique_ptr<ISdBus> b) : bus(std::move(b)) {}
  Status mount() override { return bus->mount(false); }
};
"""

SDFAT = r"""
#pragma once
#include "Arduino.h"
#include "SPI.h"
#include <cassert>
#include <set>
#define FAT_TYPE_EXFAT 64
#define FAT_TYPE_FAT32 32
#define FAT_TYPE_FAT16 16
#define SHARED_SPI 0
#define USER_SPI_BEGIN 2
#define O_RDONLY 0
struct SdSpiConfig {
  SdSpiConfig(int cs, int flags, uint32_t hz, SPIClass* spi) {
    assert(cs == 43 && flags == (SHARED_SPI | USER_SPI_BEGIN));
    assert(hz == SD_SPI_FREQUENCY && spi == &SPI);
  }
};
class FsFile {
public:
  explicit operator bool() const { return false; }
  FsFile openNextFile() { return {}; }
  void getName(char*, size_t) {}
  bool isDirectory() { return false; }
  void close() {}
  String readStringUntil(char) { return {}; }
};
class SdFs {
  struct Card { int type() { return 3; } } cardObject;
public:
  int begins = 0, ends = 0;
  bool beginOk = false, ready = false, live = false;
  uint8_t sdError = 0;
  uint8_t sdErrorCode() { return sdError; }
  bool begin(const SdSpiConfig&) { ++begins; live = beginOk; sdError = 0; return beginOk; }
  void end() { ++ends; live = false; }
  Card* card() { return live && ready ? &cardObject : nullptr; }
  uint8_t fatType() { return live && ready ? FAT_TYPE_FAT32 : 0; }
  uint32_t freeClusterCount() { return 8192; }
  uint32_t bytesPerCluster() { return 65536; }
  uint32_t clusterCount() { return 131072; }
  FsFile open(const char*, int = 0) { return {}; }
  bool exists(const char*) { return false; }
};
extern SdFs SDFs;
class ISdCard {
public:
  enum CardType { eNone, eMMC, eSD, eSDHC, eSDXC, eSDUC, eUnknown };
  enum FatType { eNA, eFat16, eFat32, eExFat };
  enum ErrorType { eNoError, eSlotEmpty, eCardError, eFormatError, eNoMbrError, eUnknownError };
protected:
  bool updated = false;
};
class SdFsCard : public ISdCard {
public:
  bool init();
  CardType cardType();
  FatType fatType();
  ErrorType errorType();
  uint64_t usedBytes();
  uint64_t freeBytes();
  uint64_t cardSize();
  bool format();
  std::set<std::string> loadMapStyles(const char*);
  std::string getUrlProvider(const char*, const char*);
};
"""

TEST = r"""
#include <cassert>
#include <cstdio>
uint32_t nowMs = UINT32_MAX - 4999;
SPIClass SPI;
#ifdef TEST_MODERN
FakeSD SD;
#endif
int main() {
#ifdef TEST_MODERN
  auto storage = createStorage();
  auto attempt = [&]() { return storage->mount() == IStorage::Status::Ok; };
  auto& card = SD;
#else
  SdFsCard storage;
  auto attempt = [&]() { return storage.init(); };
  auto& card = SDFs;
#endif
  assert(!attempt() && card.begins == 1 && card.ends == 1);
  nowMs = UINT32_MAX;
  assert(!attempt() && card.begins == 1);
  nowMs = 4999;
  assert(!attempt() && card.begins == 1);
  nowMs = 5000;
  card.beginOk = true;
  assert(!attempt() && card.begins == 2 && card.ends == 2); // begin without card readiness
  nowMs = 15000;
  card.ready = true;
  assert(attempt() && card.begins == 3);
#ifdef TEST_MODERN
  assert(!storage->format());
  HeltecSdBus bus;
  assert(bus.mount(true) == IStorage::Status::CardError);
  assert(bus.cardType() == StorageInfo::eSDHC);
#else
  assert(!storage.format());
  assert(storage.cardType() == ISdCard::eSDHC);
  assert(storage.cardSize() == 8ULL * 1024 * 1024 * 1024);
  assert(storage.usedBytes() + storage.freeBytes() == storage.cardSize());
  assert(attempt() && card.begins == 3); // live adoption, no teardown
#endif
  assert(card.begins == 3 && card.ends == 2);
  assert(SPI.begins == 0 && SPI.ends == 0);
#ifndef TEST_MODERN
  card.sdError = 1;
  assert(storage.cardType() == ISdCard::eNone);
  assert(attempt() && card.begins == 4); // cached FAT/type cannot mask a low-level read error
#endif
  HeltecSdRetry retry;
  nowMs = 0;
  retry.failure();
  assert(!retry.ready());
  nowMs = 10000;
  assert(retry.ready());
  retry.success();
  assert(retry.ready());
  puts("P04 production storage adapter: PASS");
}
"""

with tempfile.TemporaryDirectory(prefix="heltec-p04-") as tmp:
    fake = Path(tmp)
    for name, content in {
        "configuration.h": "#pragma once\n",
        "Arduino.h": ARDUINO,
        "UptimeClock.h": '#include "Arduino.h"\n',
        "SPI.h": SPI,
        "SD.h": SD,
        "storage/FatFsStorage.h": FATFS,
        "graphics/common/SdCard.h": SDFAT,
    }.items():
        p = fake / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content)
    source = fake / "test.cpp"
    source.write_text('#include "graphics/HeltecV4R2Storage.cpp"\n' + TEST)
    for hz in (4_000_000, 1_000_000):
        cmd = [
            os.environ.get("CXX", "g++"),
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DHELTEC_V4_R2_P04=1",
            "-DSDCARD_CS=43",
            f"-DSD_SPI_FREQUENCY={hz}",
            f"-I{fake}",
            f"-I{ROOT / 'src'}",
            str(source),
            str(ROOT / "src/mesh/Throttle.cpp"),
            "-o",
            str(fake / "test"),
        ]
        if MODERN:
            cmd.insert(1, "-DTEST_MODERN=1")
        subprocess.run(cmd, check=True)
        subprocess.run([str(fake / "test")], check=True)

config = json.loads(
    subprocess.check_output(
        ["pio", "project", "config", "--json-output"], cwd=ROOT, text=True
    )
)
envs = {name: dict(values) for name, values in config}
for suffix in ("p02", "p03", "p04"):
    env = envs[f"env:heltec-v4-r2-ili9488-{suffix}"]
    flags = env["build_flags"]
    if isinstance(flags, list):
        flags = "\n".join(flags)
    for flag in (
        "LGFX_PIN_SCK=47",
        "LGFX_PIN_MOSI=33",
        "LGFX_PIN_MISO=48",
        "LGFX_PIN_CS=21",
        "LGFX_PIN_DC=16",
        "LGFX_PIN_RST=35",
        "LGFX_PIN_BL=6",
        "HW_SPI1_DEVICE=1",
        "LGFX_SCREEN_WIDTH=320",
        "LGFX_SCREEN_HEIGHT=480",
    ):
        assert flag in flags, (suffix, flag)
    for conflict in (
        "CUSTOM_TOUCH_DRIVER",
        "PIN_BUZZER",
        "PIN_BUTTON2",
        "I2C_SDA1",
        "I2C_SCL1",
        "SDCARD_USE_SPI1",
    ):
        assert conflict not in flags, (suffix, conflict)
    assert ("LGFX_TOUCH=XPT2046" in flags) == (suffix != "p02")
    assert flags.count("HAS_TOUCHSCREEN=") == int(suffix != "p02"), suffix
    assert ("HAS_SDCARD=1" in flags) == (suffix == "p04")
    unflags = env["build_unflags"]
    assert "gnu++11" in str(unflags)
assert "HELTEC_V4_R2_ILI9488" not in str(envs["env:heltec-v4-tft"]["build_flags"])
print("P02/P03/P04 harness and legacy-target isolation: PASS")
