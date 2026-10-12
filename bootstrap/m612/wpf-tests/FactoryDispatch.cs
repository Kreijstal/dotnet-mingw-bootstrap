using System;
using System.Threading;
using System.Windows.Threading;
var dispatcher = Dispatcher.CurrentDispatcher;
var operation = WpfFactoryDispatch.Schedule();
if (operation.Status != DispatcherOperationStatus.Pending || WpfFactoryDispatch.Count() != 0)
    throw new Exception("Factory callback was not queued asynchronously");
GC.Collect(); GC.WaitForPendingFinalizers();
var frame = new DispatcherFrame();
_ = dispatcher.BeginInvoke(DispatcherPriority.Background, new Action(() => frame.Continue = false));
Dispatcher.PushFrame(frame);
if (operation.Status != DispatcherOperationStatus.Completed || WpfFactoryDispatch.Count() != 1 ||
    WpfFactoryDispatch.ThreadId() != Thread.CurrentThread.ManagedThreadId)
    throw new Exception("Factory callback did not complete once on its dispatcher thread");
operation = WpfFactoryDispatch.Schedule();
frame = new DispatcherFrame();
_ = dispatcher.BeginInvoke(DispatcherPriority.Background, new Action(() => frame.Continue = false));
Dispatcher.PushFrame(frame);
if (operation.Status != DispatcherOperationStatus.Completed || WpfFactoryDispatch.Count() != 2)
    throw new Exception("repeated Factory scheduling did not complete");
Console.WriteLine("upstream WPF Factory dispatcher scheduling OK");
