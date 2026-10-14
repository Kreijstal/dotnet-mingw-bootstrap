using System;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

class Check {
    const int Width=33,Height=25,Stride=Width*3;
    static void CheckDrawingInterop(byte[] pixels) {
        // Exercise the LockBits/Scan0/Create/CopyPixels API sequence in Greenshot's
        // ImageHelper with a width that requires GDI+ row padding.
        using var bitmap=new System.Drawing.Bitmap(Width,Height,System.Drawing.Imaging.PixelFormat.Format24bppRgb);
        bitmap.SetResolution(120,144);
        var bounds=new System.Drawing.Rectangle(0,0,Width,Height);
        var data=bitmap.LockBits(bounds,System.Drawing.Imaging.ImageLockMode.WriteOnly,bitmap.PixelFormat);
        try {
            if(data.Stride<=Stride)throw new Exception("GDI+ fixture must have padded rows");
            for(int y=0;y<Height;++y)System.Runtime.InteropServices.Marshal.Copy(pixels,y*Stride,IntPtr.Add(data.Scan0,y*data.Stride),Stride);
        } finally {bitmap.UnlockBits(data);}
        BitmapSource source;
        data=bitmap.LockBits(bounds,System.Drawing.Imaging.ImageLockMode.ReadOnly,bitmap.PixelFormat);
        try {
            source=BitmapSource.Create(Width,Height,bitmap.HorizontalResolution,bitmap.VerticalResolution,
                PixelFormats.Bgr24,null,data.Scan0,data.Stride*Height,data.Stride);
            source.Freeze();
        } finally {bitmap.UnlockBits(data);}
        // The frozen WPF bitmap must own its data after the original is modified.
        using(var graphics=System.Drawing.Graphics.FromImage(bitmap))graphics.Clear(System.Drawing.Color.Magenta);
        GC.Collect(2,GCCollectionMode.Forced,true,true);
        if(!source.IsFrozen||Math.Abs(source.DpiX-120)>0.01||Math.Abs(source.DpiY-144)>0.01||!Pixels(source).SequenceEqual(pixels))
            throw new Exception("GDI+ to frozen WPF data/DPI lifetime changed");
        using var result=new System.Drawing.Bitmap(Width,Height,System.Drawing.Imaging.PixelFormat.Format24bppRgb);
        data=result.LockBits(bounds,System.Drawing.Imaging.ImageLockMode.WriteOnly,result.PixelFormat);
        try {source.CopyPixels(Int32Rect.Empty,data.Scan0,data.Height*data.Stride,data.Stride);}
        finally {result.UnlockBits(data);}
        for(int y=0;y<Height;++y)for(int x=0;x<Width;++x) {
            var color=result.GetPixel(x,y);int at=y*Stride+x*3;
            if(color.B!=pixels[at]||color.G!=pixels[at+1]||color.R!=pixels[at+2])throw new Exception("WPF to GDI+ padded-row pixels changed");
        }
        Console.WriteLine("original WPF/GDI+ Scan0 conversion, padded rows, DPI and frozen data lifetime PASS");
    }
    static byte[] Pixels(BitmapSource bitmap) {
        var converted=new FormatConvertedBitmap(bitmap,PixelFormats.Bgr24,null,0);
        var data=new byte[Stride*Height];converted.CopyPixels(data,Stride,0);return data;
    }
    [STAThread] static void Main() {
        var pixels=new byte[Stride*Height];
        for(int y=0;y<Height;++y)for(int x=0;x<Width;++x){int at=y*Stride+x*3;pixels[at]=(byte)(x*7);pixels[at+1]=(byte)(y*9);pixels[at+2]=(byte)((x+y)*4);}
        CheckDrawingInterop(pixels);
        var source=BitmapSource.Create(Width,Height,96,96,PixelFormats.Bgr24,null,pixels,Stride);source.Freeze();
        foreach(var encoder in new BitmapEncoder[]{new PngBitmapEncoder(),new BmpBitmapEncoder(),new TiffBitmapEncoder{Compression=TiffCompressOption.Zip},new WmpBitmapEncoder{Lossless=true},new JpegBitmapEncoder{QualityLevel=100}}) {
            encoder.Frames.Add(BitmapFrame.Create(source));using var stream=new MemoryStream();encoder.Save(stream);var bytes=stream.ToArray();if(bytes.Length<30)throw new Exception("encoder produced no image data");stream.Position=0;
            var decoder=BitmapDecoder.Create(stream,BitmapCreateOptions.PreservePixelFormat,BitmapCacheOption.OnLoad);if(decoder.Frames.Count!=1)throw new Exception("single image frame count");var frame=decoder.Frames[0];if(frame.PixelWidth!=Width||frame.PixelHeight!=Height)throw new Exception("image dimensions changed");
            var actual=Pixels(frame);if(encoder is JpegBitmapEncoder){double average=actual.Zip(pixels,(a,b)=>Math.Abs(a-b)).Average();if(average>8)throw new Exception("JPEG pixels do not retain the encoded image: "+average);}else if(!pixels.SequenceEqual(actual))throw new Exception("lossless codec changed original pixels: "+encoder.GetType().Name);
            using var file=File.Create(Path.Combine(AppContext.BaseDirectory,encoder.GetType().Name+".image"));file.Write(bytes);Console.WriteLine($"original WPF codec {encoder.GetType().Name}: {bytes.Length} bytes, exact dimensions/pixel checks PASS");
        }
        // Greenshot uses ImageQualityLevel and CacheOption.None before CopyPixels.
        // Keep the stream alive while forcing lazy decoding, then retain copied pixels.
        foreach(float quality in new[]{0.75f,1.0f}) {
            var encoder=new WmpBitmapEncoder{ImageQualityLevel=quality};
            encoder.Frames.Add(BitmapFrame.Create(source));byte[] actual;
            using(var stream=new MemoryStream()) {
                encoder.Save(stream);stream.Position=0;
                var decoder=BitmapDecoder.Create(stream,BitmapCreateOptions.PreservePixelFormat,BitmapCacheOption.None);
                if(!(decoder is WmpBitmapDecoder)||decoder.Frames.Count!=1)throw new Exception("JPEG XR decoder/frame identity");
                var frame=decoder.Frames[0];
                if(frame.PixelWidth!=Width||frame.PixelHeight!=Height)throw new Exception("JPEG XR lazy dimensions");
                actual=Pixels(frame);
            }
            GC.Collect(2,GCCollectionMode.Forced,true,true);
            double error=actual.Zip(pixels,(a,b)=>Math.Abs(a-b)).Average();
            if(error>8)throw new Exception("JPEG XR lazy pixels changed: "+error);
            Console.WriteLine($"original WPF JPEG XR quality {quality}: lazy decode mean pixel error {error:F3} PASS");
        }
        var blackWhite=new byte[Width*Height];for(int y=0;y<Height;++y)for(int x=0;x<Width;++x)blackWhite[y*Width+x]=(byte)((x+y)%2);
        var indexed=BitmapSource.Create(Width,Height,96,96,PixelFormats.Indexed8,BitmapPalettes.BlackAndWhite,blackWhite,Width);var gif=new GifBitmapEncoder();gif.Frames.Add(BitmapFrame.Create(indexed));using(var stream=new MemoryStream()){gif.Save(stream);stream.Position=0;var decoder=BitmapDecoder.Create(stream,BitmapCreateOptions.PreservePixelFormat,BitmapCacheOption.OnLoad);if(!Pixels(indexed).SequenceEqual(Pixels(decoder.Frames[0])))throw new Exception("GIF palette/pixels changed");}
        var alpha=new byte[Width*Height*4];for(int i=0;i<alpha.Length;i+=4){alpha[i]=50;alpha[i+1]=90;alpha[i+2]=130;alpha[i+3]=(byte)(i/4%256);}var translucent=BitmapSource.Create(Width,Height,96,96,PixelFormats.Bgra32,null,alpha,Width*4);var png=new PngBitmapEncoder();png.Frames.Add(BitmapFrame.Create(translucent));BitmapFrame retained;
        using(var stream=new MemoryStream()){png.Save(stream);stream.Position=0;retained=BitmapDecoder.Create(stream,BitmapCreateOptions.PreservePixelFormat,BitmapCacheOption.OnLoad).Frames[0];}
        retained.Freeze();GC.Collect(2,GCCollectionMode.Forced,true,true);var copied=new byte[alpha.Length];new FormatConvertedBitmap(retained,PixelFormats.Bgra32,null,0).CopyPixels(copied,Width*4,0);if(!alpha.SequenceEqual(copied))throw new Exception("PNG alpha or OnLoad stream-independent lifetime changed");
        using(var corrupt=new MemoryStream(new byte[]{1,2,3,4,5})){bool rejected=false;try{BitmapDecoder.Create(corrupt,BitmapCreateOptions.None,BitmapCacheOption.OnLoad);}catch(FileFormatException){rejected=true;}catch(NotSupportedException){rejected=true;}if(!rejected)throw new Exception("corrupt image must be rejected");}
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original WPF image codec formats, palette/alpha, stream lifetime/GC and corrupt-input checks PASS");
    }
}
