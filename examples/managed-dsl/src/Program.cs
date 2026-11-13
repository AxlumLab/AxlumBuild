/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#if !DSL_FEATURE || !GENERATED_VALUE
#error "Managed DefineConstants were not applied"
#endif
using System.Reflection;
using ManagedSample;
string? optional = null;
var metadata = Assembly.GetExecutingAssembly().GetCustomAttributes<AssemblyMetadataAttribute>().Single(a => a.Key == "DSL");
Console.WriteLine($"value={Generated.s_value} metadata={metadata.Value} nullable={optional is null}");
