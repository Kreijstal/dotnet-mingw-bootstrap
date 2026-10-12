using System;
using MS.Internal.Text.TextInterface;

static class Check {
    static void Main() {
        for (int i = 0; i < 32; ++i) {
            var face = FontFace.Create();
            var faceHandle = face.Handle();
            if (faceHandle.IsInvalid || faceHandle.IsClosed) throw new Exception("font face ownership missing");
            var file = face.GetFileZero();
            if (file == null) throw new Exception("real font face returned no file");
            var fileHandle = file.Handle();
            if (fileHandle.IsInvalid || fileHandle.IsClosed) throw new Exception("first font file ownership missing");
            face.Dispose(); face.Dispose();
            if (!faceHandle.IsInvalid || !faceHandle.IsClosed) throw new Exception("font face cleanup failed");
            GC.Collect(2, GCCollectionMode.Forced, true, true);
            if (file.Analyze() <= 0) throw new Exception("returned file lost its COM reference or disagrees with native Analyze");
            file.Dispose(); file.Dispose();
            if (!fileHandle.IsInvalid || !fileHandle.IsClosed) throw new Exception("font file cleanup failed");
        }
        Console.WriteLine("original WPF FontFace::GetFileZero, real DirectWrite array/COM ownership, GC and repeated disposal OK");
    }
}
