namespace Bootstrap;
public static class Greeting
{
    public static string Text => $"hello from a source-built package ({typeof(Greeting).Assembly.GetName().Version})";
}
