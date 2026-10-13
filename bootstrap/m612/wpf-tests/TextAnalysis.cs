using System;
using System.Collections;
using System.Globalization;
using System.Reflection;
using System.Windows.Media;

unsafe class Check {
    static readonly BindingFlags All=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance;
    static object Call(object target,string name,params object[] arguments)=>target.GetType().GetMethod(name,All).Invoke(target,arguments);
    static object Property(object target,string name)=>target.GetType().GetProperty(name,All).GetValue(target);
    static void Main() {
        var bridge=Assembly.Load("DirectWriteForwarder");
        var core=typeof(Typeface).Assembly;
        const string prefix="MS.Internal.Text.TextInterface.";
        var factoryType=bridge.GetType(prefix+"Factory",true);
        var create=factoryType.GetMethod("Create",All);
        var factory=create.Invoke(null,new[]{Enum.ToObject(bridge.GetType(prefix+"FactoryType",true),0),null,null});
        try {
            var analyzer=Call(factory,"CreateTextAnalyzer");
            var itemize=analyzer.GetType().GetMethod("Itemize",All);
            var parameters=itemize.GetParameters();
            var native=core.GetType("MS.Internal.TextFormatting.UnsafeNativeMethods",true);
            var classification=core.GetType("MS.Internal.ClassificationUtility",true).GetProperty("Instance",All).GetValue(null);
            var arguments=new object[parameters.Length];
            string[] callbacks={"CreateTextAnalysisSink","GetScriptAnalysisList","GetNumberSubstitutionList","CreateTextAnalysisSource"};
            for(int i=0;i<callbacks.Length;++i)
                arguments[9+i]=Delegate.CreateDelegate(parameters[9+i].ParameterType,native.GetMethod(callbacks[i],All));
            string text="Latin العربية 123 אב कि 😀";
            object spans;
            fixed(char* buffer=text) {
                arguments[0]=Pointer.Box(buffer,parameters[0].ParameterType);
                arguments[1]=(uint)text.Length;
                arguments[2]=CultureInfo.GetCultureInfo("en-US");
                arguments[3]=factory; arguments[4]=true;
                arguments[5]=CultureInfo.GetCultureInfo("ar-SA");
                arguments[6]=true; arguments[7]=3u; arguments[8]=classification;
                GC.Collect(2,GCCollectionMode.Forced,true,true);
                try { spans=itemize.Invoke(analyzer,arguments); }
                catch(TargetInvocationException e) { throw e.InnerException; }
            }
            int length=0,count=0; bool latin=false,indic=false,combining=false,number=false;
            GC.Collect(2,GCCollectionMode.Forced,true,true);
            foreach(var span in (IEnumerable)spans) {
                int size=(int)span.GetType().GetField("length",All).GetValue(span);
                if(size<=0) throw new Exception("nonpositive itemized span");
                var props=span.GetType().GetField("element",All).GetValue(span);
                latin|=(bool)Property(props,"IsLatin");
                indic|=(bool)Property(props,"IsIndic");
                combining|=(bool)Property(props,"HasCombiningMark");
                var pointer=Property(props,"NumberSubstitutionNoAddRef");
                number|=Pointer.Unbox(pointer)!=null;
                if(!(bool)Call(props,"CanShapeTogether",props)) throw new Exception("retained item properties failed after native sink disposal and GC");
                length+=size; ++count;
            }
            if(length!=text.Length || count<4 || !latin || !indic || !combining || !number)
                throw new Exception($"original itemizer result failed: length={length}, spans={count}, latin={latin}, indic={indic}, combining={combining}, number={number}");
            Console.WriteLine($"original complete C++/CLI TextAnalyzer.Itemize: {count} spans, Latin/Indic/combining/number properties and retained native script/substitution ownership after GC PASS");
        } finally { ((IDisposable)factory).Dispose(); }
    }
}
