/*
 * Headless stand-ins: sound, the GS, pads, movies. Everything here does nothing and reports "idle / done", which
 * is what the first milestone (the fight simulation without picture or sound) needs. Each group gets a real
 * implementation later and then moves to its own file. Arguments are ignored (the callers clean the stack).
 */
#include <stdint.h>
#include "port_host.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif
#include <stdlib.h>
#include <time.h>
#include <time.h>
extern void Port_StateTouch(void *p, unsigned long n); /* gs/state.c */

/* ---- second processor (IOP): heap and remote calls ---- */
int sceSifInitIopHeap() { return 0; }
extern void *Port_LowAlloc(size_t size); /* plat_mem.c: the game keeps these addresses in 4 bytes */
extern void Port_LowFree(void *addr);
void *sceSifAllocIopHeap(int size) { return Port_LowAlloc((size_t)size); } /* "IOP memory": ordinary memory */
int sceSifFreeIopHeap(void *addr) { Port_LowFree(addr); return 0; }
int sceSifQueryTotalFreeMemSize() { return 0x100000; }
int sceSifQueryMaxFreeMemSize() { return 0x100000; }
/* The bind is answered at once: sceSifClientData.serve (offset 0x24) becomes non-NULL. */
int sceSifBindRpc(void *client, int id, int mode) { (void)id; (void)mode; *(uint32_t *)((uint8_t *)client + 0x24) = (uint32_t)(uintptr_t)client; /* a 4-byte pointer field of the game */ return 0; }
int func_002B4AF0() { return 0; }      /* sceSifCheckStatRpc: never busy */
/* sceSifCallRpc, sceSifSetDma, sceSdRemote: the sound effects, port/src/gs/snd_se.c */
int sceSifDmaStat() { return -1; }     /* transfer finished */

/* ---- sound driver ---- */
int sceSdRemoteInit() { return 0; }
int func_00296B48() { return 0; }      /* sceSdRemoteCallbackInit */

/* ---- CRI ADXT stream players ---- */
/* ---- CRI ADXT (streamed music and voices): port/src/gs/snd_adx.c ---- */

/* ---- GS ---- */
static int (*sVsyncHandler)(int);
void sceGsResetGraph() {}
void sceGsResetPath() {}
/* Reading pixels back from the GS (the screen cross-fade of the fight's scenes reads the frame being shown: stg_b.c).
   SetDef says what: the place in GS memory (in 64-pixel units), the size; Exec delivers it, 3 bytes a pixel. */
static int sStoreBp, sStoreW, sStoreH;
int sceGsSetDefStoreImage(void *si, int sbp, int sbw, int spsm, int x, int y, int w, int h) {
    (void)si; (void)sbw; (void)spsm; (void)x; (void)y;
    sStoreBp = sbp & 0xFFFF;
    sStoreW = w & 0xFFFF;
    sStoreH = h & 0xFFFF;
    return 0;
}
int sceGsExecStoreImage(void *si, void *dst) {
    extern void Gs_StoreImage(unsigned bp, unsigned w, unsigned h, unsigned char *rgb); /* gs/gs_core.c */
    (void)si;
    Gs_StoreImage((unsigned)sStoreBp, (unsigned)sStoreW, (unsigned)sStoreH, (unsigned char *)dst);
    return 0;
}
int sceGsSyncPath() { return 0; }
void *sceGsSyncVCallback(int (*handler)(int)) { void *old = (void *)sVsyncHandler; sVsyncHandler = handler; return old; }
/* One vertical blank: runs the game's VBlank handler, as the interrupt would. */
extern void Port_Trace(unsigned vblanks);
unsigned gPortVBlanks; /* vertical blanks since start: the headless build's clock */
/* With a window, a vertical blank is also the clock: one every 1/59.94 s. The game waits for one per frame in
   the menus (60 frames per second) and two in a battle (30), as on the console. Headless runs do not wait. */
extern int GsGpu_Enabled(void);
PORT_HOST unsigned long long gPortSleptNs = 0; /* time spent waiting here (the renderer's frame timing subtracts it) */

static unsigned long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}

/* (real time, not game state: PORT_HOST) */
PORT_HOST static unsigned long long sPacePrev = 0, sPaceMin = ~0ull, sPaceMax = 0;
PORT_HOST static unsigned sPaceCount = 0, sPaceOff = 0, sPaceLate = 0, sPaceReset = 0;

/* Online play (gs/net.c): this copy runs ahead of the other one; the coming blanks are to come this much later
   in all (the grid moves, once). */
PORT_HOST unsigned gPortPaceShiftNs = 0;
/* Vertical blanks the game has really gone through (not the ones run again after a rollback): host data. */
PORT_HOST unsigned gPortLiveBlanks = 0;

PORT_HOST static unsigned long long sPaceNext = 0; /* when the coming vertical blank is due */
#define next sPaceNext
/* How long until the coming vertical blank, in microseconds (0: it is due or past, or there is no grid yet). For
   what wants to do something before it without making it late (gs/gs_draw.c: in-between pictures). */
int Port_VBlankUsLeft(void) {
    unsigned long long t = now_ns();
    return sPaceNext > t && sPaceNext - t < 100000000ull ? (int)((sPaceNext - t) / 1000ull) : 0;
}

/* How far behind its grid the game is, in microseconds: the coming vertical blank was due that long ago (0: it
   is not due yet, or there is no grid). For gs/gs_draw.c, which leaves pictures out when the machine is behind. */
int Port_PaceBehindUs(void) {
    unsigned long long t = now_ns();
    return sPaceNext != 0 && t > sPaceNext && t - sPaceNext < 100000000ull ? (int)((t - sPaceNext) / 1000ull) : 0;
}

/* The game's pace, 1000 = its own. Online it is the match's and nothing else (gs/net.c: 1000 or 1200, the same on
   both copies); otherwise the player's own "Game speed" of the Cheats tab (setting game_speed, percent, 50..200),
   which an online match never reads. Like the match's speed it is the vertical blanks' pace and the pitch of what
   is heard: every step of the game is computed as always. (PORT_HOST: not the game's state.) */
/* Mod "Ultra Instinct" v56: the mod's own tables, read once from the current directory (the game's folder, as
   bt3_settings.txt): mod_values.txt (lines "key = value", # starts a comment) and power_levels.csv (the columns id,
   power_level, base_id and health_new). A missing file, key or row keeps the value built into the program. */
#define MOD_MAX_VALUES 256
PORT_HOST static int sModLoaded;
PORT_HOST static int sModCount;
PORT_HOST static char sModKey[MOD_MAX_VALUES][64];
PORT_HOST static float sModVal[MOD_MAX_VALUES];
PORT_HOST static float sModLevel[256];
PORT_HOST static float sModDamage[256];
PORT_HOST static int sModHealth[256];
PORT_HOST static int sModHave[256];
PORT_HOST static int sModBase[256];
PORT_HOST static float sModDrain[256];  /* stamina points per second the form drains (power_levels.csv stamina_drain) */
PORT_HOST static int sModStaminaBase[256];  /* the form it goes back to out of stamina (stamina_base), -1: none */
float Port_ModValue(const char *key, float def);

/* A decimal number ("12", "-0.5", "1.5e3", "200000000000000"). Not strtod: this file is compiled with software
   float, and the C library's strtod (like log10f) returns its result in the float unit, which software-float code
   does not read (include/port_libm.h; only the functions listed there are bridged). */
static float Port_ModNumber(const char *s) {
    float v = 0.0f;
    float scale = 1.0f;
    int neg = 0;
    int eneg = 0;
    int ex = 0;

    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == '-' || *s == '+') {
        neg = *s == '-';
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10.0f + (float)(*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            scale = scale / 10.0f;
            v = v + (float)(*s - '0') * scale;
            s++;
        }
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '-' || *s == '+') {
            eneg = *s == '-';
            s++;
        }
        while (*s >= '0' && *s <= '9' && ex < 40) {
            ex = ex * 10 + (*s - '0');
            s++;
        }
        for (; ex > 0; ex--) {
            v = eneg ? v / 10.0f : v * 10.0f;
        }
    }
    return neg ? -v : v;
}

static void Port_ModLoadValues(void) {
    FILE *fp = fopen("mod_values.txt", "r");
    char line[256];

    if (fp == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), fp) != NULL && sModCount < MOD_MAX_VALUES) {
        char *p = line;
        char *eq;
        char *e;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#' || *p == '\0' || *p == '\n' || *p == '\r') {
            continue;
        }
        eq = strchr(p, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        e = eq;
        while (e > p && (e[-1] == ' ' || e[-1] == '\t')) {
            *--e = '\0';
        }
        snprintf(sModKey[sModCount], sizeof(sModKey[0]), "%s", p);
        sModVal[sModCount] = Port_ModNumber(eq + 1);
        sModCount++;
    }
    fclose(fp);
}

/* One field of a CSV line (quotes allowed); returns where the next one starts. */
static char *Port_ModField(char *p, char *out, int size) {
    int n = 0;
    int quoted = 0;

    if (*p == '"') {
        quoted = 1;
        p++;
    }
    while (*p != '\0') {
        if (quoted) {
            if (*p == '"') {
                if (p[1] == '"') {
                    p++;
                } else {
                    quoted = 0;
                    p++;
                    continue;
                }
            }
        } else if (*p == ',' || *p == '\n' || *p == '\r') {
            break;
        }
        if (n < size - 1) {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = '\0';
    return *p == ',' ? p + 1 : p;
}

static void Port_ModLoadPower(void) {
    static char field[32][128];
    char line[1024];
    float level[256];
    int base[256];
    int health[256];
    int have[256];
    int cid = -1, cpl = -1, cbase = -1, chp = -1, cdrain = -1, cstb = -1;
    float drain[256];
    int stb[256];
    int header = 1;
    int i;
    int n;
    float e = Port_ModValue("power_exponent", 0.25f);
    FILE *fp = fopen("power_levels.csv", "r");

    if (fp == NULL) {
        return;
    }
    memset(have, 0, sizeof(have));
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line;

        for (n = 0; n < 32 && *p != '\0' && *p != '\n' && *p != '\r'; n++) {
            p = Port_ModField(p, field[n], sizeof(field[0]));
        }
        if (header) {
            for (i = 0; i < n; i++) {
                if (strcmp(field[i], "id") == 0) cid = i;
                if (strcmp(field[i], "power_level") == 0) cpl = i;
                if (strcmp(field[i], "base_id") == 0) cbase = i;
                if (strcmp(field[i], "health_new") == 0) chp = i;
                if (strcmp(field[i], "stamina_drain") == 0) cdrain = i;
                if (strcmp(field[i], "stamina_base") == 0) cstb = i;
            }
            header = 0;
            if (cid < 0 || cpl < 0) {
                break;
            }
            continue;
        }
        if (cid >= n || cpl >= n) {
            continue;
        }
        i = (int)strtol(field[cid], NULL, 0);
        if (i < 0 || i >= 256) {
            continue;
        }
        level[i] = Port_ModNumber(field[cpl]);
        base[i] = cbase >= 0 && cbase < n ? (int)strtol(field[cbase], NULL, 0) : i;
        health[i] = chp >= 0 && chp < n ? atoi(field[chp]) : 0;
        drain[i] = cdrain >= 0 && cdrain < n ? Port_ModNumber(field[cdrain]) : 0.0f;
        stb[i] = cstb >= 0 && cstb < n && field[cstb][0] != 0 ? (int)strtol(field[cstb], NULL, 0) : -1;
        have[i] = 1;
    }
    fclose(fp);
    for (i = 0; i < 256; i++) {
        float pl;
        float b;

        if (!have[i]) {
            continue;
        }
        pl = level[i] > 0.0f ? level[i] : 1.0f;
        b = base[i] >= 0 && base[i] < 256 && have[base[i]] && level[base[i]] > 0.0f ? level[base[i]] : pl;
        sModLevel[i] = pl;
        sModDamage[i] = powf(pl, 2.0f * e);  /* one exponent for every level: the stronger always hits harder */
        sModHealth[i] = health[i];
        sModBase[i] = base[i] >= 0 && base[i] < 256 ? base[i] : i;
        sModDrain[i] = drain[i];
        sModStaminaBase[i] = stb[i] >= 0 && stb[i] < 256 ? stb[i] : -1;
        sModHave[i] = 1;
    }
}

/* abilities.csv: the columns id, kaioken, ultra_instinct and super_speed (1 = the character has it; L2 + down /
   right / left + circle). Without the file: Kaioken for the adult Gokus (0x00..0x0A), nothing else. */
PORT_HOST static unsigned char sModAbility[256];
PORT_HOST static int sModAbilityLoaded;

static void Port_ModLoadAbilities(void) {
    static char field[16][64];
    char line[512];
    int col[4];
    int header = 1;
    int i;
    int n;
    FILE *fp = fopen("abilities.csv", "r");

    col[0] = col[1] = col[2] = col[3] = -1;
    if (fp == NULL) {
        for (i = 0x00; i <= 0x0A; i++) {
            sModAbility[i] = 1;
        }
        return;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line;

        for (n = 0; n < 16 && *p != '\0' && *p != '\n' && *p != '\r'; n++) {
            p = Port_ModField(p, field[n], sizeof(field[0]));
        }
        if (header) {
            for (i = 0; i < n; i++) {
                if (strcmp(field[i], "id") == 0) col[0] = i;
                if (strcmp(field[i], "kaioken") == 0) col[1] = i;
                if (strcmp(field[i], "ultra_instinct") == 0) col[2] = i;
                if (strcmp(field[i], "super_speed") == 0) col[3] = i;
            }
            header = 0;
            if (col[0] < 0) {
                break;
            }
            continue;
        }
        if (col[0] >= n) {
            continue;
        }
        i = (int)strtol(field[col[0]], NULL, 0);
        if (i < 0 || i >= 256) {
            continue;
        }
        sModAbility[i] = (unsigned char)((col[1] >= 0 && col[1] < n && atoi(field[col[1]]) != 0 ? 1 : 0) |
                                         (col[2] >= 0 && col[2] < n && atoi(field[col[2]]) != 0 ? 2 : 0) |
                                         (col[3] >= 0 && col[3] < n && atoi(field[col[3]]) != 0 ? 4 : 0));
    }
    fclose(fp);
}

/* Whether character `chara` has ability `which` (0 Kaioken, 1 Ultra Instinct, 2 Super Speed). */
int Port_ModAbility(int chara, int which) {
    if (!sModAbilityLoaded) {
        sModAbilityLoaded = 1;
        Port_ModLoadAbilities();
    }
    return chara >= 0 && chara < 256 && which >= 0 && which < 3 && ((sModAbility[chara] >> which) & 1);
}

/* Diagnostic (debug_kaioken_fx = 1): one line per frame for a fighter in Kaioken or in the Kaioken skill, in
   mod_fx_log.txt next to the program: milliseconds, player, action, animation, the effect request bits 0..63, the
   object's flags and the aura type. At most 20000 lines. */
PORT_HOST static FILE *sModFxLog;
PORT_HOST static int sModFxLines;
void Port_ModDebugFx(int player, int action, int anim, unsigned int lo, unsigned int hi, unsigned int objFlags, int aura) {
    if (sModFxLines >= 20000) {
        return;
    }
    if (sModFxLog == NULL) {
        sModFxLog = fopen("mod_fx_log.txt", "w");
        if (sModFxLog == NULL) {
            sModFxLines = 20000;
            return;
        }
        fprintf(sModFxLog, "ms player action anim fxbits_63_32 fxbits_31_0 objflags aura\n");
    }
    fprintf(sModFxLog, "%u %d 0x%X 0x%X %08X %08X %08X %d\n", (unsigned int)(now_ns() / 1000000ull), player, action, anim,
            hi, lo, objFlags, aura);
    sModFxLines++;
    if ((sModFxLines & 63) == 0) {
        fflush(sModFxLog);
    }
}

/* God forms (L2 + R3 + left / up / right / down: Super Saiyan God, God Super Saiyan, Rose, Super Saiyan 4 Limit
   Breaker). god_forms.csv: the columns id, ssg, ssgss, rose, ssj4lb (1 = the character has it) and ssg_model,
   ssgss_model, rose_model, ssj4lb_model (the model it shows); god_palettes.csv: model, file (costume, +4 the damaged
   variant), texture, part (hair / eyes / brows / fur), mask (the palette entries to recolour). Both next to the
   exe. */
#define MOD_GOD_ROWS 4096
PORT_HOST static short sModGodModel[256][5];
PORT_HOST static int sModGodKey[MOD_GOD_ROWS];
PORT_HOST static unsigned short sModGodMaskTab[MOD_GOD_ROWS];
PORT_HOST static int sModGodRows;
PORT_HOST static int sModGodLoaded;
static void Port_ModLoad(void);

static void Port_ModLoadGods(void) {
    static char field[16][64];
    char line[512];
    int col[10];
    int header;
    int i;
    int k;
    int n;
    FILE *fp;

    for (i = 0; i < 256; i++) {
        for (k = 0; k < 5; k++) {
            sModGodModel[i][k] = -1;
        }
    }
    fp = fopen("god_forms.csv", "r");
    if (fp != NULL) {
        static const char *const names[9] = { "id", "ssg", "ssgss", "rose", "ssj4lb", "ssg_model", "ssgss_model",
                                                "rose_model", "ssj4lb_model" };

        header = 1;
        for (k = 0; k < 10; k++) {
            col[k] = -1;
        }
        while (fgets(line, sizeof(line), fp) != NULL) {
            char *p = line;

            for (n = 0; n < 16 && *p != '\0' && *p != '\n' && *p != '\r'; n++) {
                p = Port_ModField(p, field[n], sizeof(field[0]));
            }
            if (header) {
                for (i = 0; i < n; i++) {
                    for (k = 0; k < 9; k++) {
                        if (strcmp(field[i], names[k]) == 0) col[k] = i;
                    }
                }
                header = 0;
                continue;
            }
            if (col[0] < 0 || col[0] >= n) {
                continue;
            }
            i = (int)strtol(field[col[0]], NULL, 0);
            if (i < 0 || i >= 256) {
                continue;
            }
            for (k = 1; k <= 4; k++) {
                if (col[k] >= 0 && col[k] < n && atoi(field[col[k]]) != 0 && col[k + 4] >= 0 && col[k + 4] < n &&
                    field[col[k + 4]][0] != '\0') {
                    sModGodModel[i][k] = (short)strtol(field[col[k + 4]], NULL, 0);
                }
            }
        }
        fclose(fp);
    }
    fp = fopen("god_palettes.csv", "r");
    if (fp != NULL) {
        header = 1;
        for (k = 0; k < 8; k++) {
            col[k] = -1;
        }
        while (fgets(line, sizeof(line), fp) != NULL && sModGodRows < MOD_GOD_ROWS) {
            char *p = line;
            int model;
            int file;
            int tex;
            int part;

            for (n = 0; n < 16 && *p != '\0' && *p != '\n' && *p != '\r'; n++) {
                p = Port_ModField(p, field[n], sizeof(field[0]));
            }
            if (header) {
                for (i = 0; i < n; i++) {
                    if (strcmp(field[i], "model") == 0) col[0] = i;
                    if (strcmp(field[i], "file") == 0) col[1] = i;
                    if (strcmp(field[i], "texture") == 0) col[2] = i;
                    if (strcmp(field[i], "part") == 0) col[3] = i;
                    if (strcmp(field[i], "mask") == 0) col[4] = i;
                }
                header = 0;
                continue;
            }
            if (col[0] < 0 || col[1] < 0 || col[2] < 0 || col[3] < 0 || col[4] < 0 || col[0] >= n || col[1] >= n ||
                col[2] >= n || col[3] >= n || col[4] >= n) {
                continue;
            }
            model = (int)strtol(field[col[0]], NULL, 0);
            file = (int)strtol(field[col[1]], NULL, 0);
            tex = (int)strtol(field[col[2]], NULL, 0);
            part = strcmp(field[col[3]], "eyes") == 0 ? 1 : strcmp(field[col[3]], "brows") == 0 ? 2
                 : strcmp(field[col[3]], "fur") == 0 ? 3 : 0;
            sModGodKey[sModGodRows] = (model & 0xFF) << 16 | (file & 0xF) << 12 | (tex & 0xFF) << 4 | part;
            sModGodMaskTab[sModGodRows] = (unsigned short)strtol(field[col[4]], NULL, 0);
            sModGodRows++;
        }
        fclose(fp);
    }
}

static void Port_ModGodLoad(void) {
    if (!sModGodLoaded) {
        sModGodLoaded = 1;
        Port_ModLoadGods();
    }
}

/* The model character `chara` shows in god form `form` (1 SSG, 2 SSGSS, 3 Rose, 4 Super Saiyan 4 Limit Breaker), -1
   when it has none. */
int Port_ModGodModel(int chara, int form) {
    Port_ModGodLoad();
    if (chara < 0 || chara >= 256 || form < 1 || form > 4) {
        return -1;
    }
    return sModGodModel[chara][form];
}

/* The entries of palette `texture` of model `model`, file `file`, that are hair (part 0), eyes (1), eyebrows (2) or
   fur (3). */
int Port_ModGodMask(int model, int file, int texture, int part) {
    int key = (model & 0xFF) << 16 | (file & 0xF) << 12 | (texture & 0xFF) << 4 | part;
    int i;

    Port_ModGodLoad();
    for (i = 0; i < sModGodRows; i++) {
        if (sModGodKey[i] == key) {
            return sModGodMaskTab[i];
        }
    }
    return 0;
}

static const char *const sModGodName[5] = { "", "ssg", "ssgss", "rose", "ssj4lb" };

static float Port_ModGodValue(int form, const char *what, float def) {
    char key[64];

    snprintf(key, sizeof(key), "god_%s_%s", sModGodName[form], what);
    return Port_ModValue(key, def);
}

/* The power level of god form `form` of character `chara` (the model it shows): its line's base power level times
   god_<form>_multiplier; Super Saiyan 4 Limit Breaker: the Super Saiyan 4 form's own power level times it. */
float Port_ModGodLevel(int chara, int form) {
    static const float mult[5] = { 1.0f, 5000000.0f, 50000000.0f, 50000000.0f, 1000.0f };  /* above Gogeta SS4 */
    int b;

    Port_ModLoad();
    if (chara < 0 || chara >= 256 || form < 1 || form > 4) {
        return 1.0f;
    }
    if (form == 4) {
        return (sModHave[chara] ? sModLevel[chara] : 1.0f) * Port_ModGodValue(form, "multiplier", mult[form]);
    }
    b = sModHave[chara] ? sModBase[chara] : chara;
    return (sModHave[b] ? sModLevel[b] : 1.0f) * Port_ModGodValue(form, "multiplier", mult[form]);
}

/* Stamina (points of 100 per second) the form `chara` drains, or god form `god` when it is not 0
   (god_<form>_stamina_drain); 0 when the form drains none. */
float Port_ModStaminaDrain(int chara, int god) {
    static const float godDrain[5] = { 0.0f, 0.2f, 1.2f, 1.2f, 1.5f };

    Port_ModLoad();
    if (Port_ModValue("transform_stamina_drain", 1.0f) == 0.0f) {
        return 0.0f;
    }
    if (god >= 1 && god <= 4) {
        return Port_ModGodValue(god, "stamina_drain", godDrain[god]);
    }
    return chara >= 0 && chara < 256 && sModHave[chara] ? sModDrain[chara] : 0.0f;
}

/* The form `chara` goes back to when the stamina is out (power_levels.csv stamina_base), itself when none. */
int Port_ModBaseOf(int chara) {
    Port_ModLoad();
    return chara >= 0 && chara < 256 && sModHave[chara] && sModStaminaBase[chara] >= 0 ? sModStaminaBase[chara] : chara;
}

/* The aura (and powered-up glow) type of god form `form`: one of the game's own, 3 red (Kaioken's), 5 light blue,
   6 pink (Rose, Limit Breaker). */
int Port_ModGodAuraType(int form) {
    static const float type[5] = { 0.0f, 3.0f, 5.0f, 6.0f, 6.0f };

    if (form < 1 || form > 4) {
        return -1;
    }
    return (int)Port_ModGodValue(form, "aura_type", type[form]);
}

/* Tints `count` colours (four floats each, 0..1: the aura's or the glow's colours of this frame) of god form `form`
   to god_<form>_aura_tint_hue (degrees; -1 = untouched): the hue only; white and grey (saturation below 0.12) stay.
   With the colours all of one hue, the flames' palette (shared, the red aura's pink and violet) shows in that hue. */
static void Port_ModRgbToHsv(float r, float g, float b, float *h, float *s, float *v);
static void Port_ModHsvToRgb(float h, float s, float v, float *r, float *g, float *b);
static float Port_ModClamp01(float x);

void Port_ModGodTint(float *vec, int count, int form) {
    static const float tint[5] = { -1.0f, 0.0f, -1.0f, -1.0f, 338.0f };
    float hue;
    int i;

    if (form < 1 || form > 4) {
        return;
    }
    hue = Port_ModGodValue(form, "aura_tint_hue", tint[form]);
    if (hue < 0.0f) {
        return;
    }
    for (i = 0; i < count; i++) {
        float *c = vec + i * 4;
        float h, s, v;

        Port_ModRgbToHsv(Port_ModClamp01(c[0]), Port_ModClamp01(c[1]), Port_ModClamp01(c[2]), &h, &s, &v);
        if (s < 0.12f) {
            continue;
        }
        Port_ModHsvToRgb(hue / 360.0f, s, v, &c[0], &c[1], &c[2]);
    }
}

/* Whether god form `form` keeps the power-up glow (the max power shimmer) on: god_<form>_glow (Super Saiyan God 1). */
int Port_ModGodGlow(int form) {
    static const float glow[5] = { 0, 0, 0, 0, 0 };  /* off: the shimmer reads as the dash effect */

    if (form < 1 || form > 4) {
        return 0;
    }
    return Port_ModGodValue(form, "glow", glow[form]) != 0.0f;
}

/* The glow's colours of god form `form` (four floats each): turned to god_<form>_glow_hue (Super Saiyan God 12:
   Gogeta SS4's red-orange), at least god_<form>_glow_saturation saturated; a hue of -1 takes the aura's tint. */
void Port_ModGodGlowTint(float *vec, int count, int form) {
    static const float hueDef[5] = { -1.0f, 12.0f, -1.0f, -1.0f, -1.0f };
    static const float satDef[5] = { 0.0f, 0.82f, 0.0f, 0.0f, 0.0f };
    float hue;
    float sat;
    int i;

    if (form < 1 || form > 4) {
        return;
    }
    hue = Port_ModGodValue(form, "glow_hue", hueDef[form]);
    if (hue < 0.0f) {
        Port_ModGodTint(vec, count, form);
        return;
    }
    sat = Port_ModGodValue(form, "glow_saturation", satDef[form]);
    for (i = 0; i < count; i++) {
        float *c = vec + i * 4;
        float h, s, v;

        Port_ModRgbToHsv(Port_ModClamp01(c[0]), Port_ModClamp01(c[1]), Port_ModClamp01(c[2]), &h, &s, &v);
        if (s < 0.12f) {
            continue;  /* white and grey stay */
        }
        Port_ModHsvToRgb(hue / 360.0f, s < sat ? sat : s, v, &c[0], &c[1], &c[2]);
    }
}

/* The transformation (characters.csv kind: 8 Frieza / Cooler, 10 Bojack / Zarbon, 11 Super Saiyan 2 / 3 / 4, ...)
   whose effect god form `form` plays: god_<form>_transform_kind (SSG 8, Cooler's red; SSGSS 1, Uub's blue (also the
   way back to the base form); Rose 10, Bojack's purple; Limit Breaker 11, Super Saiyan 4's own, in pink). */
int Port_ModGodTransformKind(int form) {
    static const float kind[5] = { 11, 8, 1, 10, 11 };
    int k;

    if (form < 1 || form > 4) {
        return 11;
    }
    k = (int)Port_ModGodValue(form, "transform_kind", kind[form]);
    return k >= 1 && k <= 11 ? k : 11;  /* (1 is also a transformation down: same effect) */
}

static void Port_ModStoreRgb(unsigned char *rgb, float r, float g, float b);

static const float sGodFxHue[5] = { -1, -1, -1, -1, 338 }, sGodFxSat[5] = { 0, 0, 0, 0, 0.6f };
static const float sGodFxLift[5] = { 0, 0.6f, 0, 0, 0 }, sGodFxLiftHue[5] = { -1, 0, -1, -1, -1 };
static const float sGodFxLiftSat[5] = { 0, 0.8f, 0, 0, 0 };

/* Whether god form `form` changes the colours of its transformation effect at all. */
int Port_ModGodFxOn(int form) {
    if (form < 1 || form > 4) {
        return 0;
    }
    return Port_ModGodValue(form, "transform_hue", sGodFxHue[form]) >= 0.0f ||
           Port_ModGodValue(form, "transform_lift", sGodFxLift[form]) > 0.0f;
}

/* One colour (0..1) of god form `form`'s transformation effect, `alpha` 0..1 how opaque it is: its saturated colours
   turned to god_<form>_transform_hue (-1 keeps them) at least transform_saturation saturated; then a dark colour at
   least `minAlpha` opaque (a palette: a quarter; a particle: transform_lift_alpha, 0 = every one, also the faint black
   the particles fade to, which stacks into a dark ring) lifted to transform_lift brightness, in transform_lift_hue
   (-1 its own) at least transform_lift_saturation saturated: SSG's red without the dark smoke, brighter. */
static void Port_ModGodFxColor(float *r, float *g, float *b, float alpha, float minAlpha, int form) {
    float hue = Port_ModGodValue(form, "transform_hue", sGodFxHue[form]);
    float sat = Port_ModGodValue(form, "transform_saturation", sGodFxSat[form]);
    float lift = Port_ModGodValue(form, "transform_lift", sGodFxLift[form]);
    float liftHue = Port_ModGodValue(form, "transform_lift_hue", sGodFxLiftHue[form]);
    float liftSat = Port_ModGodValue(form, "transform_lift_saturation", sGodFxLiftSat[form]);
    float h, s, v;

    Port_ModRgbToHsv(Port_ModClamp01(*r), Port_ModClamp01(*g), Port_ModClamp01(*b), &h, &s, &v);
    if (hue >= 0.0f && s >= 0.12f) {
        h = hue / 360.0f;
        s = s < sat ? sat : s;
    }
    if (lift > 0.0f && alpha >= minAlpha && v < lift) {
        v = lift;
        if (liftHue >= 0.0f) {
            h = liftHue / 360.0f;
            s = s < liftSat ? liftSat : s;
        }
    }
    Port_ModHsvToRgb(h, Port_ModClamp01(s), Port_ModClamp01(v), r, g, b);
}

/* A palette colour (bytes; alpha 0..128) of the effect. */
void Port_ModGodFxRecolor(unsigned char *rgba, int form) {
    float r = rgba[0] / 255.0f, g = rgba[1] / 255.0f, b = rgba[2] / 255.0f;

    if (form < 1 || form > 4) {
        return;
    }
    Port_ModGodFxColor(&r, &g, &b, rgba[3] / 128.0f, 0.25f, form);  /* a texture: only what shows (no squares) */
    Port_ModStoreRgb(rgba, r, g, b);
}

/* A particle colour (four floats: red, green, blue 0..255, alpha 0..255) of the effect. */
void Port_ModGodFxRecolorF(float *rgba, int form) {
    float r = rgba[0] / 255.0f, g = rgba[1] / 255.0f, b = rgba[2] / 255.0f;

    if (form < 1 || form > 4 || (rgba[0] == 0.0f && rgba[1] == 0.0f && rgba[2] == 0.0f && rgba[3] == 0.0f)) {
        return;  /* an unused (all zero) slot of the table */
    }
    Port_ModGodFxColor(&r, &g, &b, rgba[3] / 255.0f, Port_ModGodValue(form, "transform_lift_alpha", 0.0f), form);
    rgba[0] = r * 255.0f;
    rgba[1] = g * 255.0f;
    rgba[2] = b * 255.0f;
}

/* debug_memory = 1: one line in mod_memory_log.txt as a battle sets up its fighters: the port's large blocks in use
   (Port_LowUsed, plat_mem.c) and the console heap's free bytes. */
void Port_ModMemLog(int heapFree) {
    extern unsigned int Port_LowUsed(void);
    static FILE *log;
    static int battle;

    if (Port_ModValue("debug_memory", 0.0f) == 0.0f) {
        return;
    }
    if (log == NULL) {
        log = fopen("mod_memory_log.txt", "w");
        if (log == NULL) {
            return;
        }
        fprintf(log, "battle  large blocks in use (KB)  console heap free (KB)\n");
    }
    fprintf(log, "%6d  %25u  %22d\n", ++battle, Port_LowUsed() / 1024u, heapFree / 1024);
    fflush(log);
}

/* Diagnostic (debug_god_fx = 1): one line in mod_fx_log.txt. */
void Port_ModGodLog(const char *what, int a, int b, int c, int d) {
    if (Port_ModValue("debug_god_fx", 0.0f) == 0.0f || sModFxLines >= 20000) {
        return;
    }
    if (sModFxLog == NULL) {
        sModFxLog = fopen("mod_fx_log.txt", "w");
        if (sModFxLog == NULL) {
            sModFxLines = 20000;
            return;
        }
    }
    fprintf(sModFxLog, "%u %s %d %d %d %d\n", (unsigned int)(now_ns() / 1000000ull), what, a, b, c, d);
    sModFxLines++;
    fflush(sModFxLog);
}

/* The blast stocks god form `form` costs. */
int Port_ModGodCost(int form) {
    static const float cost[5] = { 0.0f, 2.0f, 3.0f, 3.0f, 3.0f };

    if (form < 1 || form > 4) {
        return 0;
    }
    return (int)Port_ModGodValue(form, "cost", cost[form]);
}

static void Port_ModRgbToHsv(float r, float g, float b, float *h, float *s, float *v) {
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d = mx - mn;

    *v = mx;
    *s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 0.0f) {
        *h = 0.0f;
    } else if (mx == r) {
        *h = (g - b) / d / 6.0f;
    } else if (mx == g) {
        *h = ((b - r) / d + 2.0f) / 6.0f;
    } else {
        *h = ((r - g) / d + 4.0f) / 6.0f;
    }
    if (*h < 0.0f) {
        *h += 1.0f;
    }
}

static void Port_ModHsvToRgb(float h, float s, float v, float *r, float *g, float *b) {
    float f;
    float p;
    float q;
    float t;
    int i;

    h = (h - (float)(int)h) * 6.0f;
    i = (int)h;
    f = h - (float)i;
    p = v * (1.0f - s);
    q = v * (1.0f - s * f);
    t = v * (1.0f - s * (1.0f - f));
    switch (i % 6) {
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
    }
}

static float Port_ModClamp01(float x) {
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

static void Port_ModStoreRgb(unsigned char *rgb, float r, float g, float b) {
    rgb[0] = (unsigned char)(Port_ModClamp01(r) * 255.0f + 0.5f);
    rgb[1] = (unsigned char)(Port_ModClamp01(g) * 255.0f + 0.5f);
    rgb[2] = (unsigned char)(Port_ModClamp01(b) * 255.0f + 0.5f);
}

/* Recolours one palette colour (r, g, b bytes) for god form `form`: part 0 hair (and eyebrows), part 1 eyes. Golden
   hair keeps its shading with the form's hue; black hair (value below 0.3) is lifted to dark_floor + value x dark_gain. */
void Port_ModGodRecolor(unsigned char *rgb, int form, int part) {
    /* Super Saiyan God: Gogeta SS4's hair, red-orange (hue 12, 0.82 saturated, 0.52 .. 0.94 bright) */
    static const float hue[5] = { 0, 12, 195, 328, 352 }, satMin[5] = { 0, 0.82f, 0.7f, 0.38f, 0.6f };
    static const float satScale[5] = { 0, 1.0f, 0.9f, 0.92f, 1.0f }, valScale[5] = { 0, 0.94f, 1.0f, 1.0f, 1.0f };
    static const float darkFloor[5] = { 0, 0.52f, 0.35f, 0.35f, 0.45f }, darkGain[5] = { 0, 2.6f, 2.4f, 2.4f, 2.4f };
    static const float eyeHue[5] = { 0, 0, 200, 330, 0 }, eyeSat[5] = { 0, 0.55f, 0.85f, 0.3f, 0.5f };
    float h, s, v, r, g, b;

    if (form < 1 || form > 4) {
        return;
    }
    Port_ModRgbToHsv(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, &h, &s, &v);
    if (part == 1) {
        h = Port_ModGodValue(form, "eye_hue", eyeHue[form]) / 360.0f;
        s = Port_ModGodValue(form, "eye_saturation", eyeSat[form]);
    } else if (part == 2) {  /* eyebrows (and pupils drawn with them): the hair's hue, at least brow_value bright */
        float mn = Port_ModGodValue(form, "saturation_min", satMin[form]);
        float bv = Port_ModGodValue(form, "brow_value", 0.85f);

        h = Port_ModGodValue(form, "hue", hue[form]) / 360.0f;
        s = s < mn ? mn : s;
        v = v < bv ? bv : v;
    } else {
        float mn = Port_ModGodValue(form, "saturation_min", satMin[form]);

        h = Port_ModGodValue(form, "hue", hue[form]) / 360.0f;
        s = s * Port_ModGodValue(form, "saturation_scale", satScale[form]);
        s = s < mn ? mn : s;
        v = v < 0.3f ? Port_ModGodValue(form, "dark_floor", darkFloor[form]) + v * Port_ModGodValue(form, "dark_gain", darkGain[form])
                     : v * Port_ModGodValue(form, "value_scale", valScale[form]);
    }
    Port_ModHsvToRgb(h, Port_ModClamp01(s), Port_ModClamp01(v), &r, &g, &b);
    Port_ModStoreRgb(rgb, r, g, b);
}

/* The hair colours of one palette (the entries of `mask` in the 16 colours at `clut`) for god form `form`. With
   god_<form>_hair_ramp on (Super Saiyan God) they become a ramp, like Gogeta SS4's hair: each colour's place is the
   share of the texture's pixels (`counts`, per colour) darker than it, raised to ramp_gamma (below 1: mostly bright,
   the darkest pixels drawing the strands, as Gogeta's 85% bright hair); place 0 takes ramp_dark (brightness) /
   ramp_sat_dark, 1 ramp_bright / ramp_sat_bright. A palette of one colour (hair drawn flat, Goten) takes the place
   ramp_flat (0 dark .. 1 bright). Off: each colour by Port_ModGodRecolor. */
void Port_ModGodRecolorHair(unsigned char *clut, int mask, int form, const int *counts) {
    static const float ramp[5] = { 0, 1, 0, 0, 1 };
    float v[16];
    float lo = 2.0f;
    float hi = -1.0f;
    float hue, dark, bright, sDark, sBright, gamma, flat;
    float total = 0.0f;
    int j;
    int q;

    if (form < 1 || form > 4 || mask == 0) {
        return;
    }
    if (Port_ModGodValue(form, "hair_ramp", ramp[form]) == 0.0f) {
        for (j = 0; j < 16; j++) {
            if (mask & (1 << j)) {
                Port_ModGodRecolor(clut + j * 4, form, 0);
            }
        }
        return;
    }
    hue = Port_ModGodValue(form, "hue", 12.0f) / 360.0f;
    dark = Port_ModGodValue(form, "ramp_dark", 0.52f);
    bright = Port_ModGodValue(form, "ramp_bright", 0.94f);
    sDark = Port_ModGodValue(form, "ramp_sat_dark", 0.89f);
    sBright = Port_ModGodValue(form, "ramp_sat_bright", 0.82f);
    gamma = Port_ModGodValue(form, "ramp_gamma", 0.25f);
    flat = Port_ModGodValue(form, "ramp_flat", 1.0f);
    for (j = 0; j < 16; j++) {
        float h, s;

        Port_ModRgbToHsv(clut[j * 4] / 255.0f, clut[j * 4 + 1] / 255.0f, clut[j * 4 + 2] / 255.0f, &h, &s, &v[j]);
        if (mask & (1 << j)) {
            lo = v[j] < lo ? v[j] : lo;
            hi = v[j] > hi ? v[j] : hi;
            total += (float)counts[j];
        }
    }
    for (j = 0; j < 16; j++) {
        float t, r, g, b;

        if (!(mask & (1 << j))) {
            continue;
        }
        if (hi - lo <= 0.01f) {
            t = flat;
        } else if (total <= 0.0f) {
            t = (v[j] - lo) / (hi - lo);
        } else {
            float below = 0.0f;

            for (q = 0; q < 16; q++) {
                if (mask & (1 << q)) {
                    below += v[q] < v[j] ? (float)counts[q] : v[q] == v[j] ? (float)counts[q] * 0.5f : 0.0f;
                }
            }
            t = below / total;
        }
        t = Port_ModClamp01(t);
        if (gamma > 0.0f && t > 0.0f) {
            t = powf(t, gamma);
        }
        Port_ModHsvToRgb(hue, Port_ModClamp01(sDark + (sBright - sDark) * t), Port_ModClamp01(dark + (bright - dark) * t),
                         &r, &g, &b);
        Port_ModStoreRgb(clut + j * 4, r, g, b);
    }
}

/* The fur colours of one palette (the entries of `mask` in the 16 colours at `clut`) for god form `form` (Super
   Saiyan 4 Limit Breaker: the red fur turned magenta-pink): a ramp in god_<form>_fur_hue from fur_dark (brightness; the
   palette's darkest fur colour, saturation fur_sat_dark) to fur_bright (its brightest, fur_sat_bright), by each
   colour's brightness between them, so every model's fur (Goku's red, Vegeta's crimson, Gogeta's maroon, the other
   costumes' grey, gold or orange) takes the same tone with its own shading. */
void Port_ModGodRecolorFur(unsigned char *clut, int mask, int form) {
    float v[16];
    float lo = 2.0f;
    float hi = -1.0f;
    float hue, dark, bright, sDark, sBright;
    int j;

    if (form < 1 || form > 4 || mask == 0) {
        return;
    }
    hue = Port_ModGodValue(form, "fur_hue", 330.0f) / 360.0f;
    dark = Port_ModGodValue(form, "fur_dark", 0.42f);
    bright = Port_ModGodValue(form, "fur_bright", 0.82f);
    sDark = Port_ModGodValue(form, "fur_sat_dark", 0.8f);
    sBright = Port_ModGodValue(form, "fur_sat_bright", 0.62f);
    for (j = 0; j < 16; j++) {
        float h, s;

        Port_ModRgbToHsv(clut[j * 4] / 255.0f, clut[j * 4 + 1] / 255.0f, clut[j * 4 + 2] / 255.0f, &h, &s, &v[j]);
        if (mask & (1 << j)) {
            lo = v[j] < lo ? v[j] : lo;
            hi = v[j] > hi ? v[j] : hi;
        }
    }
    for (j = 0; j < 16; j++) {
        float t, r, g, b;

        if (!(mask & (1 << j))) {
            continue;
        }
        t = hi - lo > 0.01f ? (v[j] - lo) / (hi - lo) : 0.6f;
        Port_ModHsvToRgb(hue, Port_ModClamp01(sDark + (sBright - sDark) * t), Port_ModClamp01(dark + (bright - dark) * t),
                         &r, &g, &b);
        Port_ModStoreRgb(clut + j * 4, r, g, b);
    }
}

/* Recolours one colour of the charge particles' palette (the golden or pale blue swirl) for god form `form`: the
   form's charge hue, at least its charge saturation; white, grey-free black and the alpha stay. */
void Port_ModGodChargeRecolor(unsigned char *rgb, int form) {
    static const float hue[5] = { 0, 2, 195, 322, 338 }, satMin[5] = { 0, 0.78f, 0.55f, 0.5f, 0.6f };
    float h, s, v, r, g, b;
    float mn;

    if (form < 1 || form > 4) {
        return;
    }
    Port_ModRgbToHsv(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, &h, &s, &v);
    if (s < 0.04f || v < 0.02f) {
        return;
    }
    mn = Port_ModGodValue(form, "charge_saturation", satMin[form]);
    h = Port_ModGodValue(form, "charge_hue", hue[form]) / 360.0f;
    Port_ModHsvToRgb(h, Port_ModClamp01(s < mn ? mn : s), v, &r, &g, &b);
    Port_ModStoreRgb(rgb, r, g, b);
}

/* Port_ModGodChargeRecolor for a particle colour of three floats 0..255 (the colour tables of an effect). */
void Port_ModGodChargeRecolorF(float *rgb, int form) {
    unsigned char c[3];
    int k;

    for (k = 0; k < 3; k++) {
        float v = rgb[k] < 0.0f ? 0.0f : rgb[k] > 255.0f ? 255.0f : rgb[k];

        c[k] = (unsigned char)(v + 0.5f);
    }
    Port_ModGodChargeRecolor(c, form);
    for (k = 0; k < 3; k++) {
        rgb[k] = (float)c[k];
    }
}

static void Port_ModLoad(void) {
    if (!sModLoaded) {
        int i;
        int n = 0;

        sModLoaded = 1;
        Port_ModLoadValues();
        Port_ModLoadPower();
        for (i = 0; i < 256; i++) {
            n += sModHave[i];
        }
        /* integers only: a float passed to the C library's fprintf is not read right from software-float code */
        fprintf(stderr, "bt3: mod: %d values (mod_values.txt), %d power levels (power_levels.csv); "
                "attack_speed_strongest = %d/1000, damage_max_multiplier = %d/1000, power of id 0 = %d\n", sModCount, n,
                (int)(Port_ModValue("attack_speed_strongest", -1.0f) * 1000.0f),
                (int)(Port_ModValue("damage_max_multiplier", -1.0f) * 1000.0f), sModHave[0] ? (int)sModLevel[0] : -1);
    }
}

float Port_ModValue(const char *key, float def) {
    int i;

    Port_ModLoad();
    for (i = 0; i < sModCount; i++) {
        if (strcmp(sModKey[i], key) == 0) {
            return sModVal[i];
        }
    }
    return def;
}

int Port_ModPower(int chara, float *level, float *damage, int *health) {
    Port_ModLoad();
    if (chara < 0 || chara >= 256 || !sModHave[chara]) {
        return 0;
    }
    *level = sModLevel[chara];
    *damage = sModDamage[chara];
    *health = sModHealth[chara];
    return 1;
}

/* The speed of attack animations for power level `pl`: attack_speed_weakest at the weakest level of
   power_levels.csv, attack_speed_strongest at the strongest, on a log scale between them. lo / hi are the weakest
   and strongest levels built into the program, used when there is no power_levels.csv. */
float Port_ModAttackSpeed(float pl, float lo, float hi) {
    float slow = Port_ModValue("attack_speed_weakest", 1.0f);
    float fast = Port_ModValue("attack_speed_strongest", 1.35f);
    float t;
    int i;
    int any = 0;

    Port_ModLoad();
    for (i = 0; i < 256; i++) {
        if (sModHave[i]) {
            if (!any || sModLevel[i] < lo) lo = sModLevel[i];
            if (!any || sModLevel[i] > hi) hi = sModLevel[i];
            any = 1;
        }
    }
    if (pl <= 0.0f || lo <= 0.0f || hi <= lo) {
        return slow;
    }
    t = (logf(pl) - logf(lo)) / (logf(hi) - logf(lo));  /* logf: bridged (port_libm.h); log10f is not */
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return slow + (fast - slow) * t;
}

/* Mod "Ultra Instinct" v56: slower time for stronger fighters (setting power_time, off unless turned on in the Cheats tab;
   never online). The battle reports each fighter's power level every frame (Port_PowerReport, btl_char_action.c);
   the stronger one sets the pace: 100% at power level 100 or less down to 50% at 100 trillion, on a log scale. The
   pace of the vertical blanks only: the fight is computed as always. A report older than 0.2 s counts for nothing
   (menus, pause). */
PORT_HOST static float sPowerPl[2];
PORT_HOST static unsigned long long sPowerAt;
PORT_HOST static int sPowerTime = -1;
extern int Port_Setting(const char *name, int def);
extern void Port_SettingSave(const char *name, int value);
void Port_PowerReport(int side, float pl) {
    if (side >= 0 && side < 2) {
        sPowerPl[side] = pl;
        sPowerAt = now_ns();
    }
}
int Port_PowerTime(void) {
    if (sPowerTime < 0) {
        sPowerTime = Port_Setting("power_time", 0) != 0;
    }
    return sPowerTime;
}
void Port_PowerTimeSet(int on) {
    sPowerTime = on != 0;
    Port_SettingSave("power_time", sPowerTime);
}
/* The pace in thousandths (1000 = normal). log10 by decades and a linear rest, which is close enough here. */
static int Port_PowerPaceMilli(void) {
    float pl = sPowerPl[0] > sPowerPl[1] ? sPowerPl[0] : sPowerPl[1];
    float lg = 0.0f;
    float t;

    if (!Port_PowerTime() || sPowerAt == 0 || now_ns() - sPowerAt > 200000000ull) {
        return 1000;
    }
    while (pl >= 10.0f && lg < 40.0f) {
        pl /= 10.0f;
        lg += 1.0f;
    }
    lg += pl >= 1.0f ? (pl - 1.0f) / 9.0f : 0.0f;
    {
        float from = Port_ModValue("slow_time_from_power_log10", 2.0f);
        float to = Port_ModValue("slow_time_to_power_log10", 14.0f);

        t = to > from ? (lg - from) / (to - from) : 0.0f;
    }
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return (int)(1000.0f - (1000.0f - 1000.0f * Port_ModValue("slow_time_slowest_speed", 0.5f)) * t);
}

extern int Port_NetSpeedMilli(void);
extern int Port_NetSession(void);
extern int Port_NetActive(void);
extern int Port_Setting(const char *name, int def); /* plat_settings.c */
extern void Port_SettingSave(const char *name, int value);
PORT_HOST static int sSpeedPercent = -1;
int Port_GameSpeed(void) {
    if (sSpeedPercent < 0) {
        int v = Port_Setting("game_speed", 100);
        sSpeedPercent = v < 50 ? 50 : v > 200 ? 200 : v;
    }
    return sSpeedPercent;
}
void Port_GameSpeedSet(int percent) {
    percent = percent < 50 ? 50 : percent > 200 ? 200 : percent;
    if (percent != Port_GameSpeed()) {
        sSpeedPercent = percent;
        Port_SettingSave("game_speed", percent);
    }
}
int Port_SpeedMilli(void) {
    return Port_NetSession() || Port_NetActive() ? Port_NetSpeedMilli()
                                                 : Port_GameSpeed() * Port_PowerPaceMilli() / 100;
}
/* A vertical blank's length: 16.68 ms at the game's own pace. */
#define BLANK_NS (16683350ull * 1000ull / (unsigned long long)Port_SpeedMilli())
unsigned long long Port_BlankNs(void) { return BLANK_NS; }

static void vblank_wait(void) {
    struct timespec ts;
    unsigned long long t = now_ns(), after;

    if (gPortPaceShiftNs != 0) {
        if (next != 0) {
            next += gPortPaceShiftNs;
        }
        gPortPaceShiftNs = 0;
    }

    if (next != 0 && t >= next && t - next < 100000000ull) {
        /* Late, but not hopelessly: this blank has passed already, so do not wait, and keep the grid. A fight
           frame waits for two blanks; when its work takes longer than one period (16.7 ms) but less than two, the
           first wait is skipped here and the second one ends on time, so the frame still lasts 33.4 ms. (Starting
           a new grid instead, as this did before, added a full period to every such frame: a machine that needed
           20 ms per frame ran the fight at 27 frames per second and unevenly, though it was within the budget.) */
        after = t;
        next += BLANK_NS;
        sPaceLate++;
    } else if (next > t && next - t < 100000000ull) {
#ifdef _WIN32
        /* Windows' sleep is only good to about a millisecond, and only once the timer resolution has been asked
           for: sleep to within a millisecond and a half of the moment, then give the rest away in small slices. */
        static int asked;
        if (!asked) {
            asked = 1;
            timeBeginPeriod(1);
        }
        if (next - t > 1500000ull) {
            Sleep((DWORD)((next - t - 1500000ull) / 1000000ull));
        }
        while (now_ns() < next) {
            Sleep(0);
        }
#else
        ts.tv_sec = 0;
        ts.tv_nsec = (long)(next - t);
        nanosleep(&ts, NULL);
#endif
        after = now_ns();
        gPortSleptNs += after - t; /* the time really spent waiting (a sleep can return early or late) */
        next += BLANK_NS;
    } else {
        after = t;
        next = t + BLANK_NS; /* the first one, or more than a tenth of a second behind: a new grid from now */
        sPaceReset++;
    }
    /* BT3_GS_VERBOSE: once a second, how evenly the vertical blanks came (they should be 16.68 ms apart) */
    if (sPacePrev != 0) {
        unsigned long long d = after - sPacePrev;
        if (d < sPaceMin) { sPaceMin = d; }
        if (d > sPaceMax) { sPaceMax = d; }
        if (d > 18000000ull || d < 15400000ull) { sPaceOff++; }
    }
    sPacePrev = after;
    if (++sPaceCount == 60) {
        if (getenv("BT3_GS_VERBOSE") != NULL) {
            /* (whole numbers: this file is built with the game's software float, which has no 64-bit conversions) */
            fprintf(stderr, "pace: 60 vertical blanks: %u.%02u to %u.%02u ms apart, %u reached late (not waited for), %u new grids\n",
                    (unsigned)(sPaceMin / 1000000ull), (unsigned)(sPaceMin / 10000ull % 100), (unsigned)(sPaceMax / 1000000ull),
                    (unsigned)(sPaceMax / 10000ull % 100), sPaceLate, sPaceReset);
        }
        sPaceCount = sPaceOff = sPaceLate = sPaceReset = 0;
        sPaceMin = ~0ull;
        sPaceMax = 0;
    }
}

#undef next
static unsigned char sPadLast[2][18]; /* (defined with the pads below) */
void Port_VBlank(void) {
    {
        extern void Port_SessionPoll(void); /* gs/state.c: the state at the first blank is kept; an online session
                                               begins or ends here */
        Port_SessionPoll();
    }
    {
        extern void Port_LobbyAuto(void); /* gs/net.c: BT3_LOBBY, the room-code lobby without its window (tests) */
        Port_LobbyAuto();
    }
    /* BT3_PACED=1: real-time pacing without a window too (sound tests) */
    {
        extern int Port_NetSession(void), Port_NetWarp(void); /* gs/net.c */
        extern int gPortResim;                                /* gs/state.c: no picture, no sound */
        int warp = Port_NetWarp();
        extern int Port_NetResim(void); /* blanks being run again after a rollback: not shown, not timed either */
        int again = Port_NetResim();
        if (Port_NetSession() || again) {
            gPortResim = warp || again; /* an online session's start-up up to the versus menu is not shown and not timed */
        }
        {
            /* gs/gs_draw.c: the pictures of the tick that follow its first (in-between pictures and the real one):
               those due before this blank now, those due at it after the wait */
            extern void GsGpu_InterpPump(int ahead_ms, int all);
            /* (not while blanks are run again after a rollback or run ahead for an online session: those are
               not shown and not timed, and waiting for a picture here would slow the catching up) */
            if (GsGpu_Enabled() && !warp && !again) {
                GsGpu_InterpPump(-1, 0); /* (-1: what can be finished before the blank) */
            }
            if ((GsGpu_Enabled() || getenv("BT3_PACED") != NULL) && getenv("BT3_UNCAPPED") == NULL && !warp && !again) {
                vblank_wait();
            }
            if (GsGpu_Enabled() && !warp && !again) {
                GsGpu_InterpPump(6, 0);
            }
        }
    }
    {   /* BT3_PAD_TABLE=<file>: what each pad read during the blank that ends here, 36 bytes per blank (the input
           of a session by blank and by pad: gs/net.c plays one player's column of it for its tests) */
        PORT_HOST static FILE *table = NULL;
        PORT_HOST static int tried = 0;
        if (!tried) {
            tried = 1;
            table = getenv("BT3_PAD_TABLE") != NULL ? fopen(getenv("BT3_PAD_TABLE"), "wb") : NULL;
        }
        if (table != NULL) {
            fwrite(sPadLast, 1, sizeof(sPadLast), table);
            fflush(table);
        }
    }
    gPortVBlanks++;
    {
        extern int gPortResim;
        if (!gPortResim) {
            gPortLiveBlanks++; /* (the meter: the game's speed is this per second, of 60) */
        }
    }
    {
        extern void Port_NetBeginTick(unsigned tick); /* gs/net.c: online play */
        Port_NetBeginTick(gPortVBlanks);
    }
    {
        extern void Port_SyncTest(unsigned vblank); /* gs/state.c: BT3_SYNCTEST */
        extern void Port_AdxTick(void);             /* plat_sndstate.c */
        Port_AdxTick();
        Port_SyncTest(gPortVBlanks);
    }
    Port_Trace(gPortVBlanks);
    if (sVsyncHandler != NULL) {
        sVsyncHandler(0);
    }
}
int sceGsSyncV() { Port_VBlank(); return 0; }

/* ---- pads ---- */
int sceDbcInit() { return 1; }
int scePad2Init() { return 1; }
static int sPadSockets;
int scePad2CreateSocket() { return sPadSockets++; }
int scePad2GetState() { return 1; }            /* connected and ready */
/* A DualShock 2's button profile: every digital button, both sticks, every pressure-sensitive button. */
int scePad2GetButtonProfile(int socket, unsigned char *profile) {
    (void)socket;
    profile[0] = 0xFF; profile[1] = 0xFF; profile[2] = 0xFF; profile[3] = 0x03;
    return 4;
}
/* With a window: keyboard and gamepads (port/src/gs/gs_input.c). Headless: nobody touches the pad. Buttons are
   active-low; the sticks rest at 0x80. */
extern int Port_PadRead(int socket, unsigned char *data);

/* Recording and playing back a whole session's controller input:
       BT3_PAD_REC=<file>    every pad read of this run is appended to the file (18 bytes per read, both ports)
       BT3_PAD_PLAY=<file>   the reads are answered from the file instead; after its end the pads are idle
   The game reads the pads a fixed number of times per vertical blank and everything else is deterministic, so a
   recording made from the title screen replays the same menus and the same fight, with or without a window
   (given the same save folder to start from). For reproducing what a player saw. */
PORT_HOST static FILE *sPadRec = NULL, *sPadPlay = NULL; /* (the position in the playback file IS restored: Port_PadPlaySeek) */
PORT_HOST static int sPadFilesTried = 0;

/* Where the playback of a pad recording stands, and going back there (the state save / restore test re-runs
   frames: they have to get the same input again). */
long Port_PadPlayPos(void) { return sPadPlay != NULL ? ftell(sPadPlay) : -1; }
/* (a position of -1 was taken before the file was open: its start) */
void Port_PadPlaySeek(long pos) { if (sPadPlay != NULL) { fseek(sPadPlay, pos >= 0 ? pos : 0, SEEK_SET); } }

/* The pads as the game last read them (for BT3_PAD_TABLE, below). */
static unsigned char sPadLast[2][18] = {{0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80}, {0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80}};
static int pad_read(int socket, unsigned char *data);

int scePad2Read(int socket, unsigned char *data) {
    extern int Port_NetActive(void);                       /* gs/net.c: online play */
    extern void Port_NetInput(int player, unsigned char *data);
    int n;

    if (Port_NetActive()) {
        Port_NetInput(socket, data); /* both pads come from the exchange with the other player */
        n = 18;
    } else {
        n = pad_read(socket, data);
    }
    if (socket >= 0 && socket < 2) {
        memcpy(sPadLast[socket], data, 18);
    }
    return n;
}

static int pad_read(int socket, unsigned char *data) {
    int i;

    if (!sPadFilesTried) {
        sPadFilesTried = 1;
        if (getenv("BT3_PAD_PLAY") != NULL) {
            sPadPlay = fopen(getenv("BT3_PAD_PLAY"), "rb");
        } else if (getenv("BT3_PAD_REC") != NULL) {
            sPadRec = fopen(getenv("BT3_PAD_REC"), "wb");
        }
    }
    if (sPadPlay != NULL) {
        /* After the recording's end the pads are idle (below), for the rest of the run: the keyboard and the
           controllers are not read. (They used to be, from the end on: two runs of one recording then differed
           by whatever a controller happened to report, which looked like the game not repeating itself.) The
           file stays open: a state restored to before the end reads the recording's last part again. */
        PORT_HOST static int said = 0;
        Port_StateTouch(data, 18);
        if (fread(data, 1, 18, sPadPlay) == 18) {
            return 18;
        }
        if (!said) {
            said = 1;
            fprintf(stderr, "bt3: the recorded input has ended\n");
        }
    } else if (Port_PadRead(socket, data)) {
        if (sPadRec != NULL) {
            fwrite(data, 1, 18, sPadRec);
            fflush(sPadRec);
        }
        return 18;
    }
    data[0] = 0xFF; data[1] = 0xFF;
    for (i = 2; i < 6; i++) { data[i] = 0x80; }
    for (i = 6; i < 18; i++) { data[i] = 0; }
    if (sPadRec != NULL) {
        fwrite(data, 1, 18, sPadRec);
        fflush(sPadRec);
    }
    return 18;
}
int sceVibGetProfile() { return 0; }
int sceVibSetActParam() { return 0; }

/* ---- MPEG movies: port/src/plat_movie.c ---- */

/* ---- widescreen ----
   The picture's shape, as width over height times 1000 (1333 = 4:3, the console's). Wider shapes keep the height
   of view and add to the sides:
       BT3_ASPECT=21:9 (or 16:9, 32:9, ...)    the shape asked for
       BT3_WIDE=1                               16:9          (BT3_WIDE=0: 4:3 whatever else is said)
       BT3_WINDOW=3440x1440                     a window wider than 3:2 asks for its own shape
   The game's projection, culling and effect proportions take Port_WideFactor() (src/battle/btl_cam.c, stg_a.c,
   eft_*.c, all #ifdef PORT); the renderer keeps 2D art in proportion and shows the picture at this shape
   (port/src/gs/gs_gpu.c). */
/* (PORT_HOST: the player's setting, not the game's state. Without it the shape was saved and put back with the
   game: a session starts by going back to the state of the program's first moments, so a shape chosen in the
   settings window during the run went back to what it was at start, 4:3 on a first run. Reported as "joining a
   match resets the screen to 4:3".) */
PORT_HOST static int sAspectMilli = -1;
extern int Port_Setting(const char *name, int def); /* plat_settings.c */
int Port_AspectMilli(void) {
    if (sAspectMilli < 0) {
        int a = 0, b = 0, w = 0, h = 0;
        sAspectMilli = Port_Setting("aspect_milli", 1333); /* the saved choice; the variables below win */
        if (getenv("BT3_WIDE") != NULL && atoi(getenv("BT3_WIDE")) == 0) {
            sAspectMilli = 1333; /* 4:3 */
        } else if (getenv("BT3_ASPECT") != NULL && sscanf(getenv("BT3_ASPECT"), "%d:%d", &a, &b) == 2 && a > 0 && b > 0) {
            sAspectMilli = a * 1000 / b;
        } else if (getenv("BT3_WIDE") != NULL) {
            sAspectMilli = 1778;
        } else if (getenv("BT3_WINDOW") != NULL && sscanf(getenv("BT3_WINDOW"), "%dx%d", &w, &h) == 2 && h > 0 && w * 2 > h * 3) {
            sAspectMilli = w * 1000 / h;
        }
        if (sAspectMilli < 1333) { sAspectMilli = 1333; }
        if (sAspectMilli > 4000) { sAspectMilli = 4000; }
    }
    {
        /* BT3_ASPECT_AT=<frame>:<w>:<h> (testing): from that frame on the shape is w:h, as if it had been changed in
           the settings window at that moment (a run without a window can then show a change made during a fight). */
        static int at = -2, to;
        extern unsigned gGsFrame; /* gs_core.c */
        if (at == -2) {
            int f = 0, w = 0, h = 0;
            const char *e = getenv("BT3_ASPECT_AT");
            at = -1;
            if (e != NULL && sscanf(e, "%d:%d:%d", &f, &w, &h) == 3 && w > 0 && h > 0) {
                at = f;
                to = w * 1000 / h;
            }
        }
        if (at >= 0 && (int)gGsFrame >= at) {
            return to < 1333 ? 1333 : to > 4000 ? 4000 : to;
        }
    }
    return sAspectMilli;
}
/* The settings overlay changes the shape while the game runs: the game asks every frame. */
void Port_SetAspectMilli(int milli) {
    sAspectMilli = milli < 1333 ? 1333 : milli > 4000 ? 4000 : milli;
}
int Port_IsWide(void) {
    return Port_AspectMilli() > 1340;
}
/* The picture's width over the console's 4:3 width (1 = not wide). */
/* In the menus it is 1: their pages are composed for 4:3 (backdrops, scrolling scenery, 2D and 3D placed against
   each other), so they are shown as they are, stretched to the picture's width (the user's choice). */
extern int gPortMenuMode; /* headless.c */
float Port_WideFactor(void) {
    return Port_IsWide() && !gPortMenuMode ? (float)Port_AspectMilli() * 3.0f / 4000.0f : 1.0f;
}
