#include "common.h"
#include "battle/btl_pool.h"
#include "sys/heap.h"

extern void *memset(void *dst, s32 c, u32 n);

extern BtlPool *gBtlPool;
#ifdef PORT
/* Mod "Ultra Instinct" v83: the arenas are effect_pool_scale (4) times the console's; an allocation an arena has no
   room left for comes from the heap (and goes back with the arena: BtlPool_Free, BtlPool_Reset, BtlPool_Term) instead
   of running over into the next arena (a technique swap's bigger effect corrupted memory). */
extern f32 Port_ModValue(const char *key, f32 def);
extern void Port_ModPoolLog(s32 slot, s32 peak, s32 console, s32 size, s32 spilled);
#define BTL_POOL_SPILL_MAX 256
static void *sBtlPoolSpill[BTL_POOL_SLOT_COUNT][BTL_POOL_SPILL_MAX];
static s32 sBtlPoolSpills[BTL_POOL_SLOT_COUNT];
static s32 sBtlPoolPeak[BTL_POOL_SLOT_COUNT];
static s32 sBtlPoolLogged[BTL_POOL_SLOT_COUNT];
static s32 sBtlPoolScale;

static s32 BtlPool_Scale(void) {
    if (sBtlPoolScale == 0) {
        sBtlPoolScale = (s32)Port_ModValue("effect_pool_scale", 4.0f);
        sBtlPoolScale = sBtlPoolScale < 1 ? 1 : sBtlPoolScale > 16 ? 16 : sBtlPoolScale;
    }
    return sBtlPoolScale;
}

/* Gives back the heap blocks of an arena slot; logs (debug_memory) a peak over the console's size. */
static void BtlPool_FreeSpills(s32 slot) {
    s32 i;

    if (sBtlPoolPeak[slot] > gBtlPool->slots[slot].size / BtlPool_Scale() && sBtlPoolPeak[slot] > sBtlPoolLogged[slot]) {
        sBtlPoolLogged[slot] = sBtlPoolPeak[slot];
        Port_ModPoolLog(slot, sBtlPoolPeak[slot], gBtlPool->slots[slot].size / BtlPool_Scale(),
                        gBtlPool->slots[slot].size, sBtlPoolSpills[slot]);
    }
    for (i = 0; i < sBtlPoolSpills[slot]; i++) {
        Heap_Free(sBtlPoolSpill[slot][i]);
    }
    sBtlPoolSpills[slot] = 0;
    sBtlPoolPeak[slot] = 0;
}
#endif

/* Written by BtlPool_Init and BtlPool_SetInitArg, never read. */
extern s32 gBtlPoolInitArg;

/* The arena block is reached through a pointer to the field, not as a plain member: after a store to it
 * the compiler reloads gBtlPool, which a direct `gBtlPool->arenaMem = x` does not reproduce. */
#define ARENA_MEM (*&gBtlPool->arenaMem)

s32 BtlPool_CalcArenaSize(void);
void BtlPool_CarveArenas(void);
u8 *BtlPool_InitSlot(BtlPoolSlot *slot, s32 size, u8 *mem);

/* Allocates the pool manager and the memory behind every arena slot. */
void BtlPool_Init(s32 arg) {
    s32 size;

    gBtlPoolInitArg = arg;
    gBtlPool = Heap_Alloc(sizeof(BtlPool), 0x20, 0, HEAP_ANY);
    memset(gBtlPool, 0, sizeof(BtlPool));
    gBtlPool->arenaMask = 0x3FE;
    size = BtlPool_CalcArenaSize();
    if (size > 0) {
        ARENA_MEM = Heap_Alloc(size, 0x20, 0, HEAP_ANY);
        memset(ARENA_MEM, 0, size);
        BtlPool_CarveArenas();
    }
}

/* Frees the arena memory and the pool manager. */
void BtlPool_Term(void) {
#ifdef PORT
    if (gBtlPool != NULL) {
        s32 i;

        for (i = 0; i < BTL_POOL_SLOT_COUNT; i++) {
            BtlPool_FreeSpills(i);
        }
    }
#endif
    if (ARENA_MEM != NULL) {
        Heap_Free(ARENA_MEM);
        ARENA_MEM = NULL;
    }
    if (gBtlPool != NULL) {
        Heap_Free(gBtlPool);
        gBtlPool = NULL;
    }
}

/* Allocates size bytes (rounded up to 32) from a slot: bump allocation for an arena, the heap otherwise. */
void *BtlPool_Alloc(s32 slot, s32 size) {
    s32 aligned = BtlPool_AlignUp32(size);
    BtlPoolSlot *s = &gBtlPool->slots[slot];
    void *ptr;

    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(slot)) {
#ifdef PORT
        if (s->used + aligned > s->size && sBtlPoolSpills[slot] < BTL_POOL_SPILL_MAX) {
            ptr = Heap_Alloc(size, 0x20, 0, HEAP_ANY);  /* the arena is full */
            sBtlPoolSpill[slot][sBtlPoolSpills[slot]++] = ptr;
            s->used += aligned;
            sBtlPoolPeak[slot] = s->used > sBtlPoolPeak[slot] ? s->used : sBtlPoolPeak[slot];
            return ptr;
        }
#endif
        ptr = s->cur;
        s->cur += aligned;
        s->used += aligned;
#ifdef PORT
        sBtlPoolPeak[slot] = s->used > sBtlPoolPeak[slot] ? s->used : sBtlPoolPeak[slot];
#endif
    } else {
        s->used += aligned;
        ptr = Heap_Alloc(size, 0x20, 0, HEAP_ANY);
    }
    return ptr;
}

/* Frees a pointer from a slot; a no-op for arena slots, which are only released by BtlPool_Reset. */
void BtlPool_Free(s32 slot, void *ptr) {
#ifdef PORT
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(slot)) {
        s32 i;

        for (i = 0; i < sBtlPoolSpills[slot]; i++) {
            if (sBtlPoolSpill[slot][i] == ptr) {
                Heap_Free(ptr);  /* one of the arena's heap blocks */
                sBtlPoolSpill[slot][i] = sBtlPoolSpill[slot][--sBtlPoolSpills[slot]];
                return;
            }
        }
        return;
    }
#endif
    if (!(gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(slot))) {
        if (ptr != NULL) {
            Heap_Free(ptr);
        }
    }
}

/* Rewinds an arena slot to empty. */
void BtlPool_Reset(s32 slot) {
    BtlPoolSlot *s = &gBtlPool->slots[slot];

    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(slot)) {
#ifdef PORT
        BtlPool_FreeSpills(slot);
#endif
        s->cur = s->base;
        s->used = 0;
    }
}

/* Selects the slot that BtlPool_GetCurrent reports. */
void BtlPool_SetCurrent(s32 slot) {
    gBtlPool->current = slot;
}

/* Returns the slot selected by BtlPool_SetCurrent. */
s32 BtlPool_GetCurrent(void) {
    return gBtlPool->current;
}

/* Stores the value BtlPool_Init also records (never read back). */
void BtlPool_SetInitArg(s32 arg) {
    gBtlPoolInitArg = arg;
}

/* Returns the total size of all enabled arena slots. */
s32 BtlPool_CalcArenaSize(void) {
    s32 total = 0;

    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(0)) {
        total = BtlPool_AlignUp32(0xA000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(1)) {
        total += BtlPool_AlignUp32(0x113000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(2)) {
        total += BtlPool_AlignUp32(0x1E000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(3)) {
        total += BtlPool_AlignUp32(0x1400);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(4)) {
        total += BtlPool_AlignUp32(0x19000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(5)) {
        total += BtlPool_AlignUp32(0x19000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(6)) {
        total += BtlPool_AlignUp32(0x400);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(7)) {
        total += BtlPool_AlignUp32(0x19000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(8)) {
        total += BtlPool_AlignUp32(0x19000);
    }
#ifdef PORT
    total *= BtlPool_Scale();
#endif
    return total;
}

/* Hands each enabled arena slot its piece of arenaMem. Slots 5 and 8 test the bit of slots 4 and 7 (original bug). */
void BtlPool_CarveArenas(void) {
    u8 *mem = gBtlPool->arenaMem;

    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(0)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[0], BtlPool_AlignUp32(0xA000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(1)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[1], BtlPool_AlignUp32(0x113000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(2)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[2], BtlPool_AlignUp32(0x1E000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(3)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[3], BtlPool_AlignUp32(0x1400), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(4)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[4], BtlPool_AlignUp32(0x19000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(4)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[5], BtlPool_AlignUp32(0x19000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(6)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[6], BtlPool_AlignUp32(0x400), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(7)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[7], BtlPool_AlignUp32(0x19000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(7)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[8], BtlPool_AlignUp32(0x19000), mem);
    }
}

/* Points a slot at its arena memory and returns the address just past it. */
u8 *BtlPool_InitSlot(BtlPoolSlot *slot, s32 size, u8 *mem) {
#ifdef PORT
    size *= BtlPool_Scale();  /* Mod "Ultra Instinct" v83 */
#endif
    slot->size = size;
    slot->base = mem;
    slot->cur = mem;
    return mem + size;
}

/* Rounds a size up to a multiple of 32. */
s32 BtlPool_AlignUp32(s32 size) {
    if (size % 32) {
        size = size / 32 * 32 + 32;
    }
    return size;
}
