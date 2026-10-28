"""Prepare the original complete PowerShell SDK/console graph for source builds."""
from pathlib import Path
import hashlib,json,runpy,sys,xml.etree.ElementTree as E

source=Path(sys.argv[1]).resolve();here=Path(__file__).resolve().parent;root=source.parent
prepare=here.parent/'powershell-core/prepare.py'
assert prepare.is_file()
runpy.run_path(str(prepare),run_name='__main__')
runpy.run_path(str(here/'localaccounts-overlay.py'),run_name='__main__')
# Keep the existing source/compiler/archive adjustments. The core-only catalog
# target remains unused: build.sh invokes the original SDK _GetDependencies.
record=json.loads((root/'source-build-inputs.json').read_text())
record['scope']='original full Microsoft.PowerShell.SDK, original console entry/project and separate LocalAccounts; conditional GUI remains separate'
record['catalog']='original SDK _GetDependencies target and original TypeCatalogGen; no core-only reference substitution'
record['production_source_overlays']='localaccounts-source-overlay.json; exact formatting/private-static and legacy serialization annotations; all original sources retained'
record['limits']=['MMI3 and SqlClient remain experimental','serviced SDK host/runtime-pack integration remains','conditional WindowsDesktop/GraphicalHost and complete shipping integration remain','complete native/remote compatibility and .NET10 remain']
record['shared_prepare_sha256']=hashlib.sha256(prepare.read_bytes()).hexdigest()
(root/'source-build-inputs.json').write_text(json.dumps(record,indent=2)+'\n')
tree=E.parse(source/'Directory.Build.targets');target=E.SubElement(tree.getroot(),'Target',Name='RecordCompleteSdkCatalogInputs',AfterTargets='_GetDependencies')
E.SubElement(target,'WriteLinesToFile',File='$(_DependencyFile).metadata.txt',Lines="@(_ReferencesFromRAR->'%(FullPath)|%(NuGetPackageId)|%(OriginalItemSpec)')",Overwrite='true')
E.indent(tree);tree.write(source/'Directory.Build.targets',encoding='utf-8',xml_declaration=True)
print('PASS full original SDK/project graph retained; original catalog generation target selected')
