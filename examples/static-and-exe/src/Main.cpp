/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "Math.hpp"
#ifdef MATH_PRIVATE
#error "Private compile definition leaked to consuming target"
#endif
#include <iostream>

int main()
{
    std::cout << "result=" << mathAdd(2, 3) << '\n';
}
