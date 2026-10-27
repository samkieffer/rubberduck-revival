#pragma once

// Addresses inside rdh3o.exe v2.12 (SHA-256 3719f6d5...9774).
#define RD_ADDR_RAND        0x0041a9e7u
#define RD_ADDR_MMX_FLAG    0x00448124u
#define RD_ADDR_MMX_DETECT  0x00413620u

int rd_load(const char *path);
void rd_srand(unsigned seed);
int __cdecl rd_rand(void);
