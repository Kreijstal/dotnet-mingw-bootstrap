# sdkproj.sh: compile one SDK-style C# project for .NET 8 with a direct csc
# command line (S11).  Source after common.sh.  tools/sdkproj.py evaluates the
# project; this does what the SDK targets would: resources (resgen, ResxGen for
# GenerateSource), global usings, assembly attributes, references, csc.
#
# sdkproj <csproj> [extra csc args...]   -> $SP_OUT/<AssemblyName>.dll
# Configuration (set before the call; arrays may be empty):
#   SP_EVAL=(-p K=V ... --pre f --post f)   evaluation inputs (TargetFramework is added)
#   SP_TF       target framework for the evaluation (default net8.0)
#   SP_OUT      output directory (also referenced: assemblies built earlier there)
#   SP_REFS=()  extra reference assemblies/directories (after the ref pack and OOB libs)
#   SP_KEY      public-sign key name (keys/<KEY>-*.pubkey.hex); "-" = unsigned; default from
#               StrongNameKeyId, else SP_DEFAULT_KEY (Arcade repos: MSShared), else unsigned
#   SP_VER      AssemblyVersion (default: the project's AssemblyVersion/VersionPrefix, else 1.0.0.0)
#   SP_FILEVER, SP_INFOVER
#   SP_NAME     AssemblyName override
#   SP_NOWARN   extra -nowarn list
#   SP_DEFS=()  extra defines (-d:X)
#   SP_EXPLICIT=1  reference only the (transitive) ProjectReferences built earlier by sdkproj
#               (registry $M612/build/sdkproj/registry.tsv) instead of everything in SP_OUT; for
#               repos where a project compiles another project's sources in (dotnet/sdk resolvers)
#   SP_GENERATORS  source generators passed to every compile, like the analyzers/dotnet/cs of
#               the Microsoft.NETCore.App.Ref pack (default: LibraryImport + COM, built from source
#               by netfx-pkgs.sh against Roslyn 4.8, so compile with that Roslyn)
SP_TF=${SP_TF:-net8.0}
SP_GENERATORS=${SP_GENERATORS:-Microsoft.Interop.SourceGeneration Microsoft.Interop.LibraryImportGenerator Microsoft.Interop.ComInterfaceGenerator}
sp_prop() { sed -n "s/^$1=//p" "$SP_O/props"; }
SP_REG=$M612/build/sdkproj/registry.tsv
sp_prefs() { # the outputs of the transitive ProjectReferences of $SP_O (from the registry)
  [ -f "$SP_REG" ] || return 0
  python3 -S - "$SP_REG" "$SP_O/prefs" <<'EOPY'
import sys
sys.stdout.reconfigure(newline='\n')
reg = {}
for l in open(sys.argv[1], encoding='utf-8'):
    p, dll, prefs = l.rstrip('\n').split('\t')
    reg[p.lower()] = (dll, prefs)
out, todo, seen = [], [x.strip() for x in open(sys.argv[2], encoding='utf-8') if x.strip()], set()
while todo:
    p = todo.pop(0)
    if p.lower() in seen: continue
    seen.add(p.lower())
    if p.lower() not in reg:
        print('sdkproj: project reference not built: ' + p, file=sys.stderr); continue
    dll, prefs = reg[p.lower()]
    out.append(dll)
    todo += [x.strip() for x in open(prefs, encoding='utf-8') if x.strip()]
print('\n'.join(out))
EOPY
}
sdkproj() {
  local proj=$1; shift
  local name; name=$(basename "$proj" .csproj)
  SP_O=$M612/build/sdkproj/${SP_GROUP:-misc}/$name; rm -rf "${SP_O:?}"; mkdir -p "$SP_O"
  python3 -S "$M612_TOOLS_SRC/sdkproj.py" "$proj" "$SP_O" -p TargetFramework=$SP_TF "${SP_EVAL[@]}" | grep -v '^\[fc-sc\]' || true
  local an; an=${SP_NAME:-$(sp_prop AssemblyName)}; an=${an:-$name}
  local items=() res=() f mn gen cls ng=0
  mapfile -t items < "$SP_O/items"
  # resources
  while IFS=$'\t' read -r f mn gen cls; do
    [ -n "$f" ] || continue
    if [[ $f == *.resx ]]; then
      resgen "$f" "$SP_O/$mn" > /dev/null || die "resgen $f"
      res+=(-resource:"$SP_O/$mn,$mn")
      if [ "$gen" = 1 ]; then
        local gf; ng=$((ng+1)); gf=$SP_O/gen.$ng.$(basename "$f" .resx).g.cs
        resxgen "$f" "$gf" "$cls" --resource-name "${mn%.resources}" ${SP_RESX_OPTS:---format}
        items+=("$gf")
      fi
    else
      res+=(-resource:"$f,$mn")
    fi
  done < "$SP_O/resources"
  # global usings
  [ -s "$SP_O/usings" ] && { cp "$SP_O/usings" "$SP_O/GlobalUsings.g.cs"; items+=("$SP_O/GlobalUsings.g.cs"); }
  # defines: the project's + the TFM symbols the SDK adds
  local dargs=() d dl=(); IFS=';' read -ra dl < "$SP_O/defines"
  for d in "${dl[@]}"; do d=${d// /}; [ -n "$d" ] && dargs+=("-d:$d"); done
  case $SP_TF in
    net8.0) dargs+=("${NET8_DEFS[@]}") ;;
    netstandard2.0) dargs+=(-d:TRACE -d:RELEASE -d:NETSTANDARD -d:NETSTANDARD2_0 -d:NETSTANDARD2_0_OR_GREATER -d:NETSTANDARD1_0_OR_GREATER
      -d:NETSTANDARD1_1_OR_GREATER -d:NETSTANDARD1_2_OR_GREATER -d:NETSTANDARD1_3_OR_GREATER -d:NETSTANDARD1_4_OR_GREATER -d:NETSTANDARD1_5_OR_GREATER -d:NETSTANDARD1_6_OR_GREATER) ;;
    net6.0) dargs+=(-d:TRACE -d:RELEASE -d:NETCOREAPP -d:NET -d:NET6_0 -d:NET6_0_OR_GREATER -d:NET5_0_OR_GREATER -d:NETCOREAPP3_1_OR_GREATER
      -d:NETCOREAPP3_0_OR_GREATER -d:NETCOREAPP2_2_OR_GREATER -d:NETCOREAPP2_1_OR_GREATER -d:NETCOREAPP2_0_OR_GREATER -d:NETCOREAPP1_1_OR_GREATER -d:NETCOREAPP1_0_OR_GREATER) ;;
  esac
  dargs+=("${SP_DEFS[@]}")
  # assembly attributes (GenerateAssemblyInfo)
  local av fv iv
  av=${SP_VER:-$(sp_prop AssemblyVersion)}; [ -n "$av" ] || { av=$(sp_prop VersionPrefix); av=${av%%-*}; }
  [ -n "$av" ] || av=1.0.0.0
  [[ $av =~ ^[0-9]+\.[0-9]+$ ]] && av=$av.0.0; [[ $av =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] && av=$av.0
  fv=${SP_FILEVER:-$av}; iv=${SP_INFOVER:-$fv}
  local tfa; case $SP_TF in net8.0) tfa='.NETCoreApp,Version=v8.0", FrameworkDisplayName = ".NET 8.0' ;; net6.0) tfa='.NETCoreApp,Version=v6.0", FrameworkDisplayName = ".NET 6.0' ;; *) tfa='.NETStandard,Version=v2.0", FrameworkDisplayName = ".NET Standard 2.0' ;; esac
  {
    printf '[assembly: System.Reflection.AssemblyVersion("%s")]\n[assembly: System.Reflection.AssemblyFileVersion("%s")]\n' "$av" "$fv"
    printf '[assembly: System.Reflection.AssemblyInformationalVersion("%s+m612.bootstrap.from.source")]\n' "$iv"
    printf '[assembly: System.Reflection.AssemblyTitle("%s")]\n[assembly: System.Reflection.AssemblyProduct("%s")]\n' "$an" "$an"
    printf '[assembly: System.Runtime.Versioning.TargetFramework("%s")]\n' "$tfa"
  } > "$SP_O/AssemblyInfo.g.cs"
  # InternalsVisibleTo items (key of the friend = our key, as upstream signs everything with it)
  local key=${SP_KEY:-}
  if [ -z "$key" ]; then
    key=$(sp_prop StrongNameKeyId)
    case $key in MicrosoftShared) key=MSShared ;; '') key=${SP_DEFAULT_KEY:--} ;; esac
  fi
  local sign=() pk=
  if [ "$key" != - ]; then sign=(-publicsign -keyfile:"$(keyfile "$key")"); pk=", PublicKey=$(tr -d '\n\r' < "$KEYS/$key"-*.pubkey.hex)"; fi
  local i; while read -r i; do [ -n "$i" ] && echo "[assembly: System.Runtime.CompilerServices.InternalsVisibleTo(\"$i$pk\")]" >> "$SP_O/AssemblyInfo.g.cs"; done < "$SP_O/ivt"
  local target=library ext=dll; case $(sp_prop OutputType) in Exe|exe|WinExe) target=exe ;; esac
  local nullable; nullable=$(sp_prop Nullable); nullable=${nullable:-disable}
  local lv; lv=$(sp_prop LangVersion); case $lv in ''|latest|preview|default|Latest|Preview) lv=${SP_LANGVERSION:-12} ;; esac
  local unsafe=(); [ "$(sp_prop AllowUnsafeBlocks)" = true ] && unsafe=(-unsafe)
  local nw; nw=$(sp_prop NoWarn | tr -d ' \r' | tr ';' ',' | sed 's/^,*//; s/,*$//; s/,,*/,/g')
  local an_args=() g
  for g in $SP_GENERATORS; do an_args+=(-analyzer:"$M612/pkgs/analyzers/$g.dll"); done
  if [ ${#an_args[@]} -gt 0 ]; then
    printf 'is_global = true\nbuild_property.TargetFrameworkIdentifier = .NETCoreApp\nbuild_property.TargetFrameworkVersion = v8.0\nbuild_property.RootNamespace = %s\n' "$(sp_prop RootNamespace)" > "$SP_O/build.globalconfig"
    an_args+=(-analyzerconfig:"$SP_O/build.globalconfig")
  fi
  mkdir -p "$SP_OUT"
  local auto=("$SP_OUT") pr=()
  if [ -n "${SP_EXPLICIT:-}" ]; then auto=(); mapfile -t pr < <(sp_prefs | tr -d '\r'); fi
  NET8_SELF=$an csc "sp-$an" -target:$target -nostdlib -noconfig -optimize+ -debug:portable -deterministic -nullable:$nullable \
    -langversion:$lv "${unsafe[@]}" -out:"$SP_OUT/$an.$ext" "${sign[@]}" \
    -nowarn:CS1591,CS1573,CS0618,CS0612,CS1574,CS0436,CS8600,CS8601,CS8602,CS8603,CS8604,CS8618,CS8625,CS8619,CS8620,CS8765,CS8767,CS8769,CS8632${nw:+,$nw}${SP_NOWARN:+,$SP_NOWARN} \
    "${dargs[@]}" $(NET8_SELF=$an net8_refs "${auto[@]}" "${SP_REFS[@]}" "${pr[@]}") "${an_args[@]}" "$@" "${res[@]}" "$SP_O/AssemblyInfo.g.cs" "${items[@]}" \
    || die "$an failed ($B/obj/sp-$an.log)"
  SP_LAST=$SP_OUT/$an.$ext
  # registry: project -> output (+ its ProjectReferences) for SP_EXPLICIT builds
  local self; self=$(cat "$SP_O/self")
  mkdir -p "$(dirname "$SP_REG")"; touch "$SP_REG"
  { grep -viF "$self"$'\t' "$SP_REG" || true; printf '%s\t%s\t%s\n' "$self" "$(winpath "$SP_LAST")" "$(winpath "$SP_O/prefs")"; } > "$SP_REG.new"
  mv "$SP_REG.new" "$SP_REG"
}
