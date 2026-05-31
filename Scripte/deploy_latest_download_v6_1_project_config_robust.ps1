param(
    [string]$DownloadsPath = "$env:USERPROFILE\Downloads",
    [int]$MaxAgeMinutes = 60,

    # Zentrale Projektliste. Neue Projekte werden dort eingetragen, nicht mehr im Skript.
    [string]$ProjectConfigPath = "C:\GitHub\releases\Scripte\build_projects.json",

    # Optionaler Build nach dem Kopieren/Entpacken:
    # off = keine Kompilierung, ask = pro erkanntem Projekt nachfragen, on = automatisch starten
    [ValidateSet("off", "ask", "on")]
    [string]$BuildMode = "on",

    # Bevorzugte Auswahl fuer Ja/Nein-Rueckfragen im BuildMode ask.
    # yes = nach Timeout automatisch Ja, no = nach Timeout automatisch Nein
    [ValidateSet("yes", "no")]
    [string]$BuildDefault = "yes",

    # Timeout fuer Ja/Nein-Rueckfragen. 0 = ohne Timeout, klassisches Read-Host.
    [int]$PromptTimeoutSeconds = 60,

    # Wenn gesetzt, laeuft das Skript trotz fehlgeschlagener Kompilierung weiter und endet nicht mit Fehlercode 1.
    [switch]$ContinueOnBuildError
)

$ErrorActionPreference = "Stop"

function Get-ProjectTargetDirFromConfig {
    param($Project)

    if ($Project.target_dir) { return [string]$Project.target_dir }
    if ($Project.target) { return [string]$Project.target }
    if ($Project.sketch_dir) { return [string]$Project.sketch_dir }
    if ($Project.sketch) { return [string]$Project.sketch }

    if (-not $Project.root) {
        throw "Projekt '$($Project.name)' hat weder target_dir/sketch_dir noch root."
    }

    $keyBase = ([string]$Project.key).TrimEnd("_")
    if ([string]::IsNullOrWhiteSpace($keyBase)) {
        throw "Projekt '$($Project.name)' hat keinen gueltigen key."
    }

    return (Join-Path -Path ([string]$Project.root) -ChildPath ($keyBase + "_main"))
}

function Load-ProjectConfig {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Projektliste nicht gefunden: $Path"
    }

    try {
        $config = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
    }
    catch {
        throw "Projektliste konnte nicht gelesen werden: $Path | $($_.Exception.Message)"
    }

    if (-not $config.projects) {
        throw "Projektliste enthaelt kein Feld 'projects': $Path"
    }

    $items = @()
    foreach ($p in @($config.projects)) {
        if (-not $p.key) { throw "Ein Projekt in build_projects.json hat keinen key." }
        if (-not $p.name) { throw "Projekt mit key '$($p.key)' hat keinen name." }

        $key = [string]$p.key
        $targetDir = Get-ProjectTargetDirFromConfig -Project $p

        $items += [pscustomobject]@{
            Key             = $key
            Name            = [string]$p.name
            Root            = [string]$p.root
            TargetDir       = [string]$targetDir
            BuildScript     = [string]$p.build_script
            StatusFile      = [string]$p.status_file
            AfterStatusFile = [string]$p.after_status_file
        }
    }

    if ($items.Count -le 0) {
        throw "Projektliste enthaelt keine Projekte: $Path"
    }

    return @($items)
}

$Projects = @(Load-ProjectConfig -Path $ProjectConfigPath)
$ProjectsByKey = [ordered]@{}
$Targets = [ordered]@{}

foreach ($project in $Projects) {
    if ($ProjectsByKey.Contains($project.Key)) {
        throw "Doppelter Projekt-key in build_projects.json: $($project.Key)"
    }
    $ProjectsByKey[$project.Key] = $project
    $Targets[$project.Key] = $project.TargetDir
}

$KnownPrefixesText = (($Projects | ForEach-Object { $_.Key }) -join "/")

Write-Host ("Projektliste: {0}" -f $ProjectConfigPath)
Write-Host ("Geladene Projekte: {0}" -f (($Projects | ForEach-Object { $_.Key + "=" + $_.Name }) -join ", "))

function Get-PrefixFromName {
    param([string]$Name)

    foreach ($prefix in $Targets.Keys) {
        if ($Name.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $prefix
        }
    }
    return $null
}

function Remove-IfExists {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
    }
}

function Get-FileHashSafe {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) { return $null }

    try {
        return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash
    }
    catch {
        return $null
    }
}

function Get-ShortHash {
    param([string]$Hash)

    if ([string]::IsNullOrWhiteSpace($Hash)) { return "-" }
    if ($Hash.Length -le 12) { return $Hash }
    return $Hash.Substring(0, 12)
}

function Read-YesNoTimed {
    param(
        [string]$Question,
        [ValidateSet("yes", "no")]
        [string]$Default = "yes",
        [int]$TimeoutSeconds = 60
    )

    $defaultIsYes = ($Default -ieq "yes")
    $suffix = if ($defaultIsYes) { "[J/n]" } else { "[j/N]" }

    if ($TimeoutSeconds -le 0 -or -not [Environment]::UserInteractive) {
        $answer = Read-Host ("{0} {1}" -f $Question, $suffix)
        if ([string]::IsNullOrWhiteSpace($answer)) { return $defaultIsYes }
        return ($answer -match "^(j|ja|y|yes)$")
    }

    Write-Host ""
    Write-Host ("{0} {1}" -f $Question, $suffix)
    Write-Host ("Standard nach {0} Sekunden: {1}" -f $TimeoutSeconds, ($(if ($defaultIsYes) { "Ja" } else { "Nein" })))
    Write-Host "Taste J oder N druecken, Enter nimmt den Standard."

    $start = Get-Date
    $lastRemaining = -1

    while ($true) {
        $elapsed = [int]((Get-Date) - $start).TotalSeconds
        $remaining = [Math]::Max(0, $TimeoutSeconds - $elapsed)

        if ($remaining -ne $lastRemaining) {
            Write-Host -NoNewline ("`rNoch {0,2} Sekunden... " -f $remaining)
            $lastRemaining = $remaining
        }

        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            Write-Host ""

            if ($key.Key -eq [ConsoleKey]::Enter) {
                Write-Host ("Auswahl: {0} (Standard)" -f ($(if ($defaultIsYes) { "Ja" } else { "Nein" })))
                return $defaultIsYes
            }

            $char = $key.KeyChar.ToString().ToLowerInvariant()
            if ($char -eq "j" -or $char -eq "y") {
                Write-Host "Auswahl: Ja"
                return $true
            }
            if ($char -eq "n") {
                Write-Host "Auswahl: Nein"
                return $false
            }

            Write-Host "Ungueltige Taste. Bitte J oder N druecken."
        }

        if ($remaining -le 0) {
            Write-Host ""
            Write-Host ("Timeout: {0} gewaehlt." -f ($(if ($defaultIsYes) { "Ja" } else { "Nein" })))
            return $defaultIsYes
        }

        Start-Sleep -Milliseconds 200
    }
}

function Read-JsonFileSafe {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    try {
        return Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    }
    catch {
        return $null
    }
}

function Get-ObjectPropertyValue {
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

function Resolve-StatusBool {
    param(
        $Object,
        [string[]]$Names
    )

    if ($null -eq $Object) { return $null }

    $foundFalse = $false

    foreach ($name in $Names) {
        if ($Object.PSObject.Properties.Name -contains $name) {
            $value = $Object.$name
            if ($null -eq $value) { continue }

            try {
                $boolValue = [System.Convert]::ToBoolean($value)
                if ($boolValue) { return $true }
                $foundFalse = $true
            }
            catch {
                $text = ([string]$value).Trim().ToLowerInvariant()
                if ($text -in @("ja", "yes", "true", "1", "ok", "success", "successful", "erfolgreich")) { return $true }
                if ($text -in @("nein", "no", "false", "0", "fail", "failed", "fehler")) { $foundFalse = $true }
            }
        }
    }

    if ($foundFalse) { return $false }
    return $null
}

function Get-ProjectStatusSummary {
    param($Project)

    $buildStatusPath = $Project.StatusFile
    $afterStatusPath = $Project.AfterStatusFile

    $build = $null
    $after = $null

    if ($buildStatusPath) { $build = Read-JsonFileSafe -Path $buildStatusPath }
    if ($afterStatusPath) { $after = Read-JsonFileSafe -Path $afterStatusPath }

    if (-not $build -and -not $after) { return $null }

    $afterBuildOk = Resolve-StatusBool -Object $after -Names @(
        "after_build_ok",
        "after_ok",
        "post_build_ok",
        "deploy_ok"
    )

    $buildOk = Resolve-StatusBool -Object $build -Names @(
        "build_ok",
        "compile_ok",
        "compilation_ok",
        "kompilierung_ok",
        "success"
    )

    # Robuste Logik:
    # Wenn after_build erfolgreich war, muss die Kompilierung vorher erfolgreich gewesen sein.
    # Dadurch korrigieren wir alte/stale Build-Statusdateien.
    if (($buildOk -ne $true) -and ($afterBuildOk -eq $true)) {
        $buildOk = $true
    }

    $binCopied = Resolve-StatusBool -Object $after -Names @(
        "bin_copied",
        "firmware_copied",
        "copied",
        "bin_ok"
    )

    $updateJsonOk = Resolve-StatusBool -Object $after -Names @(
        "update_json_written",
        "update_json_created",
        "update_json_ok",
        "json_written",
        "json_created"
    )

    $versionsJsonOk = Resolve-StatusBool -Object $after -Names @(
        "versions_json_written",
        "versions_json_created",
        "versions_json_ok"
    )

    $version = Get-ObjectPropertyValue -Object $after -Names @("version", "fw_version", "ui_version")
    if (-not $version) { $version = Get-ObjectPropertyValue -Object $build -Names @("version", "fw_version", "ui_version") }
    if (-not $version) { $version = "unbekannt" }

    return [pscustomobject]@{
        Key             = $Project.Key
        Name            = $Project.Name
        BuildStatusPath = $buildStatusPath
        AfterStatusPath = $afterStatusPath
        Version         = $version
        BuildOk         = $buildOk
        AfterBuildOk    = $afterBuildOk
        BinCopied       = $binCopied
        UpdateJsonOk    = $updateJsonOk
        VersionsJsonOk  = $versionsJsonOk
        TargetMode      = $(Get-ObjectPropertyValue -Object $after -Names @("target_mode", "mode", "zielmodus"))
        UpdateDir       = $(Get-ObjectPropertyValue -Object $after -Names @("update_dir", "release_dir", "target_dir"))
        TargetBin       = $(Get-ObjectPropertyValue -Object $after -Names @("target_bin", "bin_target", "firmware_target"))
        UpdateJson      = $(Get-ObjectPropertyValue -Object $after -Names @("update_json", "update_json_path", "json_target"))
        VersionsJson    = $(Get-ObjectPropertyValue -Object $after -Names @("versions_json", "versions_json_path"))
        Sha256          = $(Get-ObjectPropertyValue -Object $after -Names @("sha256", "sha", "hash"))
        Message         = $(if ($after -and $after.message) { $after.message } elseif ($build -and $build.message) { $build.message } else { $null })
    }
}

function Format-YesNoUnknown {

    param($Value)

    if ($null -eq $Value) { return "unbekannt" }
    if ([bool]$Value) { return "ja" }
    return "nein"
}

function Test-SourceContainsMatch {
    param([System.IO.FileSystemInfo]$Item)

    if (-not $Item) { return $false }

    if ($Item.PSIsContainer) {
        $match = Get-ChildItem -LiteralPath $Item.FullName -Recurse -Force -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -notlike "xxx_*" -and (Get-PrefixFromName -Name $_.Name) } |
            Select-Object -First 1
        return ($null -ne $match)
    }

    if ($Item.Extension -ieq ".zip") {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = $null
        try {
            $zip = [System.IO.Compression.ZipFile]::OpenRead($Item.FullName)
            foreach ($entry in $zip.Entries) {
                if ([string]::IsNullOrWhiteSpace($entry.Name)) { continue }
                if ($entry.Name -like "xxx_*") { continue }
                if (Get-PrefixFromName -Name $entry.Name) { return $true }
            }
            return $false
        }
        finally {
            if ($zip) { $zip.Dispose() }
        }
    }

    return ($null -ne (Get-PrefixFromName -Name $Item.Name))
}

function Get-LatestEligibleSource {
    param(
        [string]$Path,
        [int]$MaxAgeMinutes
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Downloads-Pfad nicht gefunden: $Path"
    }

    $cutoff = (Get-Date).AddMinutes(-1 * $MaxAgeMinutes)

    $candidates = Get-ChildItem -LiteralPath $Path -Force |
        Where-Object {
            $_.Name -notlike "xxx_*" -and
            $_.LastWriteTime -ge $cutoff
        } |
        Sort-Object LastWriteTime -Descending

    if (-not $candidates) {
        throw "Im Downloads-Ordner wurde kein Eintrag gefunden, der juenger als $MaxAgeMinutes Minuten ist."
    }

    Write-Host ""
    Write-Host "Pruefe aktuelle Downloads auf passende Inhalte..."

    foreach ($candidate in $candidates) {
        $typeText = if ($candidate.PSIsContainer) { "Ordner" } elseif ($candidate.Extension -ieq ".zip") { "ZIP" } else { "Datei" }
        Write-Host (" - Kandidat: {0} | Typ: {1} | Zeit: {2}" -f $candidate.FullName, $typeText, $candidate.LastWriteTime)

        if (Test-SourceContainsMatch -Item $candidate) {
            Write-Host "   => passend"
            return $candidate
        }
        else {
            Write-Host "   => ignoriert (kein passender Projekt-Prefix aus build_projects.json)"
        }
    }

    throw "Es wurde kein passender Download innerhalb der letzten $MaxAgeMinutes Minuten gefunden."
}

function Rename-ProcessedSource {
    param([System.IO.FileSystemInfo]$Item)

    if (-not (Test-Path -LiteralPath $Item.FullName)) {
        throw "Quelle zum Umbenennen nicht gefunden: $($Item.FullName)"
    }

    $parent = [System.IO.Path]::GetDirectoryName($Item.FullName)
    $newName = "xxx_" + $Item.Name
    $newPath = Join-Path -Path $parent -ChildPath $newName

    Write-Host ""
    Write-Host "Markiere Quelle als verarbeitet:"
    Write-Host ("  Alt : {0}" -f $Item.FullName)
    Write-Host ("  Neu : {0}" -f $newPath)

    Remove-IfExists -Path $newPath
    Move-Item -LiteralPath $Item.FullName -Destination $newPath -Force -ErrorAction Stop

    if (Test-Path -LiteralPath $Item.FullName) {
        throw "Umbenennen fehlgeschlagen: alte Quelle existiert noch: $($Item.FullName)"
    }

    if (-not (Test-Path -LiteralPath $newPath)) {
        throw "Umbenennen fehlgeschlagen: neue Quelle wurde nicht gefunden: $newPath"
    }

    Write-Host "  Ergebnis: umbenannt"
    return $newPath
}

function New-Stats {
    $perPrefix = @{}
    foreach ($prefix in $Targets.Keys) {
        $perPrefix[$prefix] = [ordered]@{
            Found       = 0
            Copied      = 0
            Overwritten = 0
        }
    }

    return [ordered]@{
        TotalFound       = 0
        TotalCopied      = 0
        TotalOverwritten = 0
        PerPrefix        = $perPrefix
        Details          = New-Object System.Collections.Generic.List[object]
    }
}

function Add-Stat {
    param(
        $Stats,
        [string]$Prefix,
        [string]$SourceName,
        [string]$SourceDisplay,
        [string]$DestinationPath,
        [bool]$WasOverwrite,
        [string]$BeforeHash,
        [string]$SourceHash,
        [string]$DestinationHash
    )

    $Stats.TotalFound++
    $Stats.TotalCopied++
    if ($WasOverwrite) { $Stats.TotalOverwritten++ }

    $Stats.PerPrefix[$Prefix].Found++
    $Stats.PerPrefix[$Prefix].Copied++
    if ($WasOverwrite) { $Stats.PerPrefix[$Prefix].Overwritten++ }

    $Stats.Details.Add([pscustomobject]@{
        Prefix         = $Prefix
        Datei          = $SourceName
        Quelle         = $SourceDisplay
        Ziel           = $DestinationPath
        UeberSchrieben = $WasOverwrite
        VorHash        = $BeforeHash
        SourceHash     = $SourceHash
        ZielHash       = $DestinationHash
    }) | Out-Null
}

function Copy-MatchedFile {
    param(
        [string]$SourcePath,
        [string]$FileName,
        [string]$SourceDisplay,
        $Stats
    )

    $prefix = Get-PrefixFromName -Name $FileName
    if (-not $prefix) { return }

    $targetDir = $Targets[$prefix]
    if (-not (Test-Path -LiteralPath $targetDir)) {
        throw "Zielpfad existiert nicht: $targetDir"
    }

    if (-not (Test-Path -LiteralPath $SourcePath)) {
        throw "Quelldatei existiert nicht: $SourcePath"
    }

    $destPath = Join-Path -Path $targetDir -ChildPath $FileName
    $wasOverwrite = Test-Path -LiteralPath $destPath

    $srcInfo = Get-Item -LiteralPath $SourcePath -ErrorAction Stop
    $srcHash = Get-FileHashSafe -Path $SourcePath
    $beforeHash = Get-FileHashSafe -Path $destPath

    Write-Host ""
    Write-Host ("Pruefe/Kopiere: {0}" -f $FileName)
    Write-Host ("  Quelle: {0}" -f $SourceDisplay)
    Write-Host ("  Ziel  : {0}" -f $destPath)
    Write-Host ("  Hash alt : {0}" -f (Get-ShortHash -Hash $beforeHash))
    Write-Host ("  Hash src : {0}" -f (Get-ShortHash -Hash $srcHash))

    [System.IO.File]::Copy($SourcePath, $destPath, $true)

    if (-not (Test-Path -LiteralPath $destPath)) {
        throw "Nach dem Kopieren existiert die Zieldatei nicht: $destPath"
    }

    $dstInfo = Get-Item -LiteralPath $destPath -ErrorAction Stop
    $afterHash = Get-FileHashSafe -Path $destPath

    if ($srcInfo.Length -ne $dstInfo.Length) {
        throw "Kopieren fehlgeschlagen: Dateigroesse stimmt nicht ueberein ($($srcInfo.Length) -> $($dstInfo.Length)) bei $destPath"
    }

    if ($srcHash -and $afterHash -and ($srcHash -ne $afterHash)) {
        throw "Kopieren fehlgeschlagen: Dateihash stimmt nicht ueberein bei $destPath"
    }

    $overwriteText = if ($wasOverwrite) { "ja" } else { "nein" }
    $changedText = if ($beforeHash -and $afterHash -and ($beforeHash -eq $afterHash)) { "unveraendert" } else { "geaendert/neu" }

    Write-Host ("  Hash neu : {0}" -f (Get-ShortHash -Hash $afterHash))
    Write-Host ("  Ergebnis: kopiert | ueberschrieben: {0} | Zustand: {1}" -f $overwriteText, $changedText)

    Add-Stat -Stats $Stats -Prefix $prefix -SourceName $FileName -SourceDisplay $SourceDisplay -DestinationPath $destPath -WasOverwrite $wasOverwrite -BeforeHash $beforeHash -SourceHash $srcHash -DestinationHash $afterHash
}

function Process-Folder {
    param(
        [string]$FolderPath,
        $Stats
    )

    Get-ChildItem -LiteralPath $FolderPath -Recurse -Force |
        Where-Object {
            -not $_.PSIsContainer -and
            $_.Name -notlike "xxx_*" -and
            (Get-PrefixFromName -Name $_.Name)
        } |
        ForEach-Object {
            Copy-MatchedFile -SourcePath $_.FullName -FileName $_.Name -SourceDisplay $_.FullName -Stats $Stats
        }
}

function Process-SingleFile {
    param(
        [System.IO.FileInfo]$File,
        $Stats
    )

    $prefix = Get-PrefixFromName -Name $File.Name
    if ($prefix) {
        Copy-MatchedFile -SourcePath $File.FullName -FileName $File.Name -SourceDisplay $File.FullName -Stats $Stats
    }
}

function Process-Zip {
    param(
        [string]$ZipPath,
        $Stats
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem

    $tempRoot = Join-Path -Path $env:TEMP -ChildPath ("deploy_download_" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $tempRoot | Out-Null

    try {
        $zip = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)

        try {
            $entries = @(
                foreach ($entry in $zip.Entries) {
                    if ([string]::IsNullOrWhiteSpace($entry.Name)) { continue }
                    if ($entry.Name -like "xxx_*") { continue }
                    $prefix = Get-PrefixFromName -Name $entry.Name
                    if (-not $prefix) { continue }

                    [pscustomobject]@{
                        Prefix   = $prefix
                        Name     = $entry.Name
                        FullName = $entry.FullName
                        Entry    = $entry
                    }
                }
            )

            $dupes = $entries | Group-Object -Property Name | Where-Object { $_.Count -gt 1 }
            if ($dupes) {
                Write-Host ""
                Write-Host "WARNUNG: Doppelte Dateinamen im ZIP gefunden:"
                foreach ($g in $dupes) {
                    Write-Host (" - {0}" -f $g.Name)
                    foreach ($item in $g.Group) {
                        Write-Host ("    * {0}" -f $item.FullName)
                    }
                }
                Write-Host "Hinweis: Beim flachen Kopieren nach Zielordner gewinnt der zuletzt verarbeitete Dateiname."
            }

            foreach ($item in $entries) {
                $entry = $item.Entry
                $tempFile = Join-Path -Path $tempRoot -ChildPath ([guid]::NewGuid().ToString("N") + "_" + $entry.Name)
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $tempFile, $true)
                $display = $ZipPath + "::" + $entry.FullName
                Copy-MatchedFile -SourcePath $tempFile -FileName $entry.Name -SourceDisplay $display -Stats $Stats
            }
        }
        finally {
            if ($zip) { $zip.Dispose() }
        }
    }
    finally {
        Remove-IfExists -Path $tempRoot
    }
}


function Get-CopiedPrefixes {
    param($Stats)

    $prefixes = New-Object System.Collections.Generic.List[string]

    foreach ($prefix in $Targets.Keys) {
        if ($Stats.PerPrefix[$prefix].Copied -gt 0) {
            $prefixes.Add($prefix) | Out-Null
        }
    }

    return @($prefixes)
}

function Test-UserWantsBuild {
    param($Project)

    if ($BuildMode -ieq "off") { return $false }
    if ($BuildMode -ieq "on") { return $true }

    return (Read-YesNoTimed -Question ("Kompilierung fuer {0} ({1}) starten?" -f $Project.Name, $Project.Key) -Default $BuildDefault -TimeoutSeconds $PromptTimeoutSeconds)
}

function Invoke-BuildScript {
    param($Project)

    $scriptPath = $Project.BuildScript

    if ([string]::IsNullOrWhiteSpace($scriptPath)) {
        Write-Host ("  Uebersprungen: Kein Build-Skript fuer {0} ({1}) hinterlegt." -f $Project.Name, $Project.Key) -ForegroundColor Yellow
        return [pscustomobject]@{
            Prefix = $Project.Key
            Name = $Project.Name
            Script = $null
            Status = "kein Build-Skript"
            ExitCode = $null
        }
    }

    if (-not (Test-Path -LiteralPath $scriptPath)) {
        Write-Host ("  Uebersprungen: Build-Skript nicht gefunden: {0}" -f $scriptPath) -ForegroundColor Yellow
        return [pscustomobject]@{
            Prefix = $Project.Key
            Name = $Project.Name
            Script = $scriptPath
            Status = "nicht gefunden"
            ExitCode = $null
        }
    }

    $scriptDir = Split-Path -Path $scriptPath -Parent

    Write-Host ""
    Write-Host "========================================"
    Write-Host ("Starte Kompilierung fuer {0} ({1})" -f $Project.Name, $Project.Key)
    Write-Host "========================================"
    Write-Host ("Build-Skript: {0}" -f $scriptPath)
    Write-Host ""

    $oldLocation = Get-Location
    try {
        Set-Location -LiteralPath $scriptDir
        & $scriptPath
        $exitCode = $LASTEXITCODE
    }
    finally {
        Set-Location -LiteralPath $oldLocation
    }

    if ($null -eq $exitCode) { $exitCode = 0 }

    if ($exitCode -ne 0) {
        Write-Host ("Kompilierung fuer {0} fehlgeschlagen. ExitCode: {1}" -f $Project.Name, $exitCode) -ForegroundColor Red
        return [pscustomobject]@{
            Prefix = $Project.Key
            Name = $Project.Name
            Script = $scriptPath
            Status = "fehlgeschlagen"
            ExitCode = $exitCode
        }
    }

    Write-Host ""
    Write-Host ("Kompilierung fuer {0} erfolgreich abgeschlossen." -f $Project.Name) -ForegroundColor Green

    return [pscustomobject]@{
        Prefix = $Project.Key
        Name = $Project.Name
        Script = $scriptPath
        Status = "erfolgreich"
        ExitCode = 0
    }
}

function Invoke-OptionalBuilds {
    param(
        $Stats,
        [switch]$ContinueOnBuildError
    )

    $prefixes = @(Get-CopiedPrefixes -Stats $Stats)

    Write-Host ""
    Write-Host "========================================"
    Write-Host "Optionale Kompilierung"
    Write-Host "========================================"
    Write-Host ("Build-Modus: {0}" -f $BuildMode)

    if ($BuildMode -ieq "off") {
        Write-Host "Kompilierung ist deaktiviert."
        return @()
    }

    if ($prefixes.Count -le 0) {
        Write-Host "Keine kopierten Projektdateien gefunden, keine Kompilierung notwendig."
        return @()
    }

    $results = @()

    foreach ($prefix in $prefixes) {
        if (-not $ProjectsByKey.Contains($prefix)) {
            Write-Host ("  Uebersprungen: Kein Projekt fuer Prefix {0} in build_projects.json hinterlegt." -f $prefix) -ForegroundColor Yellow
            $results += [pscustomobject]@{ Prefix = $prefix; Name = $prefix; Script = $null; Status = "kein Projekt"; ExitCode = $null }
            continue
        }

        $project = $ProjectsByKey[$prefix]

        if (-not (Test-UserWantsBuild -Project $project)) {
            Write-Host ("  Uebersprungen: Kompilierung fuer {0} nicht gestartet." -f $project.Name)
            $results += [pscustomobject]@{ Prefix = $prefix; Name = $project.Name; Script = $project.BuildScript; Status = "uebersprungen"; ExitCode = $null }
            continue
        }

        $result = Invoke-BuildScript -Project $project
        $results += $result

        if (($result.Status -eq "fehlgeschlagen") -and (-not $ContinueOnBuildError)) {
            throw ("Kompilierung fuer {0} fehlgeschlagen. Mit -ContinueOnBuildError kann das Skript trotz Build-Fehler weiterlaufen." -f $project.Name)
        }
    }

    if ($results.Count -gt 0) {
        Write-Host ""
        Write-Host "Build-Ergebnis:"
        foreach ($r in $results) {
            Write-Host (" - {0} ({1}) | {2} | ExitCode: {3} | {4}" -f $r.Name, $r.Prefix, $r.Status, $r.ExitCode, $r.Script)
        }
    }

    return $results
}

function Show-Summary {
    param(
        [System.IO.FileSystemInfo]$SourceItem,
        [string]$RenamedPath,
        $Stats
    )

    Write-Host ""
    Write-Host "========================================"
    Write-Host "Verarbeitung abgeschlossen"
    Write-Host "========================================"
    Write-Host ("Quelle        : {0}" -f $SourceItem.FullName)
    Write-Host ("Typ           : {0}" -f ($(if ($SourceItem.PSIsContainer) { "Ordner" } else { "Datei" })))
    Write-Host ("Markiert nach : {0}" -f $RenamedPath)
    Write-Host ("Max. Alter    : {0} Minuten" -f $MaxAgeMinutes)
    Write-Host ""
    Write-Host ("Gefundene passende Dateien : {0}" -f $Stats.TotalFound)
    Write-Host ("Kopierte Dateien           : {0}" -f $Stats.TotalCopied)
    Write-Host ("Davon ueberschrieben       : {0}" -f $Stats.TotalOverwritten)
    Write-Host ""

    foreach ($prefix in $Targets.Keys) {
        $s = $Stats.PerPrefix[$prefix]
        Write-Host ("{0}  gefunden: {1} | kopiert: {2} | ueberschrieben: {3}" -f $prefix, $s.Found, $s.Copied, $s.Overwritten)
    }

    if ($Stats.Details.Count -gt 0) {
        Write-Host ""
        Write-Host "Dateien:"
        foreach ($d in $Stats.Details) {
            $flag = if ($d.UeberSchrieben) { " [ueberschrieben]" } else { "" }
            Write-Host (" - {0} <- {1} -> {2}{3}" -f $d.Datei, $d.Quelle, $d.Ziel, $flag)
        }
    } else {
        Write-Host ""
        Write-Host "Es wurden keine passenden Projektdateien aus build_projects.json gefunden."
    }

    Write-Host ""
}

function Show-FinalSummary {
    param(
        $Stats,
        $BuildResults
    )

    Write-Host ""
    Write-Host "========================================"
    Write-Host "Kurze Zusammenfassung"
    Write-Host "========================================"
    Write-Host ("Dateien kopiert        : {0}" -f $Stats.TotalCopied)
    Write-Host ("Dateien ueberschrieben : {0}" -f $Stats.TotalOverwritten)

    if ($BuildResults -and $BuildResults.Count -gt 0) {
        Write-Host ""
        Write-Host "Kompilierung:"
        foreach ($r in $BuildResults) {
            $exitText = if ($null -eq $r.ExitCode) { "-" } else { $r.ExitCode }
            Write-Host (" - {0} ({1}): {2} (ExitCode: {3})" -f $r.Name, $r.Prefix, $r.Status, $exitText)
        }
    }
    else {
        Write-Host "Kompilierung           : nicht gestartet"
    }

    $keysToReport = @()

    foreach ($prefix in $Targets.Keys) {
        if ($Stats.PerPrefix.ContainsKey($prefix) -and $Stats.PerPrefix[$prefix].Copied -gt 0) {
            if ($keysToReport -notcontains $prefix) { $keysToReport += $prefix }
        }
    }

    if ($BuildResults) {
        foreach ($r in $BuildResults) {
            if ($r.Prefix -and ($keysToReport -notcontains $r.Prefix)) { $keysToReport += $r.Prefix }
        }
    }

    foreach ($prefix in $keysToReport) {
        if (-not $ProjectsByKey.Contains($prefix)) { continue }

        $project = $ProjectsByKey[$prefix]
        $s = Get-ProjectStatusSummary -Project $project

        Write-Host ""
        Write-Host ("{0}:" -f $project.Name)
        Write-Host (" - Prefix                 : {0}" -f $project.Key)

        if (-not $s) {
            Write-Host " - Status                 : keine Statusdateien gefunden"
            if ($project.StatusFile)      { Write-Host (" - build status           : {0}" -f $project.StatusFile) }
            if ($project.AfterStatusFile) { Write-Host (" - after_build status     : {0}" -f $project.AfterStatusFile) }
            continue
        }

        Write-Host (" - Version kompiliert     : {0}" -f $s.Version)
        Write-Host (" - Build erfolgreich      : {0}" -f (Format-YesNoUnknown -Value $s.BuildOk))
        Write-Host (" - after_build OK         : {0}" -f (Format-YesNoUnknown -Value $s.AfterBuildOk))
        Write-Host (" - BIN kopiert            : {0}" -f (Format-YesNoUnknown -Value $s.BinCopied))
        Write-Host (" - update.json erstellt   : {0}" -f (Format-YesNoUnknown -Value $s.UpdateJsonOk))
        if ($null -ne $s.VersionsJsonOk) { Write-Host (" - versions.json aktual.  : {0}" -f (Format-YesNoUnknown -Value $s.VersionsJsonOk)) }
        if ($s.TargetMode)   { Write-Host (" - Zielmodus              : {0}" -f $s.TargetMode) }
        if ($s.UpdateDir)    { Write-Host (" - Zielordner             : {0}" -f $s.UpdateDir) }
        if ($s.TargetBin)    { Write-Host (" - BIN Ziel               : {0}" -f $s.TargetBin) }
        if ($s.UpdateJson)   { Write-Host (" - update.json            : {0}" -f $s.UpdateJson) }
        if ($s.VersionsJson) { Write-Host (" - versions.json          : {0}" -f $s.VersionsJson) }
        if ($s.Sha256)       { Write-Host (" - SHA256                 : {0}" -f $s.Sha256) }
        if ($s.Message)      { Write-Host (" - Meldung                : {0}" -f $s.Message) }
    }
}

try {
    $source = Get-LatestEligibleSource -Path $DownloadsPath -MaxAgeMinutes $MaxAgeMinutes
    $stats = New-Stats

    Write-Host ""
    Write-Host ("Ausgewaehlter Eintrag: {0}" -f $source.FullName)

    if ($source.PSIsContainer) {
        Process-Folder -FolderPath $source.FullName -Stats $stats
    }
    elseif ($source.Extension -ieq ".zip") {
        Process-Zip -ZipPath $source.FullName -Stats $stats
    }
    else {
        Process-SingleFile -File $source -Stats $stats
    }

    if ($stats.TotalCopied -le 0) {
        throw "Es wurde zwar ein passender Eintrag ausgewaehlt, aber keine Datei kopiert. Bitte Ausgabe pruefen."
    }

    $renamed = Rename-ProcessedSource -Item $source
    Show-Summary -SourceItem $source -RenamedPath $renamed -Stats $stats
    $buildResults = @(Invoke-OptionalBuilds -Stats $stats -ContinueOnBuildError:$ContinueOnBuildError)
    Show-FinalSummary -Stats $stats -BuildResults $buildResults
}
catch {
    Write-Host ""
    Write-Host ("FEHLER: {0}" -f $_.Exception.Message) -ForegroundColor Red
    if ($_.InvocationInfo -and $_.InvocationInfo.ScriptLineNumber) {
        Write-Host ("Zeile : {0}" -f $_.InvocationInfo.ScriptLineNumber) -ForegroundColor Yellow
        Write-Host ("Code  : {0}" -f $_.InvocationInfo.Line.Trim()) -ForegroundColor Yellow
    }
    Write-Host ""
    exit 1
}
