using System;
using System.Reflection;
object integer = TemplateFactory.Integer(), wide = TemplateFactory.Wide(), derived = TemplateFactory.Derived();
if (integer == null || wide == null || derived == null || integer.GetType() == wide.GetType() ||
    integer.GetType().IsGenericType || wide.GetType().IsGenericType || derived.GetType().IsGenericType)
    throw new Exception("managed template specialization identity failed");
var property = integer.GetType().GetProperty("Value");
if (property.PropertyType != typeof(int) || (int)property.GetValue(integer) != 29 ||
    wide.GetType().GetProperty("Value").PropertyType != typeof(long) ||
    (long)wide.GetType().GetProperty("Value").GetValue(wide) != 5000000003L ||
    (int)derived.GetType().GetMethod("Read").Invoke(derived, null) != 42)
    throw new Exception("specialized property/virtual inherited member failed");
property.SetValue(integer, 61);
if ((int)property.GetValue(integer) != 61 || integer.GetType().GetMethod("Unused") != null)
    throw new Exception("specialized setter or unused member emission failed");
Console.WriteLine("Closed C++ managed templates, out-of-line properties and virtual inheritance OK");
