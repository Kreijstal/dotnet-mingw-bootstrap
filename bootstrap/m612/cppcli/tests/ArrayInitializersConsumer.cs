using System;
using System.Linq;
using System.Threading.Tasks;
var arrays=new int[16][];
Parallel.For(0,arrays.Length,index=>arrays[index]=InitializerArrays.Numbers());
if(arrays.Any(array=>!ReferenceEquals(array,arrays[0]))||InitializerArrays.Sequence()!=123||!arrays[0].SequenceEqual(new[]{11,22,33}))
    throw new Exception("initializer element order/once/concurrent identity failed");
GC.Collect(2,GCCollectionMode.Forced,true,true);
if(!InitializerArrays.Words().SequenceEqual(new[]{"RAW","東京 😀"})||
    !InitializerArrays.Types().SequenceEqual(new[]{typeof(int),typeof(InitializerNode)})||
    InitializerArrays.Nodes().Length!=3||InitializerArrays.Nodes()[0].Value!=7||InitializerArrays.Nodes()[1]!=null||InitializerArrays.Nodes()[2].Value!=9||InitializerArrays.Empty().Length!=0||InitializerArrays.Converted()[0]!=44)
    throw new Exception("typed literal/type-token/object/empty array initializer lost data after GC");
arrays[0][1]=44;
if(InitializerArrays.Numbers()[1]!=44) throw new Exception("initializer array copied on read");
Console.WriteLine("managed static array initializers: typed scalar/string/type-token/object/null/empty elements, order/once/concurrency, shared mutation and GC PASS");
