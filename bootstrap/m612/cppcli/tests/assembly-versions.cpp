// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System::Reflection;
[assembly:AssemblyVersion("1.2.3.4")];
[assembly:AssemblyFileVersionAttribute("7.8.9.10")];
[assembly:AssemblyInformationalVersion("0.3+source λ")];
[assembly:AssemblyTitle("Source-only C++/CLI")];
public ref class VersionedAssembly abstract sealed {
public: static int Answer() { return 42; }
};
