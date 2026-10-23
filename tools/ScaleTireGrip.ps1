# ScaleTireGrip.ps1: multiplies the ground and mud grip of SnowRunner tires in a folder of XML files. Asphalt grip stays.
# It edits BodyFriction (ground) and SubstanceFriction (mud) in every classes\wheels\*.xml under the folder, base game
# and _dlc alike, and in the WheelFriction templates of _templates\trucks.xml, so tires that take their grip from a
# template follow. BodyFrictionAsphalt is never touched. Nothing outside those files is read or written: overlay files
# carry BodyFriction too, as the grip of a surface, and stay as they are.
# Each edited file gets a marker comment with the factor, and its original goes to a folder beside the chosen one
# (<folder>_grip_backup, same layout). A second run skips files already at the factor and scales new ones (tires a DLC
# added), so it never scales twice. A run with another factor starts again from the originals.
#   powershell -ExecutionPolicy Bypass -File ScaleTireGrip.ps1 -Path <folder> [-Factor 0.55] [-DryRun] [-Undo]
# -DryRun lists every change and writes nothing. -Undo puts the originals back from the backup folder.
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [double]$Factor = 0.55,
    [switch]$DryRun,
    [switch]$Undo
)
$ErrorActionPreference = 'Stop'
$inv = [Globalization.CultureInfo]::InvariantCulture
$utf8 = New-Object Text.UTF8Encoding($false)
$markerStart = '<!-- tire grip x'
$markerRx = [regex]'<!-- tire grip x([0-9.]+) [^>]*-->\r?\n?[ \t]*'

if ($Factor -le 0 -or $Factor -gt 10) { throw "The factor must be above 0 and at most 10 (got $Factor)." }
if (-not (Test-Path -LiteralPath $Path -PathType Container)) { throw "No folder at $Path" }
$root = (Resolve-Path -LiteralPath $Path).ProviderPath.TrimEnd('\')
$backupRoot = $root + '_grip_backup'
$factorText = $Factor.ToString('0.####', $inv)

function ReadText([string]$file) {
    $b = [IO.File]::ReadAllBytes($file)
    $bom = $b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF
    $start = 0; if ($bom) { $start = 3 }
    @{ Text = $utf8.GetString($b, $start, $b.Length - $start); Bom = $bom }
}
function WriteText([string]$file, [string]$text, [bool]$bom) {
    $body = $utf8.GetBytes($text)
    if ($bom) {
        $out = New-Object byte[] ($body.Length + 3)
        $out[0] = 0xEF; $out[1] = 0xBB; $out[2] = 0xBF
        [Array]::Copy($body, 0, $out, 3, $body.Length)
        $body = $out
    }
    [IO.File]::WriteAllBytes($file, $body)
}
# Where a match sits, for the list: its line and the tire's Name (a WheelFriction takes its TruckTire's), or the
# template's tag.
function Locate([string]$text, [int]$at) {
    $open = $text.LastIndexOf('<', $at)
    $tagText = $text.Substring($open, $text.IndexOf('>', $at) - $open)
    $tag = [regex]::Match($tagText, '^<(\w+)').Groups[1].Value
    $own = [regex]::Match($tagText, '\bName\s*=\s*"([^"]*)"')
    $line = ([regex]::Matches($text.Substring(0, $at), "`n")).Count + 1
    if ($own.Success) { $name = $own.Groups[1].Value }
    elseif ($tag -eq 'WheelFriction') {
        $names = [regex]::Matches($text.Substring(0, $open), '<TruckTire\b[^>]*?\bName\s*=\s*"([^"]*)"')
        $name = '?'; if ($names.Count) { $name = $names[$names.Count - 1].Groups[1].Value }
    }
    else { $name = $tag }
    'line {0,-5} {1,-28}' -f $line, $name
}
# Scales BodyFriction and SubstanceFriction in text[from, to). Returns the new text and the list of changes.
function Scale([string]$text, [int]$from, [int]$to) {
    $rx = [regex]'\b(BodyFriction|SubstanceFriction)(\s*=\s*")([^"]*)"'
    $changes = New-Object Collections.Generic.List[string]
    $sb = New-Object Text.StringBuilder
    $last = 0
    foreach ($m in $rx.Matches($text)) {
        if ($m.Index -lt $from -or $m.Index -ge $to) { continue }
        $v = 0.0
        if (-not [double]::TryParse($m.Groups[3].Value, [Globalization.NumberStyles]::Float, $inv, [ref]$v)) {
            $changes.Add((Locate $text $m.Index) + ' ' + $m.Groups[1].Value + ' "' + $m.Groups[3].Value + '" is not a number: left as it is')
            continue
        }
        $nv = [Math]::Round($v * $Factor, 4).ToString('0.####', $inv)
        [void]$sb.Append($text, $last, $m.Index - $last)
        [void]$sb.Append($m.Groups[1].Value + $m.Groups[2].Value + $nv + '"')
        $last = $m.Index + $m.Length
        $changes.Add((Locate $text $m.Index) + ' ' + ('{0,-18} {1} -> {2}' -f $m.Groups[1].Value, $m.Groups[3].Value, $nv))
    }
    [void]$sb.Append($text, $last, $text.Length - $last)
    @{ Text = $sb.ToString(); Changes = $changes }
}
# The marker goes inside the first element, where the game's own files keep their comments.
function AddMarker([string]$text) {
    $m = [regex]::Match($text, '<[A-Za-z_][^>]*>')
    if (-not $m.Success) { return $text }
    $nl = "`n"; if ($text.Contains("`r`n")) { $nl = "`r`n" }
    $marker = $markerStart + $factorText + ' on ground and mud by ScaleTireGrip.ps1, asphalt as it was; the original is in ' + `
        (Split-Path $backupRoot -Leaf) + ' -->'
    $text.Substring(0, $m.Index + $m.Length) + $nl + "`t" + $marker + $text.Substring($m.Index + $m.Length)
}

$files = Get-ChildItem -LiteralPath $root -Recurse -File -Filter *.xml | Where-Object {
    $_.FullName -match '\\classes\\wheels\\[^\\]+\.xml$' -or $_.FullName -match '\\_templates\\trucks\.xml$' } | Sort-Object FullName
if (-not $files) { Write-Host "No tire files under $root (classes\wheels\*.xml or _templates\trucks.xml)."; exit 1 }

$mode = 'Scaling'; if ($Undo) { $mode = 'Putting back' } elseif ($DryRun) { $mode = 'Dry run, nothing is written:' }
Write-Host "$mode $root" $(if (-not $Undo) { "(ground and mud grip x$factorText, asphalt as it was)" })
$done = 0; $values = 0; $skipped = 0
foreach ($f in $files) {
    $rel = $f.FullName.Substring($root.Length + 1)
    $backup = Join-Path $backupRoot $rel
    $cur = ReadText $f.FullName
    $mk = $markerRx.Match($cur.Text)
    if ($Undo) {
        if (-not $mk.Success) { continue }
        if (-not (Test-Path -LiteralPath $backup)) { Write-Host "  $rel : scaled, but no original in the backup folder: left as it is"; continue }
        if (-not $DryRun) { Copy-Item -LiteralPath $backup -Destination $f.FullName -Force }
        Write-Host "  $rel : original $(if ($DryRun) { 'would come back' } else { 'back' })"; $done++
        continue
    }
    if ($mk.Success) {
        if ($mk.Groups[1].Value -eq $factorText) { $skipped++; continue }
        if (-not (Test-Path -LiteralPath $backup)) { Write-Host "  $rel : already x$($mk.Groups[1].Value), and no original to start again from: left as it is"; continue }
        $src = ReadText $backup
    } else {
        $src = $cur
    }
    $from = 0; $to = $src.Text.Length
    if ($f.Name -eq 'trucks.xml') {
        $a = $src.Text.IndexOf('<WheelFriction>'); $b = $src.Text.IndexOf('</WheelFriction>')
        if ($a -lt 0 -or $b -lt $a) { continue }
        $from = $a; $to = $b
    }
    $r = Scale $src.Text $from $to
    if ($r.Changes.Count -eq 0) { continue }
    Write-Host "  $rel"
    foreach ($c in $r.Changes) { Write-Host "    $c" }
    $done++; $values += $r.Changes.Count
    if ($DryRun) { continue }
    if (-not $mk.Success) {
        New-Item -ItemType Directory -Force -Path (Split-Path $backup) | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination $backup -Force
    }
    WriteText $f.FullName (AddMarker $r.Text) $src.Bom
}
if ($Undo) { Write-Host "$done file(s) $(if ($DryRun) { 'would be put back' } else { 'put back' })."; exit 0 }
$verb = 'scaled'; if ($DryRun) { $verb = 'would be scaled' }
Write-Host "$done file(s) $verb, $values value(s); $skipped file(s) already at x$factorText."
if (-not $DryRun -and $done) { Write-Host "Originals: $backupRoot" }
