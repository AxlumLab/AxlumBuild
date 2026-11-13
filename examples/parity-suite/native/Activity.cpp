/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#ifdef __ANDROID__
#include <android/native_activity.h>

extern "C" __attribute__((visibility("default"))) void ANativeActivity_onCreate(ANativeActivity*, void*, size_t) {}
#elif defined(_WIN32)
extern "C" __declspec(dllexport) int androidEntryStub()
{
    return 0;
}
#else
extern "C" int androidEntryStub()
{
    return 0;
}
#endif
