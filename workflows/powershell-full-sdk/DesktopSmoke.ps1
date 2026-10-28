$ErrorActionPreference='Stop'
if ([Threading.Thread]::CurrentThread.ApartmentState -ne 'STA') { throw 'Desktop fixture requires STA' }
if ([Environment]::Version.ToString() -ne '8.0.31') { throw 'Serviced runtime required' }
Add-Type -AssemblyName PresentationFramework
Add-Type -Path (Join-Path $PSHOME 'Microsoft.PowerShell.GraphicalHost.dll')
$type=[Microsoft.PowerShell.Commands.ShowCommandInternal.CmdletControl]
if ($type.Assembly.Location -ne (Join-Path $PSHOME 'Microsoft.PowerShell.GraphicalHost.dll')) { throw 'Foreign GraphicalHost' }
# Exercise the original compiled BAML/control without creating a window or
# invoking the window classes' user-settings persistence handlers.
$control=$type::new()
$control.Measure([Windows.Size]::new(640,450))
$control.Arrange([Windows.Rect]::new(0,0,640,450))
$control.UpdateLayout()
if ($control.ActualWidth -ne 640 -or $control.ActualHeight -ne 450) { throw 'Original control layout' }
$children=[Windows.Media.VisualTreeHelper]::GetChildrenCount($control)
if ($children -lt 1) { throw 'Original control BAML missing visual content' }
$bitmap=[Windows.Media.Imaging.RenderTargetBitmap]::new(640,450,96,96,[Windows.Media.PixelFormats]::Pbgra32)
$bitmap.Render($control)
$pixels=[byte[]]::new(640*450*4)
$bitmap.CopyPixels($pixels,640*4,0)
$visible=0
for ($i=3; $i -lt $pixels.Length; $i+=4) { if ($pixels[$i] -ne 0) { $visible++ } }
if ($visible -eq 0) { throw 'Original control rendered no pixels' }
"PASS original GraphicalHost compiled BAML/Show-Command control,STA layout and source WPF raster: $visible visible pixels; $children visual children"
foreach ($assembly in [AppDomain]::CurrentDomain.GetAssemblies()) {
    if (-not $assembly.IsDynamic -and $assembly.Location -match '[\\/]Microsoft.NET[\\/]Framework') { throw "Framework fallback: $($assembly.Location)" }
}
