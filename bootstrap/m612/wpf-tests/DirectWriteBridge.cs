// SPDX-License-Identifier: MIT
using System;
int count = SourceDirectWriteBridge.FontCount();
if (count <= 0) throw new Exception("DirectWrite did not return system fonts");
// Exercise capacity reporting before handing the bridge a sufficiently large array.
int capacity = SourceDirectWriteBridge.FamilyName(new char[1]);
if (capacity <= 1) throw new Exception("DirectWrite family-name capacity failed");
var buffer = new char[capacity];
if (SourceDirectWriteBridge.FamilyName(buffer) != capacity || buffer[^1] != '\0')
    throw new Exception("DirectWrite family-name write failed");
string name = new string(buffer, 0, capacity - 1);
if (string.IsNullOrWhiteSpace(name)) throw new Exception("empty DirectWrite family name");
try { SourceDirectWriteBridge.FamilyName(Array.Empty<char>()); throw new Exception("empty array accepted"); }
catch (IndexOutOfRangeException) { }
Console.WriteLine($"DirectWrite bridge OK: {count} families; first family {name}");
