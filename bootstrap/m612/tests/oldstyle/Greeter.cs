namespace Hello
{
    internal sealed class Greeter(string who)
    {
        public string Greet() => $"Hello from {who} on Mono";
    }
}
