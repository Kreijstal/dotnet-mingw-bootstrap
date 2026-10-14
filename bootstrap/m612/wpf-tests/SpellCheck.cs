using System;
using System.Globalization;
using System.Linq;
using System.Collections.Generic;
using System.IO;
using Expression=System.Linq.Expressions.Expression;
using System.Reflection;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Markup;
using System.Windows.Threading;
class Check {
 const BindingFlags I=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
 record Word(int Start,int Length,bool Clean,string[] Suggestions);
 class Scan {public string Text;public List<Word> Words=new();public List<int> Ends=new();public bool Stop;}
 static object Get(object value,string property)=>value.GetType().GetProperty(property,I).GetValue(value);
 static bool Segment(object value,object state){var scan=(Scan)state;var range=Get(value,"TextRange");int start=(int)Get(range,"Start"),length=(int)Get(range,"Length");if(start<0||length<=0||start+length>scan.Text.Length)throw new Exception("invalid original UTF-16 range");var suggestions=((System.Collections.IEnumerable)Get(value,"Suggestions")).Cast<string>().ToArray();scan.Words.Add(new Word(start,length,(bool)Get(value,"IsClean"),suggestions));if(((System.Collections.IEnumerable)Get(value,"SubSegments")).Cast<object>().Any())throw new Exception("unexpected compound segment");return !scan.Stop;}
 static bool Sentence(object value,object state){var scan=(Scan)state;scan.Ends.Add((int)Get(value,"EndOffset"));return !scan.Stop;}
 static Delegate Callback(Type type,string method){var parameters=type.GetMethod("Invoke").GetParameters().Select(x=>Expression.Parameter(x.ParameterType)).ToArray();return Expression.Lambda(type,Expression.Call(typeof(Check).GetMethod(method,BindingFlags.Static|BindingFlags.NonPublic),parameters.Select(x=>Expression.Convert(x,typeof(object)))),parameters).Compile();}
 static Scan Analyze(object provider,string text,bool stop=false){var method=provider.GetType().GetMethod("EnumTextSegments",I);var args=method.GetParameters();var scan=new Scan{Text=text,Stop=stop};int count=(int)method.Invoke(provider,new object[]{text.ToCharArray(),text.Length,Callback(args[2].ParameterType,"Sentence"),Callback(args[3].ParameterType,"Segment"),scan});if(!stop&&count!=scan.Words.Count)throw new Exception("original segment enumeration count");return scan;}
 static void Mode(object provider,int mode){var property=provider.GetType().GetProperty("Mode",I);property.SetValue(provider,Enum.ToObject(property.PropertyType,mode));}
 static void Legacy(){
  var type=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.NLGSpellerInterop",true);
  for(int iteration=0;iteration<3;++iteration){var provider=(IDisposable)Activator.CreateInstance(type,true);string lexicon=null;try{
   type.GetMethod("SetLocale",I).Invoke(provider,new object[]{CultureInfo.GetCultureInfo("en-US")});Mode(provider,7);
   const string text="hello speling world. 😀 Another speling sentence.";var scan=Analyze(provider,text);if(scan.Words.Count!=6||scan.Ends.Count!=2)throw new Exception("legacy sentence/word segmentation");
   var errors=scan.Words.Where(x=>!x.Clean).ToArray();if(errors.Length!=2||errors[0].Start!=text.IndexOf("speling")||errors[1].Start!=text.LastIndexOf("speling")||errors.Any(x=>x.Length!=7||!x.Suggestions.Contains("spelling")))throw new Exception("legacy UTF-16 offsets/errors/suggestions");
   Mode(provider,2);scan=Analyze(provider,text);if(scan.Words.Count(x=>!x.Clean)!=2||scan.Words.Any(x=>x.Suggestions.Length!=0))throw new Exception("legacy verification-only mode");
   Mode(provider,1);scan=Analyze(provider,text);if(scan.Words.Any(x=>!x.Clean)||scan.Words.Count!=6)throw new Exception("legacy wordbreaking-only mode");
   if(Analyze(provider," \r\n 😀").Words.Count!=0||Analyze(provider,"",false).Words.Count!=0)throw new Exception("legacy empty/punctuation-only enumeration");
   scan=Analyze(provider,text,true);if(scan.Words.Count!=1||scan.Ends.Count!=1)throw new Exception("legacy callback cancellation");
   Mode(provider,7);const string custom="zzbootstraplexicon";if(Analyze(provider,custom).Words.Single().Clean)throw new Exception("custom baseline unexpectedly correct");
   lexicon=Path.Combine(Path.GetTempPath(),"wpf-bootstrap-lexicon-"+Guid.NewGuid()+".lex");File.WriteAllText(lexicon,"#LID 1033\r\n"+custom+"\r\n",iteration==0?new System.Text.UTF8Encoding(true):iteration==1?System.Text.Encoding.Unicode:System.Text.Encoding.BigEndianUnicode);
   var load=type.GetMethod("LoadDictionary",I,null,new[]{typeof(string)},null);var dictionary=load.Invoke(provider,new object[]{lexicon});if(!Analyze(provider,custom.ToUpperInvariant()).Words.Single().Clean)throw new Exception("legacy custom dictionary case matching");type.GetMethod("UnloadDictionary",I).Invoke(provider,new[]{dictionary});if(Analyze(provider,custom).Words.Single().Clean)throw new Exception("legacy dictionary removal");
   File.WriteAllText(lexicon,"#LID 1031\r\n"+custom+"\r\n",System.Text.Encoding.Unicode);dictionary=load.Invoke(provider,new object[]{lexicon});if(Analyze(provider,custom).Words.Single().Clean)throw new Exception("legacy dictionary culture escaped");type.GetMethod("UnloadDictionary",I).Invoke(provider,new[]{dictionary});File.WriteAllText(lexicon,"#LID 1033\r\n"+custom+"\r\n",System.Text.Encoding.Unicode);
   dictionary=load.Invoke(provider,new object[]{lexicon});type.GetMethod("ReleaseAllLexicons",I).Invoke(provider,null);if(Analyze(provider,custom).Words.Single().Clean)throw new Exception("legacy dictionary release-all");System.Runtime.InteropServices.Marshal.ReleaseComObject(dictionary);
   type.GetMethod("SetLocale",I).Invoke(provider,new object[]{CultureInfo.GetCultureInfo("ja-JP")});Mode(provider,1);if(Analyze(provider,"日本語の文章です。").Words.Count==0)throw new Exception("legacy Unicode segmentation without spelling provider");
  }finally{provider.Dispose();if(lexicon!=null)File.Delete(lexicon);}GC.Collect(2,GCCollectionMode.Forced,true,true);GC.WaitForPendingFinalizers();}
  Console.WriteLine("original legacy wrapper: UTF-16 sentence/word boundaries, errors/suggestions, verification/word modes, callback stop, UTF-8/UTF-16 dictionaries, release and GC PASS");
 }
 static void Public(bool legacy){
  var text=new TextBox{Text="hello",Language=XmlLanguage.GetLanguage("en-US"),FontSize=24};SpellCheck.SetIsEnabled(text,true);var window=new Window{Content=text,Width=400,Height=120,ShowInTaskbar=false};string lexicon=null;
  try{window.Show();text.Focus();Pump(window);
   if(legacy){var editor=typeof(System.Windows.Controls.Primitives.TextBoxBase).GetProperty("TextEditor",I).GetValue(text);var speller=Get(editor,"Speller");speller.GetType().GetMethod("EnsureInitialized",I).Invoke(speller,null);var field=speller.GetType().GetField("_spellerInterop",I);var old=(IDisposable)field.GetValue(speller);field.SetValue(speller,Activator.CreateInstance(typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.NLGSpellerInterop",true),true));old.Dispose();}
   text.Text="hello speling world";text.CaretIndex=text.Text.Length;SpellingError error=null;for(int i=0;i<30&&error==null;++i){Pump(window);Thread.Sleep(20);error=text.GetSpellingError(7);}
   if(error==null)throw new Exception("original public spelling error missing");var suggestions=error.Suggestions.ToArray();if(!suggestions.Any(x=>x.Equals("spelling",StringComparison.OrdinalIgnoreCase)))throw new Exception("correct spelling suggestion missing");
   error.Correct("spelling");Pump(window);if(text.Text!="hello spelling world")throw new Exception("spelling correction source identity");
   text.Text="speling speling";text.CaretIndex=text.Text.Length;Pump(window);error=text.GetSpellingError(1)??throw new Exception("public repeated-word error");error.IgnoreAll();Pump(window);if(text.GetSpellingError(1)!=null||text.GetSpellingError(9)!=null)throw new Exception("public IgnoreAll");
   const string custom="zzbootstraplexicon";
   foreach(var encoding in new System.Text.Encoding[]{new System.Text.UTF8Encoding(true),System.Text.Encoding.Unicode,System.Text.Encoding.BigEndianUnicode})foreach(bool header in new[]{false,true}){
    text.Text=custom+" hello";text.CaretIndex=text.Text.Length;Pump(window);if(text.GetSpellingError(1)==null)throw new Exception("public custom baseline");
    lexicon=Path.Combine(Path.GetTempPath(),"wpf-bootstrap-public-"+Guid.NewGuid()+".lex");File.WriteAllText(lexicon,(header?"#LID 1033\r\n":"")+custom+"\r\n",encoding);var original=File.ReadAllBytes(lexicon);var uri=new Uri(lexicon);
    SpellCheck.GetCustomDictionaries(text).Add(uri);ExpectError(text,window,false,"public dictionary registration "+encoding.WebName+" header="+header);if(!File.ReadAllBytes(lexicon).SequenceEqual(original))throw new Exception("public dictionary source modified");
    SpellCheck.GetCustomDictionaries(text).Remove(uri);ExpectError(text,window,true,"public dictionary removal");File.Delete(lexicon);lexicon=null;
   }
   lexicon=Path.Combine(Path.GetTempPath(),"wpf-bootstrap-scope-"+Guid.NewGuid()+".lex");File.WriteAllText(lexicon,"#LID 1031\r\n"+custom+"\r\n",System.Text.Encoding.Unicode);var foreign=new Uri(lexicon);SpellCheck.GetCustomDictionaries(text).Add(foreign);Pump(window);if(text.GetSpellingError(1)==null)throw new Exception("public dictionary culture escaped");SpellCheck.GetCustomDictionaries(text).Clear();Pump(window);if(text.GetSpellingError(1)==null)throw new Exception("public dictionary clear");File.Delete(lexicon);lexicon=null;
   Console.WriteLine("original public "+(legacy?"legacy":"WinRT")+" spelling detection/suggestions/correction, IgnoreAll and custom dictionaries PASS");
  }finally{SpellCheck.SetIsEnabled(text,false);window.Close();if(lexicon!=null)File.Delete(lexicon);}GC.Collect(2,GCCollectionMode.Forced,true,true);GC.WaitForPendingFinalizers();
 }
 static void Pump(FrameworkElement element){element.UpdateLayout();var frame=new DispatcherFrame();Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle,new Action(()=>frame.Continue=false));Dispatcher.PushFrame(frame);element.UpdateLayout();}
 static void ExpectError(TextBox text,Window window,bool expected,string reason){for(int i=0;i<100;++i){Pump(window);if((text.GetSpellingError(1)!=null)==expected)return;Thread.Sleep(20);}throw new Exception(reason);}

 [STAThread]static void Main(){
  _=Dispatcher.CurrentDispatcher;
  CultureInfo.CurrentCulture=CultureInfo.GetCultureInfo("en-US");CultureInfo.CurrentUICulture=CultureInfo.CurrentCulture;
  var providerType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.SpellerInteropBase",true);
  var factory=providerType.GetMethod("CreateInstance",BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static)??throw new Exception("SpellerInteropBase factory missing: "+string.Join(",",providerType.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static).Select(x=>x.ToString())));object provider=null;
  try{provider=factory.Invoke(null,null);Console.WriteLine("original spell provider: "+provider?.GetType().FullName);}catch(TargetInvocationException e){System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(e.InnerException).Throw();throw;}
  (provider as IDisposable)?.Dispose();
  Legacy();
  Public(false);Public(true);Dispatcher.CurrentDispatcher.InvokeShutdown();
 }
}
