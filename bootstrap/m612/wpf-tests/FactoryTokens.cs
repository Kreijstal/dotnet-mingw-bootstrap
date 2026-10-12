using System;
using System.Reflection;
using System.Runtime.ConstrainedExecution;
var getter = typeof(FactoryProbe).GetProperty("IsInvalid").GetMethod;
var contract = getter.GetCustomAttribute<ReliabilityContractAttribute>();
using var factory = new FactoryProbe();
if (!factory.IsInvalid || !getter.IsVirtual || getter.GetBaseDefinition().DeclaringType != typeof(System.Runtime.InteropServices.CriticalHandle) ||
    contract.ConsistencyGuarantee != Consistency.WillNotCorruptState || contract.Cer != Cer.Success)
    throw new Exception("upstream Factory getter dispatch/reliability metadata changed");
Console.WriteLine("upstream WPF Factory IsInvalid/reliability accessor OK");
