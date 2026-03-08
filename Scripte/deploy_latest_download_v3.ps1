param(
    [string]$DownloadsPath = "$env:USERPROFILE\Downloads",
    [int]$MaxAgeMinutes = 60
)

$ErrorActionPreference = "Stop"

$Targets = [ordered]@{
    "wc_" = "C:\GitHub\word-clock-IV\wc_main"
    "cc_" = "C:\GitHub\million-times\cc_main"
    "eq_" = "C:\GitHub\equinox-clock\eq_main"
    "mc_" = "C:\GitHub\moon-clock\mc_main"
}

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
            Write-Host "   => ignoriert (kein wc_/cc_/eq_/mc_ Inhalt)"
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
        Write-Host "Es wurden keine passenden wc_/cc_/eq_/mc_-Dateien gefunden."
    }

    Write-Host ""
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
