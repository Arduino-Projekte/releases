param(
    [string]$RepoRoot = "C:\GitHub\releases",
    [string]$StatusFile = "",
    [string]$ProjectName = "",
    [ValidateSet("ask", "on", "off")]
    [string]$PromptMode = "ask",
    [ValidateSet("yes", "no")]
    [string]$DefaultAnswer = "yes",
    [int]$TimeoutSeconds = 60,
    [switch]$StrictExit
)

# ============================================================
# Git-Veröffentlichung für Firmware-Releases
#
# Liest die after_build_*_status.json, nimmt daraus BIN/JSON-Pfade,
# committet nur Dateien innerhalb von C:\GitHub\releases
# und pusht sie nach Rückfrage mit 60s-Timeout.
# ============================================================

function Write-Info {
    param([string]$Text)
    Write-Host $Text
}

function Write-Warn {
    param([string]$Text)
    Write-Host ("WARNUNG: " + $Text)
}

function Read-JsonFileSafe {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    if (-not (Test-Path -LiteralPath $Path)) { return $null }

    try {
        return Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    }
    catch {
        Write-Warn ("Statusdatei konnte nicht gelesen werden: {0}" -f $Path)
        return $null
    }
}

function Get-Prop {
    param(
        $Object,
        [string[]]$Names
    )

    if ($null -eq $Object) { return $null }

    foreach ($name in $Names) {
        if ($Object.PSObject.Properties.Name -contains $name) {
            return $Object.$name
        }
    }

    return $null
}

function Get-RelativePathInsideRepo {
    param(
        [string]$RepoRootPath,
        [string]$FilePath
    )

    if ([string]::IsNullOrWhiteSpace($FilePath)) { return $null }
    if (-not (Test-Path -LiteralPath $FilePath)) { return $null }

    try {
        $repoFull = (Resolve-Path -LiteralPath $RepoRootPath -ErrorAction Stop).Path.TrimEnd('\', '/')
        $fileFull = (Resolve-Path -LiteralPath $FilePath -ErrorAction Stop).Path

        if (-not $fileFull.StartsWith($repoFull, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $null
        }

        $rel = $fileFull.Substring($repoFull.Length).TrimStart('\', '/')
        if ([string]::IsNullOrWhiteSpace($rel)) { return $null }

        return ($rel -replace '\\', '/')
    }
    catch {
        return $null
    }
}

function Read-YesNoWithTimeout {
    param(
        [string]$Question,
        [ValidateSet("yes", "no")]
        [string]$Default = "yes",
        [int]$Timeout = 60
    )

    $defaultText = if ($Default -eq "yes") { "Ja" } else { "Nein" }
    $hint = if ($Default -eq "yes") { "[J/n]" } else { "[j/N]" }

    Write-Host ""
    Write-Host ("{0} {1}" -f $Question, $hint)
    Write-Host ("Standard nach {0} Sekunden: {1}" -f $Timeout, $defaultText)
    Write-Host "Taste J oder N druecken. Enter nimmt den Standard."

    $end = (Get-Date).AddSeconds($Timeout)

    while ((Get-Date) -lt $end) {
        $remaining = [Math]::Max(0, [int][Math]::Ceiling(($end - (Get-Date)).TotalSeconds))
        Write-Host -NoNewline ("`rNoch {0} Sekunden... " -f $remaining)

        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)

            if ($key.Key -eq [ConsoleKey]::Enter) {
                Write-Host ""
                return ($Default -eq "yes")
            }

            $ch = [string]$key.KeyChar
            if ($ch -match '^[jJyY]$') {
                Write-Host ""
                return $true
            }

            if ($ch -match '^[nN]$') {
                Write-Host ""
                return $false
            }
        }

        Start-Sleep -Milliseconds 250
    }

    Write-Host ""
    return ($Default -eq "yes")
}

function Invoke-Git {
    param(
        [string[]]$Arguments,
        [switch]$AllowFailure
    )

    & git @Arguments
    $exit = $LASTEXITCODE

    if ($exit -ne 0 -and -not $AllowFailure) {
        throw ("Git-Befehl fehlgeschlagen: git {0} ; ExitCode={1}" -f ($Arguments -join " "), $exit)
    }

    return $exit
}

Write-Host ""
Write-Host "========================================"
Write-Host "Git-Veröffentlichung prüfen"
Write-Host "========================================"

if ($PromptMode -eq "off") {
    Write-Host "Git-Veröffentlichung ist deaktiviert."
    exit 0
}

if (-not (Test-Path -LiteralPath $RepoRoot)) {
    Write-Warn ("Release-Repository nicht gefunden: {0}" -f $RepoRoot)
    if ($StrictExit) { exit 1 } else { exit 0 }
}

try {
    Invoke-Git -Arguments @("-C", $RepoRoot, "rev-parse", "--is-inside-work-tree") | Out-Null
}
catch {
    Write-Warn ("Ordner ist kein Git-Repository: {0}" -f $RepoRoot)
    if ($StrictExit) { exit 1 } else { exit 0 }
}

$status = Read-JsonFileSafe -Path $StatusFile

if (-not $status) {
    Write-Warn "Keine after_build-Statusdatei gefunden. Git-Veröffentlichung wird übersprungen."
    if ($StrictExit) { exit 1 } else { exit 0 }
}

if ([string]::IsNullOrWhiteSpace($ProjectName)) {
    $ProjectName = Get-Prop -Object $status -Names @("project", "name")
}
if ([string]::IsNullOrWhiteSpace($ProjectName)) {
    $ProjectName = "Firmware"
}

$version = Get-Prop -Object $status -Names @("version", "fw_version", "ui_version")
if ([string]::IsNullOrWhiteSpace($version)) {
    $version = "unbekannte Version"
}

$targetBin = Get-Prop -Object $status -Names @("target_bin", "bin_target", "firmware_target")
$updateJson = Get-Prop -Object $status -Names @("update_json", "update_json_path", "json_target")
$versionsJson = Get-Prop -Object $status -Names @("versions_json", "versions_json_path")
$sha256 = Get-Prop -Object $status -Names @("sha256", "sha", "hash")
$targetMode = Get-Prop -Object $status -Names @("target_mode", "mode", "zielmodus")

$candidateFiles = New-Object System.Collections.Generic.List[string]
foreach ($p in @($targetBin, $updateJson, $versionsJson)) {
    if (-not [string]::IsNullOrWhiteSpace($p)) {
        if (Test-Path -LiteralPath $p) {
            if (-not $candidateFiles.Contains([string]$p)) {
                [void]$candidateFiles.Add([string]$p)
            }
        }
    }
}

$relativeFiles = New-Object System.Collections.Generic.List[string]
foreach ($p in $candidateFiles) {
    $rel = Get-RelativePathInsideRepo -RepoRootPath $RepoRoot -FilePath $p
    if ($rel) {
        if (-not $relativeFiles.Contains($rel)) {
            [void]$relativeFiles.Add($rel)
        }
    }
}

if ($relativeFiles.Count -eq 0) {
    Write-Host "Keine Release-Dateien innerhalb des Git-Repositories gefunden."
    Write-Host ("Repo: {0}" -f $RepoRoot)
    if ($targetBin) { Write-Host ("BIN Ziel: {0}" -f $targetBin) }
    if ($updateJson) { Write-Host ("update.json: {0}" -f $updateJson) }
    Write-Host "Git-Veröffentlichung wird übersprungen."
    exit 0
}

Write-Host ("Projekt:   {0}" -f $ProjectName)
Write-Host ("Version:   {0}" -f $version)
if ($targetMode) { Write-Host ("Zielmodus: {0}" -f $targetMode) }
Write-Host "Dateien:"
foreach ($rel in $relativeFiles) {
    Write-Host (" - {0}" -f $rel)
}

$doPublish = $false
if ($PromptMode -eq "on") {
    $doPublish = $true
}
elseif ($PromptMode -eq "ask") {
    $doPublish = Read-YesNoWithTimeout -Question ("Release-Dateien fuer {0} {1} nach Git committen und pushen?" -f $ProjectName, $version) -Default $DefaultAnswer -Timeout $TimeoutSeconds
}

if (-not $doPublish) {
    Write-Host "Git-Veröffentlichung wurde übersprungen."
    exit 0
}

try {
    foreach ($rel in $relativeFiles) {
        Invoke-Git -Arguments @("-C", $RepoRoot, "add", "--", $rel) | Out-Null
    }

    & git -C $RepoRoot diff --cached --quiet
    $diffExit = $LASTEXITCODE

    if ($diffExit -eq 0) {
        Write-Host "Keine Änderungen zum Committen gefunden."
        exit 0
    }

    $subject = "{0}: Firmware {1}" -f $ProjectName, $version

    $bodyLines = New-Object System.Collections.Generic.List[string]
    [void]$bodyLines.Add("Automatisch nach erfolgreichem Arduino-CLI-Build veröffentlicht.")
    [void]$bodyLines.Add("")
    [void]$bodyLines.Add("Dateien:")
    foreach ($rel in $relativeFiles) {
        [void]$bodyLines.Add("- " + $rel)
    }
    if ($sha256) {
        [void]$bodyLines.Add("")
        [void]$bodyLines.Add("SHA256: " + $sha256)
    }
    if ($targetMode) {
        [void]$bodyLines.Add("Zielmodus: " + $targetMode)
    }

    $body = ($bodyLines -join "`n")

    Write-Host ""
    Write-Host ("Commit: {0}" -f $subject)

    Invoke-Git -Arguments @("-C", $RepoRoot, "commit", "-m", $subject, "-m", $body) | Out-Null

    Write-Host ""
    Write-Host "Push nach origin..."
    Invoke-Git -Arguments @("-C", $RepoRoot, "push") | Out-Null

    Write-Host ""
    Write-Host "Git-Veröffentlichung abgeschlossen."
    exit 0
}
catch {
    Write-Warn $_.Exception.Message
    Write-Warn "Firmware-Build bleibt gültig; nur Git-Veröffentlichung ist fehlgeschlagen."
    if ($StrictExit) { exit 1 } else { exit 0 }
}
