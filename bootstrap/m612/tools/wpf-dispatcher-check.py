#!/usr/bin/env python3
"""Retain Factory's dispatcher lookup and scheduling expression in a small probe."""
from pathlib import Path
import re, sys
source, output = map(Path, sys.argv[1:])
original = source / "src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper/Factory.cpp"
text = original.read_text(encoding="utf-8-sig")
lookup = re.search(r"Dispatcher\^\s+currentDispatcher\s*=\s*Dispatcher::FromThread\(Thread::CurrentThread\);", text)
schedule = re.search(r"_timeStampCacheCleanupOp\s*=\s*currentDispatcher->BeginInvoke\(gcnew Action\(CleanupTimeStampCache\)\);", text)
if not lookup or not schedule:
    raise SystemExit("Factory dispatcher expressions changed; review the probe extraction")
output.write_text("""// Probe callback supplies observable effects; scheduling expressions below are unchanged WPF source.
using namespace System;
using namespace System::Threading;
using namespace System::Windows::Threading;
public ref class WpfFactoryDispatch abstract sealed {
    static DispatcherOperation^ _timeStampCacheCleanupOp;
    static int _count;
    static int _thread;
    static void CleanupTimeStampCache() { ++_count; _thread = Thread::CurrentThread->ManagedThreadId; }
public:
    static DispatcherOperation^ Schedule() {
""" + lookup.group() + "\n" + schedule.group() + """
        return _timeStampCacheCleanupOp;
    }
    static int Count() { return _count; }
    static int ThreadId() { return _thread; }
};
""")
