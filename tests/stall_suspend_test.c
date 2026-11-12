/* The Windows suspended interval tested with fakes on both systems. */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t DWORD;
typedef uint64_t DWORD64;
typedef int BOOL;
typedef void *HANDLE;
typedef size_t SIZE_T;
typedef struct { unsigned ContextFlags; uint64_t Rip, Rsp, Rbp; } CONTEXT;
typedef struct { uint64_t Offset; int Mode; } ADDRESS64;
typedef struct { ADDRESS64 AddrPC, AddrStack, AddrFrame; } STACKFRAME64;
typedef struct { uintptr_t pc[48]; unsigned count; int done; } ThreadStack;
enum { MAX_FRAMES = 48, CONTEXT_FULL = 1, AddrModeFlat = 1, IMAGE_FILE_MACHINE_AMD64 = 1 };
static unsigned char stack_copy[256 * 1024];
static DWORD64 copy_base;
static SIZE_T copy_size;
static int suspended, suspend_fail, context_fail, read_fail, resume_fail, resumed, reads, walks;
static DWORD SuspendThread(HANDLE h) { (void)h; if (suspend_fail) return (DWORD)-1; suspended = 1; return 0; }
static BOOL GetThreadContext(HANDLE h, CONTEXT *c) {
    (void)h; assert(suspended);
    c->Rip = 0x123, c->Rsp = 0x100001, c->Rbp = 0;
    return !context_fail;
}
static HANDLE GetCurrentProcess(void) { return NULL; }
static BOOL ReadProcessMemory(HANDLE h, const void *from, void *to, SIZE_T size, SIZE_T *got) {
    (void)h; (void)from; assert(suspended); reads++;
    assert((unsigned char *)to >= stack_copy && size <= (size_t)(stack_copy + sizeof stack_copy - (unsigned char *)to));
    if (read_fail) { *got = 0; return 0; }
    memset(to, 0x5a, size); *got = size; return 1;
}
static DWORD ResumeThread(HANDLE h) { (void)h; assert(suspended); suspended = 0; resumed++; return resume_fail ? (DWORD)-1 : 1; }
static void walk_read(void) {}
static void walk_table(void) {}
static void walk_base(void) {}
static BOOL StackWalk64(DWORD machine, HANDLE process, HANDLE thread, STACKFRAME64 *f, CONTEXT *c,
                        void (*reader)(void), void (*table)(void), void (*base)(void), void *translate) {
    (void)machine; (void)process; (void)thread; (void)c; (void)reader; (void)table; (void)base; (void)translate;
    assert(!suspended && resumed == 1);
    if (walks++) return 0;
    f->AddrPC.Offset = 0x456, f->AddrStack.Offset += 8;
    return 1;
}
/* Compile-poison additions to this interval. No log, loader lookup or heap call may be added. */
#define malloc forbidden_malloc
#define calloc forbidden_calloc
#define realloc forbidden_realloc
#define free forbidden_free
#define printf forbidden_printf
#define fprintf forbidden_fprintf
#define GetModuleHandleA forbidden_module_lookup
#define RtlLookupFunctionEntry forbidden_function_lookup
@CAPTURE@
#undef printf
int main(void) {
    for (int mode = 0; mode < 5; mode++) {
        ThreadStack s = {{0}, 0, 0};
        suspend_fail = mode == 1, context_fail = mode == 2, read_fail = mode == 3, resume_fail = mode == 4;
        suspended = resumed = reads = walks = 0;
        capture_thread(NULL, &s);
        assert(!suspended);
        assert(resumed == (suspend_fail ? 0 : 1));
        if (suspend_fail || context_fail || resume_fail) assert(!s.count && !s.done && !walks);
        else {
            assert(s.count == 2 && s.pc[0] == 0x123 && s.pc[1] == 0x456 && s.done && walks == 2);
            for (size_t i = 0; i < sizeof stack_copy; i++) assert(stack_copy[i] == 0);
        }
        if (mode == 0) assert(copy_size == sizeof stack_copy && reads == 65);
        if (context_fail || suspend_fail) assert(!reads);
    }
    puts("PASS: Windows capture resumes on context/read failures; walks only after resume; bounded copy, no heap/log/loader calls");
    return 0;
}
