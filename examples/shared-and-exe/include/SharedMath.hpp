/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once
#ifdef _WIN32
#ifdef SHARED_MATH_EXPORTS
#define SHARED_API __declspec(dllexport)
#else
#define SHARED_API __declspec(dllimport)
#endif
#else
#define SHARED_API __attribute__((visibility("default")))
#endif
extern "C" SHARED_API int sharedMathTwice(int x);
