using System;
var text=new string("λ🚀".ToCharArray());
var value=new PropertyData(); ValuePropertyLocations.Ref(ref value,text);
var array=new PropertyData[2]; ValuePropertyLocations.Array(array,text);
var owner=new PropertyOwner(); ValuePropertyLocations.Field(owner,text);
var copy=ValuePropertyLocations.Copy(value);
GC.Collect(2,GCCollectionMode.Forced,true,true);
if(value.Value!=7 || !ReferenceEquals(value.Text,text) || array[1].Value!=8 || array[0].Value!=0 ||
   !ReferenceEquals(array[1].Text,text) || owner.Data.Value!=9 || !ReferenceEquals(owner.Data.Text,text) ||
   copy.Value!=10 || value.Value!=7 || ValuePropertyLocations.Indices!=1)
    throw new Exception("value property receiver must retain ref/array/field location and preserve by-value copies across GC");
Console.WriteLine("value property locations: ref/array/nested field writeback, managed reference payload, index once, by-value isolation and moving GC PASS");
