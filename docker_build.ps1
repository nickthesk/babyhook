$ErrorActionPreference = "Stop"

if (-not $env:DOCKER_BUILDKIT) {
    $env:DOCKER_BUILDKIT = "1"
}

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw "docker is required"
}

$image = if ($env:CATHOOK_DOCKER_IMAGE) { $env:CATHOOK_DOCKER_IMAGE } else { "cathook-builder:ubuntu24.04" }
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot ".")).Path
$dockerfilePath = Join-Path $repoRoot "docker\\builder.Dockerfile"

function Normalize-CathookMode {
    param(
        [string] $Value,
        [bool] $AllowBoth
    )

    switch ($Value) {
        { $_ -in @("default", "non-textmode", "non_textmode", "normal", "gui", "0", "1") } { return "default" }
        { $_ -in @("textmode", "text", "2") } { return "textmode" }
        { $_ -in @("both", "all", "3") -and $AllowBoth } { return "both" }
        default { throw "Invalid Cathook mode: $Value" }
    }
}

function Normalize-TextmodeMode {
    param([string] $Value)

    switch ($Value) {
        { $_ -in @("1", "true", "TRUE", "True", "yes", "YES", "Yes", "y", "Y", "on", "ON", "On") } { return "textmode" }
        { $_ -in @("", "0", "false", "FALSE", "False", "no", "NO", "No", "n", "N", "off", "OFF", "Off") } { return "default" }
        default { throw "Invalid textmode value: $Value" }
    }
}

function Get-CathookModePreferenceFile {
    if ($env:CATHOOK_MODE_FILE) {
        return $env:CATHOOK_MODE_FILE
    }

    if ($env:CATHOOK_CONFIG_DIR) {
        return (Join-Path $env:CATHOOK_CONFIG_DIR "mode")
    }

    if ($env:APPDATA) {
        return (Join-Path $env:APPDATA "cathook\\mode")
    }

    return (Join-Path $HOME ".config\\cathook\\mode")
}

function Read-CathookModePreference {
    param([bool] $AllowBoth)

    $modeFile = Get-CathookModePreferenceFile
    if (-not (Test-Path -LiteralPath $modeFile)) {
        return $null
    }

    $mode = (Get-Content -LiteralPath $modeFile -TotalCount 1)
    return Normalize-CathookMode $mode $AllowBoth
}

function Write-CathookModePreference {
    param([string] $Mode)

    $modeFile = Get-CathookModePreferenceFile
    $modeDir = Split-Path -Parent $modeFile
    New-Item -ItemType Directory -Force -Path $modeDir | Out-Null
    Set-Content -LiteralPath $modeFile -Value $Mode
}

function Select-CathookMode {
    param([bool] $AllowBoth)

    if ($env:CATHOOK_MODE) {
        return Normalize-CathookMode $env:CATHOOK_MODE $AllowBoth
    }

    if ($env:CAT_BUILD_MODE) {
        return Normalize-CathookMode $env:CAT_BUILD_MODE $AllowBoth
    }

    if ($null -ne [Environment]::GetEnvironmentVariable("CATHOOK_TEXTMODE")) {
        return Normalize-TextmodeMode $env:CATHOOK_TEXTMODE
    }

    if ($null -ne [Environment]::GetEnvironmentVariable("TEXTMODE")) {
        return Normalize-TextmodeMode $env:TEXTMODE
    }

    $savedMode = Read-CathookModePreference $AllowBoth
    if ($savedMode) {
        return $savedMode
    }

    while ($true) {
        Write-Host ""
        Write-Host "Cathook mode is not set yet."
        Write-Host "1) default  - normal SDL/GUI mode"
        Write-Host "2) textmode - textmode binary"
        if ($AllowBoth) {
            Write-Host "3) both     - build both binaries"
            $answer = Read-Host "Choose mode [1/2/3]"
        } else {
            $answer = Read-Host "Choose mode [1/2]"
        }

        try {
            $mode = Normalize-CathookMode $answer $AllowBoth
            Write-CathookModePreference $mode
            Write-Host "Saved Cathook mode '$mode' to $(Get-CathookModePreferenceFile)."
            return $mode
        } catch {
            Write-Host "Please choose a valid mode."
        }
    }
}

$selectedMode = Select-CathookMode $true

if (-not $env:CATHOOK_DOCKER_IMAGE) {
    $needsRebuild = $env:CATHOOK_DOCKER_REBUILD -eq "1"
    if (-not $needsRebuild) {
        $previousErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & docker image inspect $image 2>$null 1>$null
        $ErrorActionPreference = $previousErrorActionPreference
        $needsRebuild = $LASTEXITCODE -ne 0
    }

    if ($needsRebuild) {
        docker build -t $image -f $dockerfilePath $repoRoot
    }
}

$containerScript = @'
set -euo pipefail
chmod +x build.sh
if [ "${CATHOOK_DOCKER_INSTALL_PACKAGES:-0}" = "1" ]; then
    chmod +x packages/packages.sh
    ./packages/packages.sh
fi
./build.sh
'@
$containerScript = $containerScript -replace "`r`n", "`n"

docker run --rm `
    -e CAT_BUILD_MODE="${selectedMode}" `
    -e CATHOOK_TEXTMODE="${env:CATHOOK_TEXTMODE}" `
    -e CATHOOK_DOCKER_INSTALL_PACKAGES="${env:CATHOOK_DOCKER_INSTALL_PACKAGES}" `
    -v "${repoRoot}:/workspace" `
    -w /workspace `
    $image `
    bash -lc $containerScript
