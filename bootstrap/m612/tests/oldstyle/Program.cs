using System;
using System.Linq;
using System.Xml.Linq;

namespace Hello
{
    internal static class Program
    {
        private static int Main(string[] args)
        {
            var g = new Greeter("MSBuild");                   // primary constructor (C# 12)
            int[] xs = [3, 1, 2];                              // collection expression (C# 12)
            var doc = new XElement("sorted", xs.OrderBy(x => x).Select(x => new XElement("i", x)));
            Console.WriteLine(g.Greet() + " " + doc.ToString(SaveOptions.DisableFormatting));
            Console.WriteLine(Properties.Strings.Footer);
            return 0;
        }
    }
}

namespace Hello.Properties
{
    internal static class Strings
    {
        private static readonly System.Resources.ResourceManager s_rm =
            new System.Resources.ResourceManager("Hello.Strings", typeof(Strings).Assembly);
        internal static string Footer => s_rm.GetString("Footer");
    }
}
