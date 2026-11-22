# SPDX-License-Identifier: GPL-3.0-or-later
Import("env")

if env["PIOENV"] != "heltec-v4-r2-ili9488-p04":
    raise RuntimeError("P04 storage adapter selected outside its opt-in environment")

REPLACED = ("source/graphics/common/SdCard.cpp",)


def board_storage_only(node):
    path = node.get_abspath().replace("\\", "/")
    if "/.pio/libdeps/" in path and path.endswith(REPLACED):
        return None
    return node


env.AddBuildMiddleware(board_storage_only)
