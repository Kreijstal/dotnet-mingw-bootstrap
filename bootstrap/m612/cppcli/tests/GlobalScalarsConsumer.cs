using System;
if(BitConverter.DoubleToInt64Bits(GlobalScalars.Threshold())!=BitConverter.DoubleToInt64Bits(0.015625/100) ||
   BitConverter.DoubleToInt64Bits(GlobalScalars.Zero())!=long.MinValue || GlobalScalars.Subnormal()!=double.Epsilon ||
   GlobalScalars.Unsigned()!=ulong.MaxValue || GlobalScalars.Narrow()!=65535 || !GlobalScalars.Boolean() ||
   GlobalScalars.Negative()!=-7 || GlobalScalars.Float()!=0.125f || !GlobalScalars.Close(0) || GlobalScalars.Close(GlobalScalars.Threshold()))
    throw new Exception($"constant global scalar mismatch: threshold={GlobalScalars.Threshold():R} zeroBits={BitConverter.DoubleToInt64Bits(GlobalScalars.Zero()):X16} tiny={GlobalScalars.Subnormal():R} wide={GlobalScalars.Unsigned()} narrow={GlobalScalars.Narrow()} bool={GlobalScalars.Boolean()} signed={GlobalScalars.Negative()} float={GlobalScalars.Float():R}");
Console.WriteLine("global scalar constants: original pixel threshold, negative zero/subnormal, unsigned widths, bool/signed/float and boundary comparisons PASS");
