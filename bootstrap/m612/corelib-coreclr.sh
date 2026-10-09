#!/bin/bash
# System.Private.CoreLib for the CoreCLR flavor of dotnet/runtime 8.0.0 (win-x64):
# src/coreclr/System.Private.CoreLib + the shared CoreLib sources, built like
# corelib.sh builds the Mono one, into m612/netcoreapp-8.0.0-coreclr/lib.
# Its FCalls/QCalls and field layouts must match coreclr.sh's coreclr.dll (same tree).
# usage: corelib-coreclr.sh <csc.exe> [step...]       steps: gen events compile
exec env FLAVOR=coreclr bash "$(cd "$(dirname "$0")" && pwd)/corelib.sh" "$@"
