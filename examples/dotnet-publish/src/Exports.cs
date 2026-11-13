/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

public static partial class Exports
{
    [LibraryImport("Vendor", EntryPoint = "vendorAdd")]
    private static partial int vendorAdd(int a, int b);

    [UnmanagedCallersOnly(EntryPoint = "axlum_add", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int add(int a, int b) => vendorAdd(a, b);
}
