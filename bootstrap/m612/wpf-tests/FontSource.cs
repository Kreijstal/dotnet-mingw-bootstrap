using System;
using System.IO;
using System.Linq;

var assembly = typeof(FontSourceHeaderMarker).Assembly;
const string prefix = "MS.Internal.Text.TextInterface.";
var source = assembly.GetType(prefix + "IFontSource", true);
var factory = assembly.GetType(prefix + "IFontSourceFactory", true);
if (!source.IsInterface || source.IsPublic || !factory.IsInterface || factory.IsPublic)
    throw new Exception("upstream interface identity/visibility lost");
if (source.GetMethod("TestFileOpenable").ReturnType != typeof(void) ||
    source.GetMethod("GetUnmanagedStream").ReturnType != typeof(UnmanagedMemoryStream) ||
    source.GetMethod("GetLastWriteTimeUtc").ReturnType != typeof(DateTime) ||
    source.GetProperty("Uri").PropertyType != typeof(Uri) ||
    source.GetProperty("IsComposite").PropertyType != typeof(bool) ||
    source.GetProperties().Any(p => p.SetMethod != null) ||
    source.GetMethods().Length != 5 || source.GetMethods().Any(m => !m.IsPublic || !m.IsVirtual || !m.IsAbstract))
    throw new Exception("upstream interface methods/properties lost");
var create = factory.GetMethod("Create");
if (create.ReturnType != source || create.GetParameters().Length != 1 ||
    create.GetParameters()[0].ParameterType != typeof(string) || !create.IsAbstract)
    throw new Exception("upstream factory signature lost");
Console.WriteLine("upstream WPF IFontSource interfaces OK");
