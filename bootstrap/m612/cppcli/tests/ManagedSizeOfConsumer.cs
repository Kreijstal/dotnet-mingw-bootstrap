using System;
using System.Runtime.CompilerServices;
if(ManagedSizeOf.Point()!=(ulong)Unsafe.SizeOf<SizePoint>() || ManagedSizeOf.Wide()!=(ulong)Unsafe.SizeOf<SizeWide>() ||
   ManagedSizeOf.References()!=(ulong)Unsafe.SizeOf<SizeReferences>() || ManagedSizeOf.Unevaluated()!=32 || ManagedSizeOf.Calls!=0 ||
   ManagedSizeOf.Bytes(5)!=160)
    throw new Exception("managed sizeof used facade layout, evaluated its operand or lost size_t width");
Console.WriteLine("managed sizeof: actual sequential/explicit/reference-containing CLR layouts, unevaluated operands and size_t arithmetic PASS");
