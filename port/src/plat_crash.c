/*
 * Crash report for the PC build: on a fatal signal, writes the signal and a backtrace (function names; the build
 * is linked without PIE and with its symbols) to the terminal and to bt3_crash.txt in the current directory, then
 * lets the default action happen. So a crash someone else hits can be diagnosed from one file.
 */
#ifdef _WIN32
/* Windows: the exception code, the faulting address and the return addresses on the stack. The program is linked
   at a fixed address (0x20000000), so the numbers can be looked up in bt3.exe.map of the same build. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
extern const char *Port_DataSummary(void); /* plat_verify.c */

extern const char *volatile gPortStage; /* plat_mem.c: how far the start got */

/* The report is written with the system's own file calls and no C library (the crash may be inside it), from a
   VECTORED handler: those are called first, before the system checks the stack, so a crash on a stack the system
   does not like (the game runs on its own stack below 4 GB) is still reported. The handler only reports; what
   happens to the exception afterwards is unchanged. */
static HANDLE sCrashOut[2];

static void crash_put(const char *s) {
    DWORD n = 0, done, k;
    while (s[n] != '\0') {
        n++;
    }
    for (k = 0; k < 2; k++) {
        if (sCrashOut[k] != NULL && sCrashOut[k] != INVALID_HANDLE_VALUE) {
            WriteFile(sCrashOut[k], s, n, &done, NULL);
        }
    }
}

static void crash_hex(unsigned long long v) {
    char b[20];
    int i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 16; i++) {
        b[2 + i] = "0123456789ABCDEF"[(v >> (60 - i * 4)) & 15];
    }
    b[18] = '\0';
    crash_put(b);
}

/* " (in <module file> + offset)" for an address */
static void crash_where(void *addr) {
    HMODULE m = NULL;
    char name[MAX_PATH];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &m) && m != NULL &&
        GetModuleFileNameA(m, name, sizeof(name)) != 0) {
        crash_put(" (");
        crash_put(name);
        crash_put(" + ");
        crash_hex((unsigned long long)((char *)addr - (char *)m));
        crash_put(")");
    }
}

/* Mod "Ultra Instinct" v112 (diagnostic, debug_stack_watch in mod_values.txt): the crash of v108 was the return of
   Quat_Unpack with its saved registers and return address overwritten by float data while it ran, which only another
   thread can do. Hardware write watchpoints (the processor's debug registers: four slots of 8 bytes) on those stack
   slots (debug_stack_watch_0..3: bytes below the top of the game thread's stack, 0 = unused) are set in every thread
   of the process except the game's own; a write there stops the writing thread right after the instruction, which
   is logged to bt3_stackwatch.txt (its address, the thread's name and start, the registers, a backtrace), and it
   goes on. The threads are looked over once a second. */
#include <string.h>
#include <stdlib.h>
#include <tlhelp32.h>
static DWORD sWatchGame;
static uintptr_t sWatchBase;
static uintptr_t sWatchAddr[4];
static int sWatchCount;
static volatile LONG sWatchHits;

static void watch_put(HANDLE f, const char *s) {
    DWORD d;

    WriteFile(f, s, (DWORD)strlen(s), &d, NULL);
}

static void watch_addr(HANDLE f, const char *label, ULONG_PTR a) {
    char b[MAX_PATH + 96];
    char name[MAX_PATH];
    HMODULE m = NULL;

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) &&
        m != NULL && GetModuleFileNameA(m, name, sizeof(name)) != 0) {
        snprintf(b, sizeof(b), "%s0x%016llX (%s + 0x%llX)\r\n", label, (unsigned long long)a, name,
                 (unsigned long long)(a - (ULONG_PTR)m));
    } else {
        snprintf(b, sizeof(b), "%s0x%016llX\r\n", label, (unsigned long long)a);
    }
    watch_put(f, b);
}

/* v115: the program's folder, with its separator (the files go there: started from elsewhere, the game moves there only
   later) */
static char sWatchDir[MAX_PATH];

static HANDLE watch_file(const char *name) {
    char path[MAX_PATH + 64];

    snprintf(path, sizeof(path), "%s%s", sWatchDir, name);
    return CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                       NULL);
}

static HANDLE watch_open(void) {
    return watch_file("bt3_stackwatch.txt");
}

/* the thread's name (SDL names its threads) and where it started */
static void watch_thread_info(HANDLE f, HANDLE h) {
    typedef HRESULT(WINAPI * GetDescFn)(HANDLE, PWSTR *);
    typedef LONG(WINAPI * QueryFn)(HANDLE, int, PVOID, ULONG, PULONG);
    GetDescFn gd = (GetDescFn)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription");
    QueryFn qi = (QueryFn)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    PWSTR desc = NULL;
    ULONG_PTR start = 0;
    char b[200];

    if (gd != NULL && SUCCEEDED(gd(h, &desc)) && desc != NULL) {
        snprintf(b, sizeof(b), "  name \"%ls\"\r\n", desc);
        watch_put(f, b);
        LocalFree(desc);
    }
    if (qi != NULL && qi(h, 9, &start, sizeof(start), NULL) == 0) { /* ThreadQuerySetWin32StartAddress */
        watch_addr(f, "  started at ", start);
    }
}

static LONG WINAPI on_watch(EXCEPTION_POINTERS *e) {
    CONTEXT *c = e->ContextRecord;
    LONG n;

    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || (c->Dr6 & 0xF) == 0 || sWatchCount == 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    n = InterlockedIncrement(&sWatchHits);
    if (n <= 40) {
        HANDLE f = watch_open();

        if (f != INVALID_HANDLE_VALUE) {
            char b[640];
            void *frames[32];
            USHORT k, nf;
            int i;

            snprintf(b, sizeof(b), "\r\n== write %ld into the game's stack: thread %lu, watch slots hit 0x%llX\r\n", (long)n,
                     (unsigned long)GetCurrentThreadId(), (unsigned long long)(c->Dr6 & 0xF));
            watch_put(f, b);
            watch_thread_info(f, GetCurrentThread());
            watch_addr(f, "  right after the instruction at ", (ULONG_PTR)c->Rip);
            for (i = 0; i < sWatchCount; i++) {
                snprintf(b, sizeof(b), "  slot %d at 0x%llX now holds 0x%016llX\r\n", i, (unsigned long long)sWatchAddr[i],
                         (unsigned long long)*(volatile uint64_t *)sWatchAddr[i]);
                watch_put(f, b);
            }
            snprintf(b, sizeof(b),
                     "  rax %llX rbx %llX rcx %llX rdx %llX\r\n  rsi %llX rdi %llX rbp %llX rsp %llX\r\n"
                     "  r8 %llX r9 %llX r10 %llX r11 %llX\r\n  r12 %llX r13 %llX r14 %llX r15 %llX\r\n  backtrace:\r\n",
                     (unsigned long long)c->Rax, (unsigned long long)c->Rbx, (unsigned long long)c->Rcx,
                     (unsigned long long)c->Rdx, (unsigned long long)c->Rsi, (unsigned long long)c->Rdi,
                     (unsigned long long)c->Rbp, (unsigned long long)c->Rsp, (unsigned long long)c->R8,
                     (unsigned long long)c->R9, (unsigned long long)c->R10, (unsigned long long)c->R11,
                     (unsigned long long)c->R12, (unsigned long long)c->R13, (unsigned long long)c->R14,
                     (unsigned long long)c->R15);
            watch_put(f, b);
            nf = RtlCaptureStackBackTrace(0, 32, frames, NULL);
            for (k = 0; k < nf; k++) {
                watch_addr(f, "    ", (ULONG_PTR)frames[k]);
            }
            CloseHandle(f);
        }
    }
    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* a value of mod_values.txt, read here (the mod's own loader runs on the game thread) */
static float watch_value(const char *key, float def) {
    char path[MAX_PATH + 32];
    FILE *fp;
    char line[512];
    size_t n = strlen(key);
    float v = def;

    snprintf(path, sizeof(path), "%smod_values.txt", sWatchDir);
    fp = fopen(path, "r");

    if (fp == NULL) {
        return def;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, key, n) == 0 && (line[n] == ' ' || line[n] == '=' || line[n] == '\t')) {
            char *eq = strchr(line, '=');

            if (eq != NULL) {
                v = (float)atof(eq + 1);
            }
        }
    }
    fclose(fp);
    return v;
}

/* v115: the freeze watch (debug_freeze_watch): when the game draws no new frame (gGsFrame) for 3 s, five samples of the
   game thread 0.2 s apart go to bt3_freeze.txt: its instruction and the program's code addresses on its stack (the
   return addresses among them). The thread is held only while its registers and stack are copied (nothing else is
   called meanwhile, so this cannot lock it up); the copy is read afterwards. At most 10 reports a run. */
extern unsigned gGsFrame;
static unsigned char sFreezeStack[0x10000];

static void watch_freeze(void) {
    static unsigned last;
    static int still;
    static int reported;
    static int reports;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)dos + dos->e_lfanew);
    uintptr_t lo = (uintptr_t)dos;
    uintptr_t hi = lo + nt->OptionalHeader.SizeOfImage;
    unsigned now = gGsFrame;
    HANDLE th;
    HANDLE f;
    char b[400];
    int smp;

    if (now != last) {
        last = now;
        still = 0;
        reported = 0;
        return;
    }
    if (++still < 3 || reported || reports >= 10 || now == 0) {
        return;
    }
    reported = 1;
    reports++;
    th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, sWatchGame);
    if (th == NULL) {
        return;
    }
    f = watch_file("bt3_freeze.txt");
    if (f == INVALID_HANDLE_VALUE) {
        CloseHandle(th);
        return;
    }
    snprintf(b, sizeof(b), "\r\n==== the game drew no new frame for %d s (frame %u; mod build v115)\r\n", still, now);
    watch_put(f, b);
    for (smp = 0; smp < 5; smp++) {
        CONTEXT ctx;
        size_t n = 0;
        int got = 0;

        memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_FULL;
        if (SuspendThread(th) != (DWORD)-1) {
            if (GetThreadContext(th, &ctx)) {
                MEMORY_BASIC_INFORMATION mbi;
                uintptr_t sp = (uintptr_t)ctx.Rsp;

                if (VirtualQuery((void *)sp, &mbi, sizeof(mbi)) != 0 && mbi.State == MEM_COMMIT) {
                    uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;

                    n = end - sp > sizeof(sFreezeStack) ? sizeof(sFreezeStack) : end - sp;
                    memcpy(sFreezeStack, (void *)sp, n);
                }
                got = 1;
            }
            ResumeThread(th);
        }
        if (got) {
            size_t k;
            int shown = 0;

            snprintf(b, sizeof(b),
                     "-- sample %d: rsp %llX rbp %llX\r\n   rax %llX rbx %llX rcx %llX rdx %llX rsi %llX rdi %llX\r\n"
                     "   r8 %llX r9 %llX r10 %llX r11 %llX r12 %llX r13 %llX r14 %llX r15 %llX\r\n",
                     smp, (unsigned long long)ctx.Rsp, (unsigned long long)ctx.Rbp, (unsigned long long)ctx.Rax,
                     (unsigned long long)ctx.Rbx, (unsigned long long)ctx.Rcx, (unsigned long long)ctx.Rdx,
                     (unsigned long long)ctx.Rsi, (unsigned long long)ctx.Rdi, (unsigned long long)ctx.R8,
                     (unsigned long long)ctx.R9, (unsigned long long)ctx.R10, (unsigned long long)ctx.R11,
                     (unsigned long long)ctx.R12, (unsigned long long)ctx.R13, (unsigned long long)ctx.R14,
                     (unsigned long long)ctx.R15);
            watch_put(f, b);
            watch_addr(f, "   at ", (ULONG_PTR)ctx.Rip);
            for (k = 0; k + 8 <= n && shown < 60; k += 8) {
                uint64_t v = *(uint64_t *)(sFreezeStack + k);

                if (v > lo && v < hi) {
                    char lab[48];

                    snprintf(lab, sizeof(lab), "   [rsp+0x%llX] ", (unsigned long long)k);
                    watch_addr(f, lab, (ULONG_PTR)v);
                    shown++;
                }
            }
        }
        Sleep(200);
    }
    CloseHandle(f);
    CloseHandle(th);
}

static DWORD WINAPI watch_thread(LPVOID arg) {
    static const float defs[4] = { 1520.0f, 1512.0f, 1488.0f, 1480.0f };
    int freezeOn;
    int stackOff = 0;
    static DWORD armed[1024];
    int nArmed = 0;
    DWORD self = GetCurrentThreadId();
    DWORD pid = GetCurrentProcessId();
    HANDLE f;
    char b[200];
    int i;

    (void)arg;
    {
        char *slash;

        if (GetModuleFileNameA(NULL, sWatchDir, sizeof(sWatchDir) - 1) == 0) {
            sWatchDir[0] = '\0';
        }
        slash = strrchr(sWatchDir, '\\');
        if (slash != NULL) {
            slash[1] = '\0';
        } else {
            sWatchDir[0] = '\0';
        }
    }
    freezeOn = watch_value("debug_freeze_watch", 1.0f) != 0.0f;
    if (watch_value("debug_stack_watch", 1.0f) == 0.0f) {
        if (!freezeOn) {
            return 0;
        }
        stackOff = 1;
    }
    for (i = 0; i < 4 && !stackOff; i++) {
        char key[32];
        float off;

        snprintf(key, sizeof(key), "debug_stack_watch_%d", i);
        off = watch_value(key, defs[i]);
        if (off >= 8.0f && off < 0x100000) {
            sWatchAddr[sWatchCount++] = (sWatchBase - (uintptr_t)off) & ~(uintptr_t)7;
        }
    }
    f = watch_open();
    if (f != INVALID_HANDLE_VALUE) {
        snprintf(b, sizeof(b), "\r\n==== started (v115): game thread %lu, its stack's top 0x%llX; %d slots:", (unsigned long)sWatchGame,
                 (unsigned long long)sWatchBase, sWatchCount);
        watch_put(f, b);
        for (i = 0; i < sWatchCount; i++) {
            snprintf(b, sizeof(b), " 0x%llX", (unsigned long long)sWatchAddr[i]);
            watch_put(f, b);
        }
        watch_put(f, "\r\n");
        CloseHandle(f);
    }
    for (;;) {
        HANDLE snap = sWatchCount > 0 ? CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0) : INVALID_HANDLE_VALUE;
        THREADENTRY32 te;

        if (snap != INVALID_HANDLE_VALUE) {
            te.dwSize = sizeof(te);
            if (Thread32First(snap, &te)) {
                do {
                    int seen = 0;
                    int k;
                    HANDLE h;

                    if (te.th32OwnerProcessID != pid || te.th32ThreadID == sWatchGame || te.th32ThreadID == self) {
                        continue;
                    }
                    for (k = 0; k < nArmed; k++) {
                        if (armed[k] == te.th32ThreadID) {
                            seen = 1;
                            break;
                        }
                    }
                    if (seen || nArmed >= 1024) {
                        continue;
                    }
                    armed[nArmed++] = te.th32ThreadID;
                    h = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                                   FALSE, te.th32ThreadID);
                    if (h == NULL) {
                        continue;
                    }
                    if (SuspendThread(h) != (DWORD)-1) {
                        CONTEXT ctx;
                        int ok = 0;

                        memset(&ctx, 0, sizeof(ctx));
                        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                        if (GetThreadContext(h, &ctx)) {
                            DWORD64 dr7 = 0;

                            for (k = 0; k < sWatchCount; k++) {
                                dr7 |= (DWORD64)1 << (k * 2);           /* enabled (local) */
                                dr7 |= (DWORD64)1 << (16 + k * 4);      /* on writes */
                                dr7 |= (DWORD64)2 << (18 + k * 4);      /* 8 bytes */
                            }
                            ctx.Dr0 = sWatchCount > 0 ? sWatchAddr[0] : 0;
                            ctx.Dr1 = sWatchCount > 1 ? sWatchAddr[1] : 0;
                            ctx.Dr2 = sWatchCount > 2 ? sWatchAddr[2] : 0;
                            ctx.Dr3 = sWatchCount > 3 ? sWatchAddr[3] : 0;
                            ctx.Dr6 = 0;
                            ctx.Dr7 = dr7;
                            ok = SetThreadContext(h, &ctx) != 0;
                        }
                        ResumeThread(h);
                        f = watch_open();
                        if (f != INVALID_HANDLE_VALUE) {
                            snprintf(b, sizeof(b), "watching thread %lu%s\r\n", (unsigned long)te.th32ThreadID,
                                     ok ? "" : " (could not set its debug registers)");
                            watch_put(f, b);
                            watch_thread_info(f, h);
                            CloseHandle(f);
                        }
                    }
                    CloseHandle(h);
                } while (Thread32Next(snap, &te));
            }
            CloseHandle(snap);
        }
        if (freezeOn) {
            watch_freeze();
        }
        Sleep(1000);
    }
    return 0;
}

static LONG WINAPI on_crash(EXCEPTION_POINTERS *e) {
    static LONG once;
    DWORD code = e->ExceptionRecord->ExceptionCode;
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    void *frames[32];
    USHORT n, i;

    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_IN_PAGE_ERROR) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (InterlockedExchange(&once, 1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    sCrashOut[0] = GetStdHandle(STD_ERROR_HANDLE);
    sCrashOut[1] = CreateFileA("bt3_crash.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    crash_put("bt3: crashed: exception ");
    crash_hex(code);
    crash_put(" at ");
    crash_hex((unsigned long long)(uintptr_t)e->ExceptionRecord->ExceptionAddress);
    crash_where(e->ExceptionRecord->ExceptionAddress);
    if (code == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->NumberParameters >= 2) {
        crash_put(e->ExceptionRecord->ExceptionInformation[0] == 1 ? "\r\nwriting address " : e->ExceptionRecord->ExceptionInformation[0] == 8 ? "\r\nexecuting address " : "\r\nreading address ");
        crash_hex(e->ExceptionRecord->ExceptionInformation[1]);
    }
    crash_put("\r\nlast step reached: ");
    crash_put(gPortStage);
    crash_put("\r\nmod build: v115");
#ifdef __x86_64__
    crash_put("\r\nrsp "); crash_hex(e->ContextRecord->Rsp);
    crash_put(" rbp "); crash_hex(e->ContextRecord->Rbp);
    crash_put("\r\nrax "); crash_hex(e->ContextRecord->Rax);
    crash_put(" rbx "); crash_hex(e->ContextRecord->Rbx);
    crash_put(" rcx "); crash_hex(e->ContextRecord->Rcx);
    crash_put(" rdx "); crash_hex(e->ContextRecord->Rdx);
    crash_put("\r\nrsi "); crash_hex(e->ContextRecord->Rsi);
    crash_put(" rdi "); crash_hex(e->ContextRecord->Rdi);
    crash_put(" r8 "); crash_hex(e->ContextRecord->R8);
    crash_put(" r9 "); crash_hex(e->ContextRecord->R9);
#endif
    crash_put("\r\nthread stack as the system knows it: ");
    crash_hex((unsigned long long)(uintptr_t)tib->StackLimit);
    crash_put(" to ");
    crash_hex((unsigned long long)(uintptr_t)tib->StackBase);
    if (Port_DataSummary()[0] != '\0') { /* (plat_verify.c: not the original disc's data) */
        crash_put("\r\n");
        crash_put(Port_DataSummary());
    }
    crash_put("\r\nbacktrace (innermost first; addresses in Tenkaichi3Decomp.exe are looked up in the build's map file):\r\n");
    n = RtlCaptureStackBackTrace(0, 32, frames, NULL);
    for (i = 0; i < n; i++) {
        crash_hex((unsigned long long)(uintptr_t)frames[i]);
        crash_where(frames[i]);
        crash_put("\r\n");
    }
    crash_put("bt3: the same report is in bt3_crash.txt\r\n");
    if (sCrashOut[1] != INVALID_HANDLE_VALUE) {
        CloseHandle(sCrashOut[1]);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* (priority 101: before the other start-up code of the port, which reads the game data and reserves memory) */
__attribute__((constructor(101))) static void crash_init(void) {
    /* The program is a window program (no console window of its own). Started from a terminal, its messages go
       to that terminal; started by the setup with a pipe, the pipe is kept. */
    if (GetStdHandle(STD_OUTPUT_HANDLE) == NULL && AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
    /* nothing held back in a buffer: what was printed before a crash is in the log */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    AddVectoredExceptionHandler(1, on_crash);
    /* Mod "Ultra Instinct" v112: the stack watch (diagnostic): first in line, it lets the watchpoints' stops go on */
    AddVectoredExceptionHandler(1, on_watch);
    sWatchGame = GetCurrentThreadId(); /* the game runs on this thread (on its stack or on one it moves to) */
    sWatchBase = (uintptr_t)((NT_TIB *)NtCurrentTeb())->StackBase;
    CloseHandle(CreateThread(NULL, 0, watch_thread, NULL, 0, NULL));
}
#else
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

extern const char *Port_DataSummary(void); /* plat_verify.c */

static void put(int fd, const char *s) {
    if (write(fd, s, strlen(s)) < 0) {
    }
}

static void on_crash(int sig) {
    void *frames[48];
    int n = backtrace(frames, 48), fd = open("bt3_crash.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644), k;
    const char *name = sig == SIGSEGV ? "SIGSEGV (bad memory access)" : sig == SIGFPE ? "SIGFPE (arithmetic)" :
                       sig == SIGILL ? "SIGILL (bad instruction)" : sig == SIGBUS ? "SIGBUS" : "SIGABRT";

    for (k = 0; k < 2; k++) {
        int out = k == 0 ? 2 : fd;
        if (out < 0) {
            continue;
        }
        put(out, "bt3: crashed: ");
        put(out, name);
        if (Port_DataSummary()[0] != '\0') { /* (plat_verify.c: not the original disc's data) */
            put(out, "\n");
            put(out, Port_DataSummary());
        }
        put(out, "\nbacktrace (innermost first):\n");
        backtrace_symbols_fd(frames, n, out);
    }
    if (fd >= 0) {
        close(fd);
        put(2, "bt3: the same report is in bt3_crash.txt\n");
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

__attribute__((constructor)) static void crash_init(void) {
    static const int sigs[] = {SIGSEGV, SIGFPE, SIGILL, SIGBUS, SIGABRT};
    unsigned i;

    for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        signal(sigs[i], on_crash);
    }
}
#endif
