using MS.Internal.Text.TextInterface;
public static class DirectWriteFriendProbe {
    public static uint Count() {
        using var factory=Factory.Create(FactoryType.Shared,null,null);
        return factory.GetSystemFontCollection().FamilyCount;
    }
}
