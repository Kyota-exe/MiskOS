#include "GDT.h"
#include "Serial.h"

struct GDTR
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static GDTR gdtr;
GDT gdt;

const uint8_t KERNEL_CODE_TYPE_ATTRIBUTES = 0b1'00'1'1010;
const uint8_t KERNEL_DATA_TYPE_ATTRIBUTES = 0b1'00'1'0010;
const uint8_t USER_CODE_TYPE_ATTRIBUTES = 0b1'11'1'1010;
const uint8_t USER_DATA_TYPE_ATTRIBUTES = 0b1'11'1'0010;
const uint8_t LIMIT1_ATTRIBUTES = 0b0'01'00000;

void InitializeGDT()
{
    // Kernel Code Segment
    gdt.entries[1].typeAttributes = KERNEL_CODE_TYPE_ATTRIBUTES;
    gdt.entries[1].limit1Attributes = LIMIT1_ATTRIBUTES;

    // Kernel Data Segment
    gdt.entries[2].typeAttributes = KERNEL_DATA_TYPE_ATTRIBUTES;
    gdt.entries[2].limit1Attributes = LIMIT1_ATTRIBUTES;

    // User Code Segment
    gdt.entries[3].typeAttributes = USER_CODE_TYPE_ATTRIBUTES;
    gdt.entries[3].limit1Attributes = LIMIT1_ATTRIBUTES;

    // User Data Segment
    gdt.entries[4].typeAttributes = USER_DATA_TYPE_ATTRIBUTES;
    gdt.entries[4].limit1Attributes = LIMIT1_ATTRIBUTES;
    Serial::Printf("%x", (uint64_t*)&gdt.entries[6]);
    *(uint64_t*)&gdt.entries[6] = (uint64_t)0x098234 >> 32;

    gdtr.base = (uint64_t)&gdt;
    gdtr.limit = sizeof(gdt) - 1;

    asm volatile("lgdt %0" : : "m"(gdtr));
}