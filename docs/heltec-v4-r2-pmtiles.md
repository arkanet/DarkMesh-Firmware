# Heltec V4 R2: offline PMTiles maps

The optional `heltec-v4-r2-ili9488-p04` target reads PMTiles through the existing
DarkMesh map view and style selector. P02, P03 and the legacy ST7789 target keep
their original map services. The pinned device-ui dependency is unchanged.

## One card for three firmwares

Use a single FAT32 partition. Keep every archive below FAT32's 4 GiB file limit.
All three map engines accept the following directory convention:

| Firmware | Archive | Selection |
| --- | --- | --- |
| WadaMesh | `/maps/wadamesh/wadamesh.pmtiles` | Map file: `wadamesh` |
| Meshtastic | `/maps/meshtastic/meshtastic.pmtiles` | Map style: `meshtastic` |
| DarkMesh | `/maps/darkmesh/darkmesh.pmtiles` | Map style: `darkmesh` |

These are style names, not private namespaces: a firmware may list the other
folders too. Separate folders allow different coverage or contents without
replacing the card. A compatible archive can also be shared directly without
three copies. In DarkMesh, preserve the exact folder/archive base name, use a
short name (at most 18 characters), and select it in the existing map dropdown.
No directory migration or card formatting is performed.

For a pack readable by all three, use **PMTiles v3**, **256×256 PNG raster tiles**,
no tile compression, and **gzip-compressed internal directories**. DarkMesh and
WadaMesh additionally accept plain internal directories. Vector MVT/PBF, JPEG,
WebP, AVIF, Brotli/Zstd directories and gzip-compressed tile payloads are outside
this implementation. Tiles outside the archive fall back to the normal PNG
folder path `/maps/darkmesh/{z}/{x}/{y}.png` and the existing URL backup if enabled.
Archives themselves are opened read-only; normal folder caching retains its
existing write behavior.

## Reader and lifetime

The bounded reader is adapted from WadaMesh commit
`2043d07d34e11134261edef3f1080599b9e544fc`, under GPL-3.0-or-later. It checks section
bounds, directory counts, varint overflow, leaf traversal depth and gzip CRC/size.
Directory and encoded tile allocations use DarkMesh's existing LVGL PSRAM pool,
preserving the separate 450 kB PNG decoder arena on the 2 MB R2 board.
Encoded tile size is capped at 256 KiB and PNG
dimensions must match the map's configured tile size before decoding.

No archive handle survives a tile request or a card remount. Cached directories
are invalidated on archive/header changes, remount generation changes and read
errors. PNGs use the existing device-ui decoder and image ownership flags.
The P04 replacement SdFat service is selected by build middleware; dependency
sources are not rewritten. SPI2 initialization, SD retry backoff and no-format
behavior remain owned by the P04 storage adapter.

## Verification

Run `python3 bin/test-heltec-v4-r2.py` and
`python3 bin/test-heltec-pmtiles.py`. The latter needs a C++17 compiler and the
system zlib development headers. It generates 24 deterministic archives, checks
the reader against independently generated Hilbert tile IDs and exact PNG bytes,
and compiles the production SdFat/PNG bridge against host doubles. Host gzip uses
a zlib adapter for the ESP ROM tinfl API; firmware compilation uses ESP32-S3 ROM
tinfl. These tests do not emulate physical SD/SPI, LVGL rendering or RF traffic.

The branch CI runs both suites followed by P04/P03/P02/legacy firmware builds.
Hardware validation still requires the actual module, touch calibration,
no-card/reinsert tests, and simultaneous display/SD/touch/LoRa operation.
