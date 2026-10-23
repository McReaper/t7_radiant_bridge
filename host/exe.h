// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/radiant_addrs.h and src/radiant_addrs.cpp at
// 89423aa) on 2026-10-21. Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
//
// The Radiant build the host's addresses belong to, told by its PE header. No Windows calls, so host/test/host_test.cpp
// checks it on buffers.
#pragma once
#include <cstddef>
#include <cstdint>

namespace host::exe
{
    struct Identity
    {
        uint32_t timestamp = 0;     // IMAGE_FILE_HEADER.TimeDateStamp
        uint32_t sizeOfImage = 0;   // IMAGE_OPTIONAL_HEADER64.SizeOfImage
    };

    // Radiant_modtools.exe of the Steam mod tools, built 2019-12-14 (md5 483f3acd5157b0f56a6c8177fafb8762): the only
    // build addrs.h describes.
    constexpr Identity kRadiant{ 0x5DF45023, 0x6D95000 };

    // The identity in the first `size` bytes of an x64 image (its headers). False when they are not an x64 PE.
    bool Read(const unsigned char* image, size_t size, Identity& out);

    bool IsRadiant(const Identity& id);
}
