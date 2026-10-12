using Callback = int(*)(int);
extern "C" __declspec(dllexport) int call_delegate(Callback callback, int value) { return callback(value); }
