using System;
using System.Reflection;
var assembly = typeof(WpfDelegatesMarker).Assembly;
if (WpfDelegatesMarker.Mask() != 18) throw new Exception("upstream native private struct enum changed");
const string prefix = "MS.Internal.Text.TextInterface.";
static Delegate Bind(Assembly assembly, string name, string method) {
    var type = assembly.GetType("MS.Internal.Text.TextInterface." + name, true);
    if (type.IsPublic || type.BaseType != typeof(MulticastDelegate) || !type.IsSealed)
        throw new Exception("upstream delegate identity/visibility lost");
    return Delegate.CreateDelegate(type, typeof(Handlers).GetMethod(method));
}
var source = Bind(assembly, "CreateTextAnalysisSource", "Source");
var sink = Bind(assembly, "CreateTextAnalysisSink", "Sink");
var script = Bind(assembly, "GetScriptAnalysisList", "List");
var number = Bind(assembly, "GetNumberSubstitutionList", "List");
unsafe {
    var invoke = assembly.GetType(prefix + "CreateTextAnalysisSource").GetMethod("Invoke");
    var parameters = invoke.GetParameters();
    if (invoke.ReturnType != typeof(int) || parameters.Length != 9 || parameters[0].ParameterType != typeof(char).MakePointerType() ||
        parameters[1].ParameterType != typeof(uint) || parameters[4].ParameterType != typeof(bool) ||
        parameters[8].ParameterType != typeof(void).MakePointerType().MakePointerType())
        throw new Exception("upstream text-analysis signature lost");
    fixed (char* text = "東京") {
        if (WpfDelegatesMarker.Source(source, text, 2) != text + 1)
            throw new Exception("upstream nine-argument/by-pointer delegate call failed");
    }
    void* value = WpfDelegatesMarker.Sink(sink);
    if (value != (void*)0x5678 || WpfDelegatesMarker.Script(script, value) != value || WpfDelegatesMarker.Number(number, value) != value)
        throw new Exception("upstream pointer delegate invocation failed");
}
Console.WriteLine("upstream WPF text-analysis delegates OK");
public static unsafe class Handlers {
    public static int Source(char* text, uint length, char* culture, void* factory, bool rtl, char* numberCulture, bool ignoreOverride, uint method, void** output) {
        if (text[0] != '東' || text[1] != '京' || length != 2 || culture != null || factory != null || !rtl || numberCulture != null || ignoreOverride || method != 7)
            throw new Exception("text-analysis callback argument mismatch");
        *output = text + 1;
        return 42;
    }
    public static void* Sink() => (void*)0x5678;
    public static void* List(void* value) => value;
}
