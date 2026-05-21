[CmdletBinding()]
param(
    [string]$ProjectRoot = "C:\Users\berts\Documents\uv390-self_dev\V2_MOTOROLA_FORMAT",
    [string]$CubeIdeExe = "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe",
    [string]$WorkspaceRoot = (Join-Path $PSScriptRoot ".headless-workspace"),
    [switch]$ReuseWorkspace,
    [switch]$StopOnError
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -Path $CubeIdeExe -PathType Leaf)) {
    $foundIde = Get-ChildItem -Path "C:\ST" -Recurse -Filter "stm32cubeidec.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName

    if ([string]::IsNullOrWhiteSpace($foundIde)) {
        throw "stm32cubeidec.exe niet gevonden. Geef -CubeIdeExe mee."
    }

    $CubeIdeExe = $foundIde
}

if (-not (Test-Path -Path $ProjectRoot -PathType Container)) {
    throw "ProjectRoot niet gevonden: $ProjectRoot"
}

$ProjectRoot = (Resolve-Path -Path $ProjectRoot).Path

$prepareBat = Join-Path $ProjectRoot "prepare.bat"
if (-not (Test-Path -Path $prepareBat -PathType Leaf)) {
    throw "prepare.bat niet gevonden in: $ProjectRoot"
}

$projectPath = Join-Path $ProjectRoot "MDUV380_firmware"
if (-not (Test-Path -Path $projectPath -PathType Container)) {
    throw "Projectmap niet gevonden: $projectPath"
}

if (-not (Test-Path -Path $WorkspaceRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $WorkspaceRoot -Force | Out-Null
}

if ($ReuseWorkspace) {
    $workspacePath = Join-Path $WorkspaceRoot "motorola_ws"
    if (-not (Test-Path -Path $workspacePath -PathType Container)) {
        New-Item -ItemType Directory -Path $workspacePath -Force | Out-Null
    }
}
else {
    $workspacePath = Join-Path $WorkspaceRoot ("motorola_ws_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
    New-Item -ItemType Directory -Path $workspacePath -Force | Out-Null
}

$projectUri = "file:/" + ($projectPath -replace "\\", "/")

$configs = @(
    "MDUV380_FW",
    "MDUV380_10W_PLUS_FW",
    "DM1701_FW",
    "RT84_FW",
    "JA_MDUV380_FW",
    "JA_MDUV380_10W_PLUS_FW",
    "JA_DM1701_FW",
    "JA_RT84_FW"
)

$results = New-Object System.Collections.Generic.List[object]

Push-Location $ProjectRoot
try {
    Write-Host "=== CONTEXT ==="
    Write-Host ("project root = " + $ProjectRoot)
    Write-Host ("workspace    = " + $workspacePath)
    Write-Host ("project uri  = " + $projectUri)

    Write-Host "=== PREPARE ==="
    & $prepareBat
    $prepareExit = $LASTEXITCODE
    Write-Host ("prepare exit=" + $prepareExit)

    if ($prepareExit -ne 0) {
        throw "prepare.bat faalde met exitcode $prepareExit"
    }

    Write-Host ""
    Write-Host "=== IMPORT PROJECT ==="
    & $CubeIdeExe `
        -nosplash `
        -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
        -data $workspacePath `
        -import $projectUri

    $importExit = $LASTEXITCODE
    Write-Host ("import exit=" + $importExit)

    if ($importExit -ne 0) {
        throw "Project import faalde met exitcode $importExit"
    }

    foreach ($cfg in $configs) {
        Write-Host ""
        Write-Host ("=== BUILD " + $cfg + " ===")

        $target = "MDUV380_firmware/$cfg"

        # Geen output capture: alle tool-output blijft live zichtbaar in de console.
        & $CubeIdeExe `
            -nosplash `
            -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
            -data $workspacePath `
            -cleanBuild $target

        $exitCode = $LASTEXITCODE
        $status = if ($exitCode -eq 0) { "OK" } else { "FAIL" }

        $results.Add([pscustomobject]@{
            Config   = $cfg
            ExitCode = $exitCode
            Status   = $status
        })

        if (($exitCode -ne 0) -and $StopOnError) {
            break
        }
    }
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "=== SUMMARY ==="
$results | Format-Table -AutoSize

$failed = $results | Where-Object { $_.ExitCode -ne 0 }
if ($failed.Count -gt 0) {
    exit 1
}

exit 0
