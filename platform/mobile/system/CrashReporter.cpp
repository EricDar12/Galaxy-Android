#include "CrashReporter.h"

#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>

#include <stdint.h>

namespace mobile_system {

namespace {

const char kCrashLogPath[] = "/sdcard/GalaxyData/galaxy_crash.log";
constexpr int kMaxFrames = 24;
constexpr size_t kAltStackSize = 64 * 1024;

// The alternate stack only covers this thread; other game threads still get
// basic signal reporting, just not stack-overflow survival.
alignas(16) char sAltStack[kAltStackSize];
char sDeviceInfo[512];
volatile sig_atomic_t sHandling = 0;

void writeAll(int fd, const char* text) {
    size_t left = text ? strlen(text) : 0;
    while (left > 0) {
        ssize_t n = write(fd, text, left);
        if (n <= 0) return;
        text += n;
        left -= (size_t)n;
    }
}

void emit(int fd, const char* line) {
    if (fd >= 0) writeAll(fd, line);
    __android_log_write(ANDROID_LOG_FATAL, "GalaxyCrash", line);
}

void buildDeviceInfo() {
    char model[PROP_VALUE_MAX] = {0};
    char manufacturer[PROP_VALUE_MAX] = {0};
    char release[PROP_VALUE_MAX] = {0};
    char sdk[PROP_VALUE_MAX] = {0};
    char abi[PROP_VALUE_MAX] = {0};
    char soc[PROP_VALUE_MAX] = {0};
    char board[PROP_VALUE_MAX] = {0};
    __system_property_get("ro.product.model", model);
    __system_property_get("ro.product.manufacturer", manufacturer);
    __system_property_get("ro.build.version.release", release);
    __system_property_get("ro.build.version.sdk", sdk);
    __system_property_get("ro.product.cpu.abi", abi);
    __system_property_get("ro.soc.model", soc);
    __system_property_get("ro.board.platform", board);
    snprintf(sDeviceInfo, sizeof(sDeviceInfo),
             "device=%s %s android=%s sdk=%s abi=%s soc=%s board=%s\n",
             manufacturer, model, release, sdk, abi, soc, board);
}

struct BacktraceState {
    uintptr_t frames[kMaxFrames];
    int count;
};

_Unwind_Reason_Code collectFrame(_Unwind_Context* context, void* arg) {
    BacktraceState* state = (BacktraceState*)arg;
    uintptr_t pc = (uintptr_t)_Unwind_GetIP(context);
    if (pc != 0 && state->count < kMaxFrames) state->frames[state->count++] = pc;
    return state->count >= kMaxFrames ? _URC_END_OF_STACK : _URC_NO_REASON;
}

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGABRT: return "SIGABRT";
    default: return "UNKNOWN";
    }
}

void crashHandler(int sig, siginfo_t* info, void* rawContext) {
    if (sHandling) {
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }
    sHandling = 1;

    char threadName[64] = "unknown";
    prctl(PR_GET_NAME, threadName, 0, 0, 0);

    int fd = open(kCrashLogPath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0660);
    char line[320];
    snprintf(line, sizeof(line), "\n[%lld] FATAL %d (%s) tid=%d thread=%s addr=%p\n",
             (long long)time(nullptr), sig, signalName(sig), (int)gettid(), threadName,
             info ? info->si_addr : nullptr);
    emit(fd, line);
    emit(fd, sDeviceInfo);

    ucontext_t* context = (ucontext_t*)rawContext;
    if (context) {
#if defined(__aarch64__)
        snprintf(line, sizeof(line), "pc=%p lr=%p sp=%p\n",
                 (void*)context->uc_mcontext.pc, (void*)context->uc_mcontext.regs[30],
                 (void*)context->uc_mcontext.sp);
        emit(fd, line);
#endif
    }

    // The unwinder and dladdr are best-effort inside a signal handler; the
    // debuggerd tombstone remains authoritative for the full stack.
    BacktraceState state = {};
    _Unwind_Backtrace(collectFrame, &state);
    snprintf(line, sizeof(line), "backtrace (%d frames):\n", state.count);
    emit(fd, line);
    for (int i = 0; i < state.count; i++) {
        uintptr_t pc = state.frames[i];
        Dl_info dl = {};
        if (dladdr((void*)pc, &dl) && dl.dli_fbase) {
            uintptr_t offset = pc - (uintptr_t)dl.dli_fbase;
            const char* module = dl.dli_fname ? dl.dli_fname : "<unknown>";
            if (dl.dli_sname && dl.dli_sname[0]) {
                snprintf(line, sizeof(line), "  #%02d pc %p %s+0x%lx (%s)\n", i, (void*)pc,
                         module, (unsigned long)offset, dl.dli_sname);
            } else {
                snprintf(line, sizeof(line), "  #%02d pc %p %s+0x%lx\n", i, (void*)pc,
                         module, (unsigned long)offset);
            }
        } else {
            snprintf(line, sizeof(line), "  #%02d pc %p\n", i, (void*)pc);
        }
        emit(fd, line);
    }

    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }

    // Preserve Android's own tombstone: the default action runs after the
    // report above, so debuggerd still gets the crash.
    signal(sig, SIG_DFL);
    raise(sig);
}

void installHandler(int sig) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = crashHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigfillset(&action.sa_mask);
    sigaction(sig, &action, nullptr);
}

}  // namespace

void installCrashReporter() {
    buildDeviceInfo();

    stack_t stack;
    memset(&stack, 0, sizeof(stack));
    stack.ss_sp = sAltStack;
    stack.ss_size = sizeof(sAltStack);
    sigaltstack(&stack, nullptr);

    mkdir("/sdcard/GalaxyData", 0770);
    installHandler(SIGSEGV);
    installHandler(SIGBUS);
    installHandler(SIGFPE);
    installHandler(SIGILL);
    installHandler(SIGABRT);

    __android_log_print(ANDROID_LOG_INFO, "GalaxyCrash", "crash reporter installed: %s",
                        sDeviceInfo);
}

}  // namespace mobile_system
