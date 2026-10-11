param([switch]$Demo)
$ErrorActionPreference = 'Stop'
$projectPath = Join-Path $PSScriptRoot 'TrackNode.DevTools.csproj'
if ($Demo) {
    dotnet run --project $projectPath --configuration Release -- --demo
} else {
    dotnet run --project $projectPath --configuration Release
}
exit $LASTEXITCODE
