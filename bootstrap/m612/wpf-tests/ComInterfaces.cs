using System;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
var assembly = typeof(WpfComHeaderMarker).Assembly;
const string prefix = "MS.Internal.Text.TextInterface.Interfaces.";
string[] names = {"IDWriteFontFileStreamMirror", "IDWriteFontFileLoaderMirror", "IDWriteFontFileEnumeratorMirror", "IDWriteFontCollectionLoaderMirror"};
string[] guids = {"6d4865fe-0ab8-4d91-8f62-5dd6be34a3e0", "727cad4e-d6af-4c9e-8a08-d695b11caa49", "72755049-5ff7-435d-8348-4be97cfa6c7c", "cca920e4-52f0-492b-bfa8-29c72ee0a468"};
int[] counts = {4, 1, 2, 1};
for (int i = 0; i < names.Length; ++i) {
    var type = assembly.GetType(prefix + names[i], true);
    if (!type.IsInterface || !type.IsImport || type.GUID != new Guid(guids[i]) ||
        type.GetCustomAttribute<InterfaceTypeAttribute>().Value != ComInterfaceType.InterfaceIsIUnknown || type.GetMethods().Length != counts[i] ||
        type.GetMethods().Any(m => !m.IsAbstract || !m.IsVirtual || (m.GetMethodImplementationFlags() & MethodImplAttributes.PreserveSig) == 0))
        throw new Exception("upstream DirectWrite COM type/method metadata lost: " + names[i]);
}
var stream = assembly.GetType(prefix + names[0]);
var read = stream.GetMethod("ReadFileFragment").GetParameters();
if (!read[0].IsOut || read[0].ParameterType != typeof(void).MakePointerType().MakePointerType() ||
    !read[1].IsIn || read[1].GetCustomAttribute<MarshalAsAttribute>().Value != UnmanagedType.U8 ||
    !read[2].IsIn || read[2].GetCustomAttribute<MarshalAsAttribute>().Value != UnmanagedType.U8 || !read[3].IsOut)
    throw new Exception("upstream fragment parameter metadata lost");
if (stream.GetMethod("GetFileSize").GetParameters()[0].ParameterType != typeof(ulong).MakePointerType())
    throw new Exception("upstream uint64 pointer signature lost");
var enumerator = assembly.GetType(prefix + names[2]);
var moved = enumerator.GetMethod("MoveNext").GetParameters()[0];
if (!moved.IsOut || moved.ParameterType != typeof(bool).MakeByRefType() || moved.GetCustomAttribute<MarshalAsAttribute>().Value != UnmanagedType.Bool ||
    enumerator.GetMethod("GetCurrentFontFile").GetParameters()[0].ParameterType != typeof(UIntPtr).MakePointerType())
    throw new Exception("upstream BOOL/opaque native interface pointer metadata lost");
var collection = assembly.GetType(prefix + names[3]).GetMethod("CreateEnumeratorFromKey").GetParameters();
if (!collection[2].IsIn || collection[2].GetCustomAttribute<MarshalAsAttribute>().Value != UnmanagedType.U4 ||
    collection[0].ParameterType != typeof(IntPtr) || collection[3].ParameterType != typeof(IntPtr).MakePointerType())
    throw new Exception("upstream collection key/pointer metadata lost");
var classification = assembly.GetType("MS.Internal.Text.TextInterface.IClassification", true);
var parameters = classification.GetMethod("GetCharAttribute").GetParameters();
if (!classification.IsInterface || classification.IsPublic || classification.IsImport || parameters.Length != 7 ||
    parameters.Skip(1).Any(p => !p.IsOut || p.ParameterType != typeof(bool).MakeByRefType()))
    throw new Exception("upstream managed classification callback declaration lost");
Console.WriteLine("upstream WPF COM/classification interface metadata OK");
