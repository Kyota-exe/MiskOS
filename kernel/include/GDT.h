#pragma once

#include <stdint.h>

class GDT
{
public:
    static void Initialize();
    static void InitializeTSS();
    static void LoadGDTR();
    static void LoadTSS();
private:
    struct SegmentDescriptor
    {
        uint16_t limit0;
        uint16_t base0;
        uint8_t base1;
        uint8_t typeAttributes;
        uint8_t limit1Attributes;
        uint8_t base2;
    } __attribute__((packed));
    SegmentDescriptor entries[7];
} __attribute__((packed));
