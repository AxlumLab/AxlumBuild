/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once
#if defined(_WIN32) && !defined(FEATURE_EXPORTS)
#define FEATURE_API __declspec(dllimport)
#else
#define FEATURE_API
#endif
extern "C" FEATURE_API int featureValue();
