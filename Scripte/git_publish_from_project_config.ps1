param(
    [string]$RepoRoot = "C:\GitHub\releases",
    [string]$ProjectConfigPath = "C:\GitHub\releases\Scripte\build_projects.json",

    [ValidateSet("ask", "on", "off")]
    [string]$PromptMode = "ask",

    [ValidateSet("yes", "no")]
    [string]$DefaultAnswer = "yes",

    [int]$TimeoutSeconds = 60,

    # Bei false bleibt der Firmware-Build auch dann OK, wenn Git fehlschlaegt.
    [bool]$StrictExit = $false
)

# ============================================================
# Git-Publish aus build_projects.json
# Version ohne try/catch-Block, damit PowerShell 5.1 sauber laeuft.
# ============================================================

function Finish-Script {
    param(
        [int]$Code,
        [string]$Message = ""
    )

    if (-not [string]::IsNullOrWhiteSpace($Message)) {
        Write-Host $Message
    }

    if ($StrictExit) {
        exit $Code
    }

    # Standard: Firmware-Build bleibt gueltig, auch wenn Git nicht klappt.
    exit 0
}

function Read-JsonFileSafe {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    if (-not (Test-Path -LiteralPath $Path)) { return $null }

    try {
        return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    }
    catch {
        Write-Host ("WARNUNG: JSON konnte nicht gelesen werden: {0}" -f $Path)
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

function Resolve-Bool {
    param(
        $Object,
        [string[]]$Names
    )

    if ($null -eq $Object) { return $null }

    foreach ($name in $Names) {
        if ($Object.PSObject.Properties.Name -contains $name) {
            $v = $Object.$name
            if ($null -eq $v) { continue }

            try {
                return [System.Convert]::ToBoolean($v)
            }
            catch {
                $s = ([string]$v).Trim().ToLowerInvariant()
                if ($s -in @("ja","yes","true","1","ok","success","erfolgreich")) { return $true }
                if ($s -in @("nein","no","false","0","fail","failed","fehler")) { return $false }
            }
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
        $repoFull = (Resolve-Path -LiteralPath $RepoRootPath -ErrorAction Stop).Path.TrimEnd('\','/')
        $fileFull = (Resolve-Path -LiteralPath $FilePath -ErrorAction Stop).Path

        if (-not $fileFull.StartsWith($repoFull, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $null
        }

        $rel = $fileFull.Substring($repoFull.Length).TrimStart('\','/')
        if ([string]::IsNullOrWhiteSpace($rel)) { return $null }

        return ($rel -replace '\\','/')
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
        Write-Host -NoNewline ("`rNoch {0,2} Sekunden... " -f $remaining)

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

function Invoke-GitChecked {
    param(
        [string[]]$Arguments,
        [string]$ErrorText
    )

    & git @Arguments
    $code = $LASTEXITCODE

    if ($code -ne 0) {
        Write-Host ("WARNUNG: {0}" -f $ErrorText)
        Write-Host ("Git-Befehl: git {0}" -f ($Arguments -join " "))
        Write-Host ("ExitCode: {0}" -f $code)
        return $false
    }

    return $true
}

Write-Host ""
Write-Host "========================================"
Write-Host "Git-Veroeffentlichung aus Projektliste"
Write-Host "========================================"

if ($PromptMode -eq "off") {
    Write-Host "Git-Veroeffentlichung ist deaktiviert."
    exit 0
}

if (-not (Test-Path -LiteralPath $ProjectConfigPath)) {
    Finish-Script -Code 1 -Message ("WARNUNG: build_projects.json nicht gefunden: {0}" -f $ProjectConfigPath)
}

if (-not (Test-Path -LiteralPath $RepoRoot)) {
    Finish-Script -Code 1 -Message ("WARNUNG: Release-Repository nicht gefunden: {0}" -f $RepoRoot)
}

$repoCheckOk = Invoke-GitChecked -Arguments @("-C", $RepoRoot, "rev-parse", "--is-inside-work-tree") -ErrorText "Ordner ist kein Git-Repository."
if (-not $repoCheckOk) {
    Finish-Script -Code 1 -Message "Git-Veroeffentlichung wird uebersprungen."
}

# Keine fremden bereits gestagten Dateien versehentlich mitcommitten.
& git -C $RepoRoot diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Host "WARNUNG: Im Release-Repository sind bereits Dateien staged."
    Write-Host "Bitte zuerst dort committen oder unstagen, damit der automatische Commit nichts Fremdes mitnimmt."
    Finish-Script -Code 1
}

$config = Read-JsonFileSafe -Path $ProjectConfigPath
if (-not $config -or -not $config.projects) {
    Finish-Script -Code 1 -Message "WARNUNG: Projektliste ist leer oder ungueltig."
}

$publishItems = @()

foreach ($project in @($config.projects)) {
    $afterStatusPath = $project.after_status_file
    if (-not $afterStatusPath) { $afterStatusPath = $project.AfterStatusFile }

    $status = Read-JsonFileSafe -Path $afterStatusPath
    if (-not $status) { continue }

    $afterOk = Resolve-Bool -Object $status -Names @("after_build_ok","after_ok","post_build_ok")
    if ($afterOk -ne $true) { continue }

    $projectName = if ($project.name) { [string]$project.name } else { [string](Get-Prop -Object $status -Names @("project","name")) }
    if ([string]::IsNullOrWhiteSpace($projectName)) { $projectName = "Firmware" }

    $version = Get-Prop -Object $status -Names @("version","fw_version","ui_version")
    if ([string]::IsNullOrWhiteSpace($version)) { $version = "unbekannte Version" }

    $sha256 = Get-Prop -Object $status -Names @("sha256","sha","hash")
    $targetMode = Get-Prop -Object $status -Names @("target_mode","mode","zielmodus")

    $paths = @(
        (Get-Prop -Object $status -Names @("target_bin","bin_target","firmware_target")),
        (Get-Prop -Object $status -Names @("update_json","update_json_path","json_target")),
        (Get-Prop -Object $status -Names @("versions_json","versions_json_path"))
    )

    $relFiles = @()
    foreach ($p in $paths) {
        if ([string]::IsNullOrWhiteSpace($p)) { continue }

        $rel = Get-RelativePathInsideRepo -RepoRootPath $RepoRoot -FilePath ([string]$p)
        if (-not $rel) { continue }

        if ($relFiles -notcontains $rel) {
            $relFiles += $rel
        }
    }

    if ($relFiles.Count -eq 0) { continue }

    # Nur aufnehmen, wenn es fuer diese Dateien wirklich Git-Aenderungen gibt.
    $statusArgs = @("-C", $RepoRoot, "status", "--porcelain", "--") + $relFiles
    $gitStatus = & git @statusArgs

    if ($LASTEXITCODE -ne 0) {
        Write-Host ("WARNUNG: Git-Status konnte fuer {0} nicht gelesen werden." -f $projectName)
        continue
    }

    if (-not $gitStatus) { continue }

    $publishItems += [pscustomobject]@{
        Name       = $projectName
        Version    = $version
        Sha256     = $sha256
        TargetMode = $targetMode
        Files      = $relFiles
        GitStatus  = @($gitStatus)
    }
}

if ($publishItems.Count -eq 0) {
    Write-Host "Keine neuen Release-Aenderungen zum Committen gefunden."
    exit 0
}

Write-Host "Gefundene Release-Aenderungen:"
foreach ($item in $publishItems) {
    Write-Host ""
    Write-Host ("{0} {1}" -f $item.Name, $item.Version)
    if ($item.TargetMode) { Write-Host (" Zielmodus: {0}" -f $item.TargetMode) }
    foreach ($line in $item.GitStatus) {
        Write-Host (" - {0}" -f $line)
    }
}

$doPublish = $false
if ($PromptMode -eq "on") {
    $doPublish = $true
}
else {
    $doPublish = Read-YesNoWithTimeout -Question "Release-Aenderungen nach Git committen und pushen?" -Default $DefaultAnswer -Timeout $TimeoutSeconds
}

if (-not $doPublish) {
    Write-Host "Git-Veroeffentlichung wurde uebersprungen."
    exit 0
}

$allFiles = @()
foreach ($item in $publishItems) {
    foreach ($f in $item.Files) {
        if ($allFiles -notcontains $f) { $allFiles += $f }
    }
}

$addArgs = @("-C", $RepoRoot, "add", "--") + $allFiles
$ok = Invoke-GitChecked -Arguments $addArgs -ErrorText "git add fehlgeschlagen."
if (-not $ok) {
    Write-Host "Firmware-Build bleibt gueltig; nur Git-Veroeffentlichung ist fehlgeschlagen."
    Finish-Script -Code 1
}

& git -C $RepoRoot diff --cached --quiet
if ($LASTEXITCODE -eq 0) {
    Write-Host "Keine Aenderungen zum Committen gefunden."
    exit 0
}

if ($publishItems.Count -eq 1) {
    $subject = "{0}: Firmware {1}" -f $publishItems[0].Name, $publishItems[0].Version
}
else {
    $subject = "Firmware-Releases aktualisiert"
}

$bodyLines = @()
$bodyLines += "Automatisch nach erfolgreichem Arduino-CLI-Build veroeffentlicht."
$bodyLines += ""

foreach ($item in $publishItems) {
    $bodyLines += ("{0}: {1}" -f $item.Name, $item.Version)
    if ($item.TargetMode) { $bodyLines += ("Zielmodus: {0}" -f $item.TargetMode) }
    foreach ($f in $item.Files) {
        $bodyLines += ("- {0}" -f $f)
    }
    if ($item.Sha256) { $bodyLines += ("SHA256: {0}" -f $item.Sha256) }
    $bodyLines += ""
}

$body = ($bodyLines -join "`n")

Write-Host ""
Write-Host ("Commit: {0}" -f $subject)

$commitOk = Invoke-GitChecked -Arguments @("-C", $RepoRoot, "commit", "-m", $subject, "-m", $body) -ErrorText "git commit fehlgeschlagen."
if (-not $commitOk) {
    Write-Host "Firmware-Build bleibt gueltig; nur Git-Veroeffentlichung ist fehlgeschlagen."
    Finish-Script -Code 1
}

Write-Host ""
Write-Host "Push nach origin..."

$pushOk = Invoke-GitChecked -Arguments @("-C", $RepoRoot, "push") -ErrorText "git push fehlgeschlagen."
if (-not $pushOk) {
    Write-Host "Firmware-Build bleibt gueltig; Commit wurde lokal erstellt, Push ist fehlgeschlagen."
    Finish-Script -Code 1
}

Write-Host ""
Write-Host "Git-Veroeffentlichung abgeschlossen."
exit 0
