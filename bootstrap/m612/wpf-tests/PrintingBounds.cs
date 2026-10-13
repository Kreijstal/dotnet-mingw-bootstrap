using System;
using System.Linq;
using System.Windows;
using System.Windows.Media;
var matrices=new[]{Matrix.Identity,new Matrix(2,0,0,3,5,7),new Matrix(-1,0,0,1,0,0),new Matrix(1,2,3,4,5,6)};
foreach(var matrix in matrices) {
    var corners=new[]{new Point(2,3),new Point(8,9),new Point(2,9),new Point(8,3)};
    var transformed=corners.Select(point=>matrix.Transform(point)).ToArray();
    var expected=new Rect(new Point(transformed.Min(point=>point.X),transformed.Min(point=>point.Y)),
                          new Point(transformed.Max(point=>point.X),transformed.Max(point=>point.Y)));
    var actual=new Rect(); PrintingBounds.Apply(matrix,2,3,8,9,ref actual);
    GC.Collect(2,GCCollectionMode.Forced,true,true);
    if(actual!=expected) throw new Exception($"original TransformBounds matrix={matrix} actual={actual} expected={expected}");
}
Console.WriteLine("original printing TransformBounds: array Point setters, Matrix.Transform, ref Rect setters, identity/scale/reflection/shear and GC PASS");
