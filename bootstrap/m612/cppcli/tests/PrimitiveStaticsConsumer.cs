using System;
if (BitConverter.DoubleToInt64Bits(PrimitiveStatics.Minimum()) != BitConverter.DoubleToInt64Bits(double.MinValue) ||
    BitConverter.DoubleToInt64Bits(PrimitiveStatics.Maximum()) != BitConverter.DoubleToInt64Bits(double.MaxValue) ||
    BitConverter.DoubleToInt64Bits(PrimitiveStatics.Epsilon()) != BitConverter.DoubleToInt64Bits(double.Epsilon) ||
    BitConverter.DoubleToInt64Bits(PrimitiveStatics.Nan()) != BitConverter.DoubleToInt64Bits(double.NaN) ||
    !double.IsPositiveInfinity(PrimitiveStatics.PositiveInfinity()) || !double.IsNegativeInfinity(PrimitiveStatics.NegativeInfinity()) ||
    BitConverter.SingleToInt32Bits(PrimitiveStatics.FloatMinimum()) != BitConverter.SingleToInt32Bits(float.MinValue) ||
    BitConverter.SingleToInt32Bits(PrimitiveStatics.FloatEpsilon()) != BitConverter.SingleToInt32Bits(float.Epsilon) ||
    BitConverter.SingleToInt32Bits(PrimitiveStatics.FloatNan()) != BitConverter.SingleToInt32Bits(float.NaN))
    throw new Exception("primitive floating literals lost metadata bits");
if (PrimitiveStatics.IntMinimum() != int.MinValue || PrimitiveStatics.IntMaximum() != int.MaxValue ||
    PrimitiveStatics.UIntMaximum() != uint.MaxValue || PrimitiveStatics.LongMinimum() != long.MinValue ||
    PrimitiveStatics.ULongMaximum() != ulong.MaxValue || PrimitiveStatics.SmallMinimum() != sbyte.MinValue + short.MinValue ||
    PrimitiveStatics.SmallMaximum() != byte.MaxValue + ushort.MaxValue || PrimitiveStatics.NativeLookup() != 17 || PrimitiveStatics.NativeClassLookup() != 18 || PrimitiveStatics.CharacterMaximum() != char.MaxValue)
    throw new Exception("primitive integer limits or native shadow lookup lost");
if (PrimitiveStatics.KeywordLong() != int.MaxValue || PrimitiveStatics.KeywordInt() != int.MaxValue ||
    PrimitiveStatics.KeywordUnsignedLong() != uint.MaxValue || PrimitiveStatics.KeywordWide() != long.MaxValue ||
    PrimitiveStatics.KeywordUnsignedWide() != ulong.MaxValue || PrimitiveStatics.KeywordFloat() != float.MinValue ||
    PrimitiveStatics.KeywordDouble() != double.MinValue)
    throw new Exception("keyword static scopes lost Windows C++ widths");
double parsed = 7;
if (!PrimitiveStatics.Parse("1.25e2", ref parsed) || parsed != 125 || PrimitiveStatics.Parse("invalid", ref parsed) || parsed != 0 ||
    !PrimitiveStatics.Finite(125) || PrimitiveStatics.Finite(double.PositiveInfinity))
    throw new Exception("primitive static methods or tracking-reference parsing failed");
int integer = 0;
if (!PrimitiveStatics.ParseInteger("2147483647", ref integer) || integer != int.MaxValue)
    throw new Exception("Int32 static parsing failed");
Console.WriteLine("CLR primitive constants, static methods and native shadowing OK");
