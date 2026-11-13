/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "Library.hpp"
#if defined(__ANDROID__) && !defined(CROSS_ANDROID)
#error "The target platform must drive Lua when conditions"
#endif
int axlumAnswer()
{
    return 42;
}
