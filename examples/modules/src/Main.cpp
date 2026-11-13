/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "Logic.hpp"
#include <iostream>
#ifdef BASE_PRIVATE
#error "Private module definition leaked into target"
#endif
int main()
{
    std::cout << "module result=" << logicValue() << '\n';
}
