using System;
using System.Data;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

internal static class NewtonsoftInstalled
{
    static void Require(bool value,string message){if(!value)throw new Exception(message);}
    [ModuleInitializer]
    internal static void Check()
    {
        var assembly=typeof(JsonConvert).Assembly;
        Require(assembly.GetCustomAttribute<AssemblyFileVersionAttribute>().Version=="13.0.4.0","source file version");
        Require(Path.GetDirectoryName(assembly.Location)==AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar),"consumer-local Newtonsoft implementation");
        int types=assembly.GetExportedTypes().Length;Require(types>100,"exported types load");
        var date=new DateOnly(2026,10,26);
        Require(JsonConvert.DeserializeObject<DateOnly>(JsonConvert.SerializeObject(date))==date,".NET 8 DateOnly feature selection");
        var big=BigInteger.Parse("123456789012345678901234567890");
        Require(JsonConvert.DeserializeObject<BigInteger>(JsonConvert.SerializeObject(big))==big,"BigInteger roundtrip");
        var table=new DataTable("fixture");table.Columns.Add("id",typeof(int));table.Columns.Add("name",typeof(string));table.Rows.Add(42,"table λ");
        var copy=JsonConvert.DeserializeObject<DataTable>(JsonConvert.SerializeObject(table));
        Require(copy.Rows.Count==1&&Convert.ToInt32(copy.Rows[0]["id"])==42&&(string)copy.Rows[0]["name"]=="table λ","ADO.NET converter feature selection");
        using(var reader=new JsonTextReader(new StringReader("1.25")){FloatParseHandling=FloatParseHandling.Decimal})
        {Require(reader.Read()&&reader.Value is decimal value&&value==1.25m,"decimal parsing");}
        bool rejected=false;
        try {JsonConvert.DeserializeObject("{\"a\":{\"b\":{\"c\":1}}}",new JsonSerializerSettings{MaxDepth=2});}
        catch(JsonReaderException){rejected=true;}
        Require(rejected,"depth limit");
        using(var reader=new JsonTextReader(new StringReader("{\"async\":\"λ\"}")))
        {var token=JToken.ReadFromAsync(reader).GetAwaiter().GetResult();Require((string)token["async"]=="λ","async JSON reader");}
        Console.WriteLine("PASS Newtonsoft installed source version/location, "+types+" exported types, DateOnly/BigInteger/ADO.NET/decimal/depth/async features");
    }
}
