param([string]$Config = "Release", [string]$Target = "Build")
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { Write-Error "MSBuild not found"; exit 1 }
& $msbuild "$PSScriptRoot\TelemFFB-Panel.vcxproj" /t:$Target /p:Configuration=$Config /p:Platform=x64 /m /v:minimal
exit $LASTEXITCODE
