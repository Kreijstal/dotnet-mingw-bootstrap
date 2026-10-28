using System;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security;
using Microsoft.Management.Infrastructure;
using Microsoft.Management.Infrastructure.Options;

internal static class MmiCredentialChecks
{
    const BindingFlags Internal=BindingFlags.Instance|BindingFlags.NonPublic;
    static void Require(bool value,string message){if(!value)throw new Exception(message);}
    static void Throws<T>(Action action) where T:Exception
    {try{action();}catch(T){return;}throw new Exception("Expected "+typeof(T).Name);}
    static object Call(object target,string name,object[] args)
    {
        try{return target.GetType().GetMethod(name,Internal).Invoke(target,args);}
        catch(TargetInvocationException e){throw e.InnerException;}
    }
    static object Handle(CimSessionOptions options)=>typeof(CimSessionOptions).GetProperty("DestinationOptionsHandleOnDemand",Internal).GetValue(options);
    static object Field(object target,string name)=>target.GetType().GetField(name,Internal).GetValue(target);
    static string Text(object target,string name)=>Marshal.PtrToStringUni((IntPtr)Field(target,name));
    static void Ok(object result)=>Require(Convert.ToUInt32(result)==0,"Native MI credential operation failed: "+result);
    static uint Count(object handle)
    {
        object[] args={null};Ok(Call(handle,"GetCredentialsCount",args));return (uint)args[0];
    }
    static void Inspect(CimSessionOptions options,string option,string authentication,string domain,string user,string password,string thumbprint)
    {
        object handle=Handle(options);Require(Count(handle)==1,"Native credential count");
        object[] args={(uint)0,null,null,null};Ok(Call(handle,"GetCredentialsAt",args));
        Require((string)args[1]==option,"Native credential destination/proxy slot");
        var credential=args[2];Require(Text(credential,"authenticationType")==authentication,"Native authentication mechanism");
        Require(Marshal.SizeOf(credential)==4*IntPtr.Size,"MI_UserCredentials ABI size");
        Require(Marshal.OffsetOf(credential.GetType(),"credentials").ToInt32()==IntPtr.Size,"MI credential union ABI offset");
        var union=Field(credential,"credentials");
        if(thumbprint!=null)Require(Text(union,"certificateThumbprint")==thumbprint,"Native certificate thumbprint");
        else
        {
            var pair=Field(union,"usernamePassword");
            Require((Text(pair,"domain")??"")==(domain??""),"Native credential domain");
            Require((Text(pair,"username")??"")==(user??""),"Native credential user");
            if(password!=null)
            {
                object[] sizing={(uint)0,null,IntPtr.Zero,(uint)0,null,null};
                var sizingResult=Call(handle,"GetCredentialsPasswordAt",sizing);
                uint chars=(uint)sizing[4];Require(chars>0,"Native password buffer size");
                IntPtr buffer=Marshal.AllocHGlobal(checked((int)chars*2));
                try
                {
                    for(int i=0;i<checked((int)chars);i++)Marshal.WriteInt16(buffer,i*2,0);
                    object[] reading={(uint)0,null,buffer,chars,null,null};Ok(Call(handle,"GetCredentialsPasswordAt",reading));
                    Require((string)reading[1]==option,"Native password slot");
                    Require(Marshal.PtrToStringUni(buffer)==password,"Native password round trip");
                }
                finally
                {
                    for(int i=0;i<checked((int)chars);i++)Marshal.WriteInt16(buffer,i*2,0);
                    Marshal.FreeHGlobal(buffer);
                }
            }
        }
    }
    internal static void Run()
    {
        const string destination="__MI_DESTINATIONOPTIONS_DESTINATION_CREDENTIALS",proxy="__MI_DESTINATIONOPTIONS_PROXY_CREDENTIALS";
        using(var options=new WSManSessionOptions())
        {
            Throws<ArgumentNullException>(()=>options.AddDestinationCredentials(null));
            Throws<ArgumentNullException>(()=>options.AddProxyCredentials(null));
            Require(Count(Handle(options))==0,"Null credential altered native options");
        }
        foreach(var item in new[]{(PasswordAuthenticationMechanism.Default,"Default"),(PasswordAuthenticationMechanism.Basic,"Basic"),
            (PasswordAuthenticationMechanism.Digest,"Digest"),(PasswordAuthenticationMechanism.Negotiate,"NegoWithCreds"),
            (PasswordAuthenticationMechanism.Kerberos,"Kerberos"),(PasswordAuthenticationMechanism.NtlmDomain,"Ntlmdomain"),(PasswordAuthenticationMechanism.CredSsp,"CredSSP")})
        {
            using var password=new SecureString();foreach(char c in "fixture-π-密碼")password.AppendChar(c);
            var credential=new CimCredential(item.Item1,"fixture-domain-é","fixture-user-名",password);
            password.Clear();password.Dispose();
            using var options=new WSManSessionOptions();options.AddDestinationCredentials(credential);
            GC.Collect(2,GCCollectionMode.Forced,true,true);GC.WaitForPendingFinalizers();
            Inspect(options,destination,item.Item2,"fixture-domain-é","fixture-user-名","fixture-π-密碼",null);
            using var clone=new WSManSessionOptions(options);options.Dispose();
            Inspect(clone,destination,item.Item2,"fixture-domain-é","fixture-user-名","fixture-π-密碼",null);
            using var proxyOptions=new WSManSessionOptions();proxyOptions.AddProxyCredentials(credential);
            Inspect(proxyOptions,proxy,item.Item2,"fixture-domain-é","fixture-user-名","fixture-π-密碼",null);
            Throws<ObjectDisposedException>(()=>options.AddDestinationCredentials(credential));
            Throws<ObjectDisposedException>(()=>options.AddProxyCredentials(credential));
        }
        foreach(var item in new[]{(CertificateAuthenticationMechanism.Default,"ClientCerts"),(CertificateAuthenticationMechanism.ClientCertificate,"ClientCerts"),(CertificateAuthenticationMechanism.IssuerCertificate,"IssuerCert")})
        {
            using var options=new WSManSessionOptions();
            options.AddDestinationCredentials(new CimCredential(item.Item1,"0123456789ABCDEF0123456789ABCDEF01234567"));
            Inspect(options,destination,item.Item2,null,null,null,"0123456789ABCDEF0123456789ABCDEF01234567");
            using var proxyOptions=new WSManSessionOptions();proxyOptions.AddProxyCredentials(new CimCredential(item.Item1,"0123456789ABCDEF0123456789ABCDEF01234567"));
            Inspect(proxyOptions,proxy,item.Item2,null,null,null,"0123456789ABCDEF0123456789ABCDEF01234567");
        }
        foreach(var item in new[]{(ImpersonatedAuthenticationMechanism.None,"None"),(ImpersonatedAuthenticationMechanism.Negotiate,"NegoNoCreds"),(ImpersonatedAuthenticationMechanism.Kerberos,"Kerberos"),(ImpersonatedAuthenticationMechanism.NtlmDomain,"Ntlmdomain")})
        {
            using var options=new DComSessionOptions();options.AddDestinationCredentials(new CimCredential(item.Item1));
            Inspect(options,destination,item.Item2,null,null,null,null);
        }
        using(var options=new WSManSessionOptions())
        {
            options.AddDestinationCredentials(new CimCredential("Basic",null,"string-constructor",null));
            Inspect(options,destination,"Basic",null,"string-constructor",null,null);
        }
        using(var options=new WSManSessionOptions())
        {
            options.AddDestinationCredentials(new CimCredential("ClientCerts","00112233445566778899AABBCCDDEEFF00112233"));
            Inspect(options,destination,"ClientCerts",null,null,null,"00112233445566778899AABBCCDDEEFF00112233");
        }
        using(var options=new DComSessionOptions())
        {
            options.AddDestinationCredentials(new CimCredential("Default"));
            using var session=CimSession.Create(null,options);
            // The independent C/mi.h probe observes the same native rejection:
            // explicit Default credentials with neither user nor password do
            // not request the transport's ordinary implicit current identity.
            try
            {
                foreach(var instance in session.QueryInstances("root/cimv2","WQL","SELECT BuildNumber FROM Win32_OperatingSystem"))instance.Dispose();
                throw new Exception("Empty explicit Default credentials silently ignored");
            }
            catch(CimException e){Require(e.NativeErrorCode==NativeErrorCode.Failed,"Native empty-credential error: "+e.NativeErrorCode);}
        }
        using(var options=new DComSessionOptions())
        {
            options.AddDestinationCredentials(new CimCredential(ImpersonatedAuthenticationMechanism.Negotiate));
            try{using var session=CimSession.Create(null,options);throw new Exception("Native DCOM unsupported authentication silently accepted");}
            catch(CimException e){Require(e.NativeErrorCode==NativeErrorCode.NotSupported,"Native DCOM authentication error");}
        }
        Throws<ArgumentOutOfRangeException>(()=>new CimCredential((CertificateAuthenticationMechanism)999,"dummy"));
        Throws<ArgumentOutOfRangeException>(()=>new CimCredential((ImpersonatedAuthenticationMechanism)999));
        Throws<ArgumentOutOfRangeException>(()=>new CimCredential((PasswordAuthenticationMechanism)999,null,"dummy",null));
        Console.WriteLine("PASS native credential union ABI, all destination/proxy authentication forms, Unicode user/domain/password, independent SecureString copy, native clone after source disposal, validation and native DCOM transport error propagation");
    }
}
