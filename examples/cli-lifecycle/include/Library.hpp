/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once
#if defined(_WIN32)
#if defined(LIBRARY_BUILD)
#define LIBRARY_API __declspec(dllexport)
#else
#define LIBRARY_API __declspec(dllimport)
#endif
#else
#define LIBRARY_API __attribute__((visibility("default")))
#endif
extern "C" LIBRARY_API int libraryValue();
