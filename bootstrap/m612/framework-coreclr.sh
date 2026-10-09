#!/bin/bash
# The CoreCLR flavor of the shared framework (framework.sh with FLAVOR=coreclr) in its own
# SDK root, m612/dotnet-sdk-8.0.131-net8-coreclr (a copy of the net8.0 SDK root of
# sdk-layout-net8.sh with this framework instead of the Mono one).
# usage: framework-coreclr.sh [step...]     steps: newroot layout install runtimepack check test
exec env FLAVOR=coreclr bash "$(cd "$(dirname "$0")" && pwd)/framework.sh" "${@:-newroot layout install runtimepack check test}"
