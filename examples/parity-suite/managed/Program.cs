/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

using System;
using System.Runtime.InteropServices;

class Program
{
    [DllImport("NativeFacade", EntryPoint = "facadeValue")]
    static extern int value();

    static void Main()
    {
        Console.WriteLine($"modular interop={value()}");
    }
}
