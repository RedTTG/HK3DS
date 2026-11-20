// Memory split between the regular heap and the linear heap (textures, sounds and GPU buffers live in linear memory).
// libctru's default caps the linear heap at 32 MiB, which the game outgrows (Knight + room sprite sheets + sounds),
// so this splits the application's memory evenly instead, with the linear heap capped at 64 MiB.
#ifndef __HOST__
#include <3ds.h>
#include <3ds/allocator/mappable.h>

extern char *fake_heap_start, *fake_heap_end;
extern u32 __ctru_heap, __ctru_linear_heap, __ctru_heap_size, __ctru_linear_heap_size;
#define HEAP_AREA_SIZE (OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN)

void __system_allocateHeaps(void) {
    Handle reslimit = 0;
    if (R_FAILED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE))) svcBreak(USERBREAK_PANIC);
    s64 maxCommit = 0, currentCommit = 0; ResourceLimitType t = RESLIMIT_COMMIT;
    svcGetResourceLimitLimitValues(&maxCommit, reslimit, &t, 1);
    svcGetResourceLimitCurrentValues(&currentCommit, reslimit, &t, 1);
    svcCloseHandle(reslimit);
    u32 remaining = (u32)(maxCommit - currentCommit) & ~0xFFF;
#ifdef HK_SIM_O3DS
    if (remaining > (58u << 20)) remaining = 58u << 20;      // test builds: an Old 3DS's 64 MB application memory
#endif
#ifdef HK_SIM_MEM
    if (remaining > ((u32)HK_SIM_MEM << 20)) remaining = (u32)HK_SIM_MEM << 20;   // test builds: a New 3DS started in 80/96 MB
#endif
    // Old 3DS (64 MB for the whole game): textures dominate, so the linear heap gets the larger share
    u32 lin = remaining < (90u << 20) ? ((remaining / 100) * 52) & ~0xFFF : (remaining / 2) & ~0xFFF;
#ifdef HK_HD
    // HD art (twice the texels across): the room tiles and sprite pages take about 20 MB more GPU memory
    if (remaining >= (100u << 20)) lin = remaining - (40u << 20);
    if (lin > (84u << 20)) lin = 84u << 20;
#else
    if (lin > (64u << 20)) lin = 64u << 20;
#endif
    u32 heap = remaining - lin;
    if (heap > HEAP_AREA_SIZE) { lin += heap - HEAP_AREA_SIZE; heap = HEAP_AREA_SIZE; }
    __ctru_heap_size = heap; __ctru_linear_heap_size = lin;
    if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size, MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE))) svcBreak(USERBREAK_PANIC);
    if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR, MEMPERM_READ | MEMPERM_WRITE))) svcBreak(USERBREAK_PANIC);
    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
    fake_heap_start = (char *)__ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}
#endif
