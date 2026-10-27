// Maps the original rdh3o.exe at its preferred base (0x400000) inside this
// process so that its internal DSP routines can be called directly as a
// reference ("oracle") for the re-implementation. Runs under Wine.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "rdimage.h"

static unsigned rd_rand_state = 1;

// Wine's process heap usually sits at 0x400000, so instead of VirtualAlloc the
// harness reserves that address range inside its own image (linked with
// --section-start=.rdimg=0x400000, see build.sh).
#define RD_IMAGE_SPAN 0x2c0000
__attribute__((section(".rdimg"), used)) unsigned char rd_image_space[RD_IMAGE_SPAN] = {1};

// MSVC CRT rand(): replaces the original so tests are deterministic.
int __cdecl rd_rand(void)
{
    rd_rand_state = rd_rand_state * 214013u + 2531011u;
    return (int)((rd_rand_state >> 16) & 0x7fff);
}

void rd_srand(unsigned seed) { rd_rand_state = seed; }

void rd_patch_jump(DWORD from, void *to)
{
    BYTE *p = (BYTE *)from;
    p[0] = 0xE9;
    *(DWORD *)(p + 1) = (DWORD)to - (from + 5);
}

int rd_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 0; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    BYTE *file = malloc(size);
    fread(file, 1, size, f);
    fclose(f);

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)file;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(file + dos->e_lfanew);
    DWORD base = nt->OptionalHeader.ImageBase;
    DWORD imageSize = nt->OptionalHeader.SizeOfImage;
    BYTE *img = rd_image_space;
    DWORD oldProtect;
    if (imageSize > RD_IMAGE_SPAN) imageSize = RD_IMAGE_SPAN;  // skip the tail of .rsrc
    if (img != (BYTE *)base || !VirtualProtect(img, imageSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        MEMORY_BASIC_INFORMATION mbi;
        VirtualQuery((void *)base, &mbi, sizeof(mbi));
        fprintf(stderr, "cannot map image at %08lx (err %lu): region base=%p alloc=%p size=%lx state=%lx type=%lx\n",
                base, GetLastError(), mbi.BaseAddress, mbi.AllocationBase, (unsigned long)mbi.RegionSize,
                mbi.State, mbi.Type);
        return 0;
    }

    memcpy(img, file, nt->OptionalHeader.SizeOfHeaders);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    memset(img + nt->OptionalHeader.SizeOfHeaders, 0, imageSize - nt->OptionalHeader.SizeOfHeaders);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        DWORD len = min(sec[i].SizeOfRawData, sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData);
        if (sec[i].VirtualAddress + len > imageSize) len = imageSize > sec[i].VirtualAddress ? imageSize - sec[i].VirtualAddress : 0;
        memcpy(img + sec[i].VirtualAddress, file + sec[i].PointerToRawData, len);
    }

    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(img + dir->VirtualAddress); imp->Name; imp++) {
        HMODULE mod = LoadLibraryA((char *)(img + imp->Name));
        if (!mod) { fprintf(stderr, "cannot load %s\n", img + imp->Name); return 0; }
        DWORD *lookup = (DWORD *)(img + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        DWORD *iat = (DWORD *)(img + imp->FirstThunk);
        for (; *lookup; lookup++, iat++) {
            FARPROC fn = (*lookup & IMAGE_ORDINAL_FLAG32)
                ? GetProcAddress(mod, (LPCSTR)(*lookup & 0xffff))
                : GetProcAddress(mod, ((IMAGE_IMPORT_BY_NAME *)(img + *lookup))->Name);
            *iat = (DWORD)fn;
        }
    }
    free(file);

    rd_patch_jump(RD_ADDR_RAND, rd_rand);
    // The original CRT heap is never initialised here: route malloc/free to ours.
    rd_patch_jump(RD_ADDR_MALLOC, malloc);
    rd_patch_jump(RD_ADDR_FREE, free);
    // MMX-available flag, normally set at startup from CPUID.
    typedef int (*cpu_fn)(void);
    *(int *)RD_ADDR_MMX_FLAG = ((cpu_fn)RD_ADDR_MMX_DETECT)();
    return 1;
}
