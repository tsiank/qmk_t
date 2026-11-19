param([Parameter(Mandatory = $true)][string]$PublishDirectory)
$ErrorActionPreference = 'Stop'
$required = @('Ciank67Music.exe','Ciank67Music.dll','Ciank67Music.pri','App.xbf','MainWindow.xbf','Ciank67Music.runtimeconfig.json','Assets/Ciank67Music.ico')
foreach ($name in $required) {
    if (-not (Test-Path (Join-Path $PublishDirectory $name))) { throw "Missing application file: $name" }
}
$forbidden = @('coreclr.dll','hostfxr.dll','hostpolicy.dll','System.Private.CoreLib.dll',
    'Microsoft.UI.Xaml.dll','Microsoft.UI.Xaml.Controls.dll','DirectML.dll','onnxruntime.dll',
    'Microsoft.Windows.Search*','Microsoft.Windows.Widgets*','Microsoft.Windows.AI*','Microsoft.Asg.SemanticIndex*',
    'WebView2Loader.dll','Microsoft.Web.WebView2.Core.dll','Microsoft.Web.WebView2.WinForms.dll','Microsoft.Web.WebView2.Wpf.dll')
foreach ($pattern in $forbidden) {
    $matches = @(Get-ChildItem $PublishDirectory -File -Recurse -Filter $pattern)
    if ($matches.Count -gt 0) { throw "Unneeded runtime/component found: $($matches.FullName -join ', ')" }
}
$runtime = Get-Content (Join-Path $PublishDirectory 'Ciank67Music.runtimeconfig.json') -Raw | ConvertFrom-Json
if ($runtime.runtimeOptions.includedFrameworks) { throw 'The app is still self-contained' }
$frameworks = @($runtime.runtimeOptions.framework) + @($runtime.runtimeOptions.frameworks)
if (-not ($frameworks | Where-Object { $_ -and $_.name -eq 'Microsoft.NETCore.App' })) {
    throw 'Expected a dependency on the shared .NET runtime'
}
Write-Host 'Lightweight publish verified: shared runtimes, required UI resources, no unused native components.'
