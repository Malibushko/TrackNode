param(
    [Parameter(Mandatory = $true)]
    [string] $Firmware,
    [string] $Volume = ''
)

$ErrorActionPreference = 'Stop'

function Test-XiaoVolume([string] $Directory)
{
    $infoPath = Join-Path $Directory 'INFO_UF2.TXT'

    if (-not (Test-Path -LiteralPath $infoPath -PathType Leaf))
    {
        return $false
    }

    $info = Get-Content -LiteralPath $infoPath -Raw

    return $info -match '(?im)^Board-ID:\s*Seeed_XIAO_nRF52840(?:_Sense)?\s*$'
}

if (-not (Test-Path -LiteralPath $Firmware -PathType Leaf))
{
    throw "UF2 firmware does not exist: $Firmware"
}

if ($Volume)
{
    if (-not (Test-XiaoVolume $Volume))
    {
        throw "Not a XIAO nRF52840 UF2 drive: $Volume (check INFO_UF2.TXT)."
    }

    $destination = $Volume
}
else
{
    $candidates = @(Get-PSDrive -PSProvider FileSystem | Where-Object { Test-XiaoVolume $_.Root })

    if ($candidates.Count -eq 0)
    {
        throw 'XIAO UF2 drive not found. Connect USB-C, press RESET twice, then run bluetooth_flash again.'
    }

    if ($candidates.Count -gt 1)
    {
        throw 'Multiple XIAO UF2 drives found. Set TRACK_NODE_BLUETOOTH_UF2_DIRECTORY to the intended drive.'
    }

    $destination = $candidates[0].Root
}

Write-Host "Copying $Firmware to $destination"
Copy-Item -LiteralPath $Firmware -Destination (Join-Path $destination 'zephyr.uf2') -Force
Write-Host 'UF2 copied. Hardware startup must be checked on the device.'
