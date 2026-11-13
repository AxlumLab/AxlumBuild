/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "Library.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    std::string data;
    std::ifstream input("Data.txt");
    std::getline(input, data);
    std::cout << "value=" << libraryValue() << " data=" << data << '\n';
    if (const auto* value = std::getenv("AXLUM_RUN_VALUE"))
        std::cout << "env=" << value << '\n';
    for (int i = 1; i < argc; ++i)
    {
        std::cout << "arg=" << argv[i] << '\n';
        if (std::string(argv[i]) == "--fail")
            return 7;
    }
    return data == "runtime-data" ? 0 : 9;
}
