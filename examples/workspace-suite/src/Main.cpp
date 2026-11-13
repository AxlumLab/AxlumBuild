/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "Utility.hpp"
#include <iostream>
#ifdef UTILITY_PRIVATE
#error "Private project setting leaked"
#endif
int main()
{
    std::cout << "suite=" << utilityValue() << '\n';
}
