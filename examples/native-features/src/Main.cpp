/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "Feature.hpp"
#include <fstream>
#include <iostream>
#include <string>
extern "C" int helperC();

int main()
{
    std::string data;
    std::ifstream("config.txt") >> data;
    std::cout << "value=" << featureValue() + helperC() + PCH_VALUE << " data=" << data << '\n';
    return data.empty() ? 1 : 0;
}
