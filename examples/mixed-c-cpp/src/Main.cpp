/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "Facade.hpp"
#ifdef BASE_PUBLIC
#error "A private dependency's compilation interface leaked transitively"
#endif
#include <iostream>

int main()
{
    std::cout << "mixed=" << facadeAdd(4, 5) << '\n';
}
