using System.Linq;
namespace LibA;
public static class A { public static int Value(int x) => Enumerable.Range(1, x).Sum(); }
