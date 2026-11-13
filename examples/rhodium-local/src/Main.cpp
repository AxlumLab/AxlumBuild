/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "Wrapper.hpp"
#include <iostream>
#include <sample_headers/Constants.hpp>
#ifndef SAMPLE_PACKAGE
#error "Transitive package interface was not propagated"
#endif
int main()
{
    std::cout << "package result=" << packageValue() + s_sampleConstant << '\n';
}
