#include "SystemManager.h"
#include "CrashReporter.h"

#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GalaxySystem", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GalaxySystem", __VA_ARGS__)

namespace mobile_system {

void SystemManager::installCrashHandlers() {
    installCrashReporter();
}

void SystemManager::pinThreadToPerformanceCores() {
#if defined(__linux__) || defined(__ANDROID__)
    // Prompt requirement: use the platform Energy Aware Scheduler (EAS) by default
    // unless an explicit override is requested via environment or debug property.
    const char* forceAffinity = getenv("GALAXY_FORCE_CPU_AFFINITY");
    if (!forceAffinity || strcmp(forceAffinity, "1") != 0) {
        return;
    }

    int numCores = (int)sysconf(_SC_NPROCESSORS_CONF);
    if (numCores <= 1) return;

    int maxFreqs[32] = {0};
    int overallMax = 0, overallMin = 0x7FFFFFFF, validCount = 0;

    for (int i = 0; i < numCores && i < 32; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        FILE* f = fopen(path, "r");
        if (f) {
            int freq = 0;
            if (fscanf(f, "%d", &freq) == 1 && freq > 0) {
                maxFreqs[i] = freq;
                if (freq > overallMax) overallMax = freq;
                if (freq < overallMin) overallMin = freq;
                validCount++;
            }
            fclose(f);
        }
    }

    if (validCount > 1 && overallMax > overallMin) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        int bigCount = 0, threshold = (int)(overallMax * 0.85f);
        for (int i = 0; i < numCores && i < 32; i++) {
            if (maxFreqs[i] >= threshold) {
                CPU_SET(i, &cpuset);
                bigCount++;
            }
        }
        if (bigCount > 0) {
            int rc = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
            if (rc != 0) {
                LOGI("pinThreadToPerformanceCores: sched_setaffinity returned %d", rc);
            }
        }
    }
#endif
}

void SystemManager::elevateThreadPriority(int priority) {
    setpriority(PRIO_PROCESS, 0, priority);
}

extern "C" void port_pin_thread_to_performance_cores(void) {
    mobile_system::SystemManager::pinThreadToPerformanceCores();
    mobile_system::SystemManager::elevateThreadPriority(-10);
}

void SystemManager::triggerHaptic(android_app* app, int ms) {
    if (ms <= 0 || !app || !app->activity || !app->activity->vm || !app->activity->clazz) return;
    JavaVM* vm = app->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    jint getEnvRes = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvRes == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
        attached = true;
    } else if (getEnvRes != JNI_OK || !env) {
        return;
    }

    if (env->ExceptionCheck()) env->ExceptionClear();
    jclass clazz = env->GetObjectClass(app->activity->clazz);
    if (clazz) {
        jmethodID mid = env->GetStaticMethodID(clazz, "triggerHaptic", "(I)V");
        if (mid) {
            env->CallStaticVoidMethod(clazz, mid, (jint)ms);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(clazz);
    }
    if (attached) vm->DetachCurrentThread();
}

void SystemManager::returnToLauncher(android_app* app) {
    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz) return;
    JavaVM* vm = app->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    jint getEnvRes = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvRes == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
        attached = true;
    } else if (getEnvRes != JNI_OK || !env) {
        return;
    }

    if (env->ExceptionCheck()) env->ExceptionClear();
    jclass clazz = env->GetObjectClass(app->activity->clazz);
    if (clazz) {
        jmethodID mid = env->GetStaticMethodID(clazz, "returnToLauncher", "()V");
        if (mid) {
            env->CallStaticVoidMethod(clazz, mid);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(clazz);
    }
    if (attached) vm->DetachCurrentThread();
    ANativeActivity_finish(app->activity);
}

static android_app* sSavedApp = nullptr;

void SystemManager::setPreferredRefreshRate(android_app* app, float targetFps) {
    if (app) sSavedApp = app;
    else app = sSavedApp;

    if (app && app->window) {
        typedef int32_t (*PFN_ANativeWindow_setFrameRate)(ANativeWindow*, float, int8_t);
        static auto sSetFrameRate = (PFN_ANativeWindow_setFrameRate)dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate");
        if (sSetFrameRate) {
            sSetFrameRate(app->window, targetFps, 0);
        }
    }

    static float sLastAppliedFps = -1.0f;
    if (fabsf(sLastAppliedFps - targetFps) < 0.5f) return;

    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz) return;
    sLastAppliedFps = targetFps;

    JavaVM* vm = app->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    jint getEnvRes = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvRes == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
        attached = true;
    } else if (getEnvRes != JNI_OK || !env) {
        return;
    }

    if (env->ExceptionCheck()) env->ExceptionClear();
    jclass clazz = env->GetObjectClass(app->activity->clazz);
    if (clazz) {
        jmethodID mid = env->GetStaticMethodID(clazz, "setPreferredRefreshRate", "(F)V");
        if (mid) {
            env->CallStaticVoidMethod(clazz, mid, (jfloat)targetFps);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(clazz);
    }
    if (attached) vm->DetachCurrentThread();
}

void SystemManager::enforceOrientation(android_app* app, int orientationChoice) {
    if (!app || !app->activity) return;
    int requested = 6;  // SCREEN_ORIENTATION_SENSOR_LANDSCAPE
    if (orientationChoice == 1) requested = 0;       // SCREEN_ORIENTATION_LANDSCAPE
    else if (orientationChoice == 2) requested = 8;  // SCREEN_ORIENTATION_REVERSE_LANDSCAPE

    ANativeActivity* act = app->activity;
    JNIEnv* env = nullptr;
    act->vm->AttachCurrentThread(&env, nullptr);
    if (env) {
        jclass actClass = env->GetObjectClass(act->clazz);
        if (actClass) {
            jmethodID setOrient = env->GetMethodID(actClass, "setRequestedOrientation", "(I)V");
            if (setOrient) {
                env->CallVoidMethod(act->clazz, setOrient, requested);
            }
            env->DeleteLocalRef(actClass);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        act->vm->DetachCurrentThread();
    }
}

}  // namespace mobile_system
