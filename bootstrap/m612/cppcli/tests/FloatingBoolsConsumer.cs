using System;
foreach(var value in new[]{0.0,-0.0,1.0,-1.0,double.Epsilon,double.PositiveInfinity,double.NegativeInfinity,double.NaN}) {
    bool expected=value!=0;
    if(FloatingBools.Double(value)!=expected || FloatingBools.Not(value)==expected || FloatingBools.Condition(value)!=(expected?1:0))
        throw new Exception($"double boolean conversion mismatch {value:R}");
}
foreach(var value in new[]{0f,-0f,1f,-1f,float.Epsilon,float.PositiveInfinity,float.NegativeInfinity,float.NaN})
    if(FloatingBools.Float(value)!=(value!=0)) throw new Exception($"float boolean conversion mismatch {value:R}");
if(FloatingBools.Calls!=8) throw new Exception("condition operand evaluation");
Console.WriteLine("floating bools: float32/64 zero/signed-zero/subnormal/finite/infinity/NaN, logical not and condition evaluation once PASS");
