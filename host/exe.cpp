// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/radiant_addrs.cpp at 89423aa) on 2026-10-21.
// Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
//
// Reads the header by offsets rather than through windows.h, so the bounds of every read are checked.
#include "exe.h"
#include <cstring>

namespace host::exe
{
    namespace
    {
        constexpr size_t kLfanew = 0x3C;            // IMAGE_DOS_HEADER.e_lfanew
        constexpr size_t kMachine = 4;              // from the "PE\0\0" signature: IMAGE_FILE_HEADER.Machine
        constexpr size_t kTimestamp = 8;            // IMAGE_FILE_HEADER.TimeDateStamp
        constexpr size_t kOptional = 24;            // the optional header follows the 20-byte file header
        constexpr size_t kSizeOfImage = kOptional + 56;
        constexpr uint16_t kAmd64 = 0x8664;
        constexpr uint16_t kPe32Plus = 0x20B;

        template <typename T> bool At(const unsigned char* image, size_t size, size_t offset, T& out)
        {
            if (offset > size || size - offset < sizeof(T)) return false;
            std::memcpy(&out, image + offset, sizeof(T));
            return true;
        }
    }

    bool Read(const unsigned char* image, size_t size, Identity& out)
    {
        uint16_t mz = 0, machine = 0, magic = 0;
        uint32_t lfanew = 0, signature = 0;
        if (!image || !At(image, size, 0, mz) || mz != 0x5A4D || !At(image, size, kLfanew, lfanew)) return false;
        if (!At(image, size, lfanew, signature) || signature != 0x00004550) return false;
        if (!At(image, size, size_t(lfanew) + kMachine, machine) || machine != kAmd64) return false;
        if (!At(image, size, size_t(lfanew) + kOptional, magic) || magic != kPe32Plus) return false;
        Identity id;
        if (!At(image, size, size_t(lfanew) + kTimestamp, id.timestamp)) return false;
        if (!At(image, size, size_t(lfanew) + kSizeOfImage, id.sizeOfImage)) return false;
        out = id;
        return true;
    }

    bool IsRadiant(const Identity& id) { return id.timestamp == kRadiant.timestamp && id.sizeOfImage == kRadiant.sizeOfImage; }
}
