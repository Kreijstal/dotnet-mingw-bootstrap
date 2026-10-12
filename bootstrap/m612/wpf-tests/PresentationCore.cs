using System;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Media;
class Check {
    static void Main() {
        var assembly=typeof(GlyphTypeface).Assembly;
        Console.WriteLine($"source PresentationCore CLR load: {assembly.GetTypes().Length} types");
        Matrix matrix=new Matrix(2,0,0,3,4,5);
        Point point=matrix.Transform(new Point(7,11));
        if(point.X!=18 || point.Y!=38) throw new Exception("source matrix/point behavior failed");
        var family=new FontFamily("Arial");
        var faces=family.GetTypefaces();
        if(faces.Count==0) throw new Exception("source font family yielded no faces");
        GlyphTypeface glyph;
        if(!new Typeface(family,FontStyles.Normal,FontWeights.Normal,FontStretches.Normal).TryGetGlyphTypeface(out glyph))
            throw new Exception("source font family did not resolve through DirectWrite");
        if(glyph.GlyphCount==0 || !glyph.CharacterToGlyphMap.ContainsKey(65)) throw new Exception("source glyph metadata missing");
        Console.WriteLine("source PresentationCore matrix, real font-family resolution and glyph metadata OK");
    }
}
