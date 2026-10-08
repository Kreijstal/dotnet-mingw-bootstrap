// Mono 6.12's mscorlib predates records/init; the usual polyfill for
// .NET Framework targets.
namespace System.Runtime.CompilerServices { internal static class IsExternalInit { } }
