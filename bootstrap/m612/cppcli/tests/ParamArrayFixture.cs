using System;
public class ParamArrayFixture {
    public int InitialCount { get; }
    private static object[] previous;
    public ParamArrayFixture(params int[] values) { InitialCount = values.Length; }
    public static int Objects(params object[] values) => values == null ? -1 : values.Length;
    public static int Numbers(int baseline, params int[] values) {
        foreach (var value in values) baseline += value;
        return baseline;
    }
    public int Instance(int offset, params int[] values) => offset + values.Length;
    public static string Text(string prefix, params string[] values) => prefix + string.Join("|", values);
    public static bool Fresh(params object[] values) {
        if (values == null) throw new Exception("omitted parameter array became null");
        bool result = !ReferenceEquals(previous, values);
        previous = values;
        return result;
    }
    public static bool IsPrevious(object[] value) => ReferenceEquals(previous, value);
    public static int Overload(string value) => 17;
    public static int Overload(string value, params object[] values) => 29;
    public static int Better(object value) => 17;
    public static int Better(string value, params int[] values) => 29;
    public static int Required(int[] values) => values.Length;
    public virtual int Virtual(params int[] values) => values.Length;
}
