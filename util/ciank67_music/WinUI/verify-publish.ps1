param([Parameter(Mandatory = $true)][string]$PublishDirectory)
$ErrorActionPreference = 'Stop'
$required = @('Samples/小星星-柔和钢琴.mid','Samples/小星星-音乐盒.mid','小星星演奏说明.txt','Qmknrf5.exe','Qmknrf5.dll','Qmknrf5.Core.dll','Qmknrf5.pri','App.xbf','MainWindow.xbf','Qmknrf5.runtimeconfig.json','Assets/Qmknrf5.ico','Layouts/ciank67.json','Layouts/ciank_pad.json',
    'Tools/nrfutil/nrfutil.exe','Tools/README.txt',
    'Tools/bootloader/nrf52840_xxaa.hex','Tools/bootloader/s140_nrf52_6.1.1_softdevice.hex',
    'Tools/bootloader/pca10056_bootloader-0.3.2_s140_6.1.1.hex',
    'Tools/bootloader/flush_nordic_bootloader.bat','Tools/bootloader/flush_adafruit_bootloader.bat')
foreach ($name in $required) {
    if (-not (Test-Path (Join-Path $PublishDirectory $name))) { throw "Missing application file: $name" }
}
# Check bundled resources against the manifest so damaged/missing tools are caught before distribution.
foreach ($line in Get-Content (Join-Path $PublishDirectory 'Tools/README.txt')) {
    if ($line -match '^([a-fA-F0-9]{64})  (.+)$') {
        $expected = $Matches[1]; $relative = $Matches[2]
        $actual = (Get-FileHash (Join-Path $PublishDirectory ('Tools/' + $relative)) -Algorithm SHA256).Hash
        if ($actual -ne $expected) { throw "Bundled flashing resource differs: $relative" }
    }
}
$forbidden = @('Ciank67.Music.*','Ciank67.Midi.*','coreclr.dll','hostfxr.dll','hostpolicy.dll','System.Private.CoreLib.dll',
    'Microsoft.UI.Xaml.dll','Microsoft.UI.Xaml.Controls.dll','DirectML.dll','onnxruntime.dll',
    'Microsoft.Windows.Search*','Microsoft.Windows.Widgets*','Microsoft.Windows.AI*','Microsoft.Asg.SemanticIndex*',
    'WebView2Loader.dll','Microsoft.Web.WebView2.Core.dll','Microsoft.Web.WebView2.WinForms.dll','Microsoft.Web.WebView2.Wpf.dll')
foreach ($pattern in $forbidden) {
    $matches = @(Get-ChildItem $PublishDirectory -File -Recurse -Filter $pattern)
    if ($matches.Count -gt 0) { throw "Unneeded runtime/component found: $($matches.FullName -join ', ')" }
}
$runtime = Get-Content (Join-Path $PublishDirectory 'Qmknrf5.runtimeconfig.json') -Raw | ConvertFrom-Json
if ($runtime.runtimeOptions.includedFrameworks) { throw 'The app is still self-contained' }
$frameworks = @($runtime.runtimeOptions.framework) + @($runtime.runtimeOptions.frameworks)
if (-not ($frameworks | Where-Object { $_ -and $_.name -eq 'Microsoft.NETCore.App' })) {
    throw 'Expected a dependency on the shared .NET runtime'
}
Write-Host 'Lightweight publish verified: shared runtimes, required UI resources, no unused native components.'
