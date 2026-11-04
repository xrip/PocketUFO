param([switch]$Tactical)

$ErrorActionPreference = 'Stop'

$nativeCode = @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class WindowSmokeNative
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int left;
        public int top;
        public int right;
        public int bottom;
    }

    private delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll", EntryPoint = "FindWindowW", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string className, string windowName);

    [DllImport("user32.dll", EntryPoint = "EnumWindows")]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll", EntryPoint = "GetWindowThreadProcessId")]
    private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);

    [DllImport("user32.dll", EntryPoint = "GetClassNameW", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(IntPtr hwnd, StringBuilder className, int maxCount);

    [DllImport("user32.dll", EntryPoint = "GetWindowTextW", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(IntPtr hwnd, StringBuilder windowName, int maxCount);

    [DllImport("user32.dll", EntryPoint = "GetClientRect")]
    public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);

    [DllImport("user32.dll", EntryPoint = "ShowWindow")]
    public static extern bool ShowWindow(IntPtr hwnd, int command);

    [DllImport("user32.dll", EntryPoint = "PostMessageW")]
    private static extern bool PostMessage(IntPtr hwnd, uint message, UIntPtr wParam, IntPtr lParam);

    public static IntPtr FindWindowForProcess(uint targetPid, string targetClass, string targetTitle)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr hwnd, IntPtr unused)
        {
            uint ownerPid;
            GetWindowThreadProcessId(hwnd, out ownerPid);
            if (ownerPid != targetPid)
            {
                return true;
            }

            StringBuilder className = new StringBuilder(128);
            StringBuilder windowName = new StringBuilder(256);
            GetClassName(hwnd, className, className.Capacity);
            GetWindowText(hwnd, windowName, windowName.Capacity);
            if (String.Equals(className.ToString(), targetClass, StringComparison.Ordinal)
                && String.Equals(windowName.ToString(), targetTitle, StringComparison.Ordinal))
            {
                found = hwnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static bool PostClientMouse(IntPtr hwnd, uint message, uint keyState, int x, int y)
    {
        uint packed = (uint)(ushort)x | ((uint)(ushort)y << 16);
        IntPtr lParam = new IntPtr(unchecked((int)packed));
        return PostMessage(hwnd, message, new UIntPtr(keyState), lParam);
    }

    public static bool PostWindowCloseForProcess(IntPtr hwnd, uint targetPid, string targetClass, string targetTitle)
    {
        if (hwnd == IntPtr.Zero)
        {
            return false;
        }

        uint ownerPid;
        GetWindowThreadProcessId(hwnd, out ownerPid);
        if (ownerPid != targetPid)
        {
            return false;
        }

        StringBuilder className = new StringBuilder(128);
        StringBuilder windowName = new StringBuilder(256);
        GetClassName(hwnd, className, className.Capacity);
        GetWindowText(hwnd, windowName, windowName.Capacity);
        if (!String.Equals(className.ToString(), targetClass, StringComparison.Ordinal)
            || !String.Equals(windowName.ToString(), targetTitle, StringComparison.Ordinal))
        {
            return false;
        }

        return PostMessage(hwnd, 0x0010, UIntPtr.Zero, IntPtr.Zero);
    }
}
'@
Add-Type -TypeDefinition $nativeCode

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildAppDir = Join-Path $repoRoot 'build/src/pufo/Debug'
$smokeRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build/window-smoke'))
$appDir = [IO.Path]::GetFullPath((Join-Path $smokeRoot 'app'))
$workerDir = [IO.Path]::GetFullPath((Join-Path $repoRoot '.workers/luna-window-smoke'))
$nativeFlow = [IO.Path]::GetFullPath((Join-Path $repoRoot 'tools/e2e_flow_native_tact.txt'))
$privateExe = Join-Path $appDir 'pocketufo.exe'
$cdbExe = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
$windowClass = 'POCKET_UFO'
$windowTitle = 'POCKET UFO'
$requiredDlls = @(
    'libopenmpt.dll',
    'openmpt-mpg123.dll',
    'openmpt-vorbis.dll',
    'openmpt-zlib.dll',
    'openmpt-ogg.dll'
)

$rootPrefix = $repoRoot.TrimEnd('\') + '\'
foreach ($path in @($smokeRoot, $appDir, $workerDir)) {
    if (-not $path.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing path outside the workspace: $path"
    }
}
if ($Tactical -and -not $nativeFlow.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing native tactical flow outside the workspace: $nativeFlow"
}
if ($Tactical -and -not (Test-Path -LiteralPath $nativeFlow -PathType Leaf)) {
    throw "Native tactical prep flow is missing: $nativeFlow"
}

if (-not (Test-Path -LiteralPath $cdbExe -PathType Leaf)) {
    throw "CDB not found at $cdbExe"
}

$existingWindow = [WindowSmokeNative]::FindWindow($windowClass, $windowTitle)
if ($existingWindow -ne [IntPtr]::Zero) {
    throw 'A POCKET UFO window already exists. Close it before this private-window probe; no existing window was touched.'
}

foreach ($process in @(Get-Process -Name pocketufo -ErrorAction SilentlyContinue)) {
    try {
        $imagePath = [IO.Path]::GetFullPath($process.MainModule.FileName)
    }
    catch {
        continue
    }
    if ([String]::Equals($imagePath, $privateExe, [StringComparison]::OrdinalIgnoreCase)) {
        throw "A previous private smoke process is still using $privateExe; no files or process were changed."
    }
}

$sourceFiles = @(
    (Join-Path $buildAppDir 'pocketufo.exe'),
    (Join-Path $buildAppDir 'pocketufo.pdb'),
    (Join-Path $buildAppDir 'libopenmpt-licenses/LICENSE.txt'),
    (Join-Path $repoRoot 'main.dat'),
    (Join-Path $repoRoot 'lang.dat')
)
foreach ($path in $sourceFiles) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required Debug build or game data is missing: $path"
    }
}
foreach ($name in $requiredDlls) {
    $path = Join-Path $buildAppDir $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required OpenMPT runtime DLL is missing: $path"
    }
}
$licenseSource = Join-Path $buildAppDir 'libopenmpt-licenses'
if (-not (Test-Path -LiteralPath $licenseSource -PathType Container)) {
    throw "OpenMPT license directory is missing: $licenseSource"
}

[IO.Directory]::CreateDirectory($appDir) | Out-Null
[IO.Directory]::CreateDirectory($workerDir) | Out-Null
foreach ($name in @('pocketufo.exe', 'pocketufo.pdb', 'main.dat', 'lang.dat') + $requiredDlls) {
    $source = if ($name -in @('main.dat', 'lang.dat')) { Join-Path $repoRoot $name } else { Join-Path $buildAppDir $name }
    Copy-Item -LiteralPath $source -Destination $appDir -Force
}
Copy-Item -LiteralPath $licenseSource -Destination $appDir -Recurse -Force

$runStamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$runMode = if ($Tactical) { 'tactical' } else { 'geoscape' }
$appStdoutLog = Join-Path $workerDir "app-$runMode-$runStamp.stdout.log"
$appStderrLog = Join-Path $workerDir "app-$runMode-$runStamp.stderr.log"
$cdbLog = Join-Path $workerDir "cdb-$runMode-$runStamp.log"
$cdbCommands = Join-Path $workerDir "cdb-$runMode-$runStamp.cmd"
if ($Tactical) {
    $cdbTemplate = @'
.sympath+ "__APP_DIR__"
.reload /f pocketufo.exe
.logopen "__CDB_LOG__"
x pocketufo!PocketUFO_WndProc
x pocketufo!App_GameTickOnce
x pocketufo!World_GameTickOnce
x pocketufo!App_TickPulse
x pocketufo!World_PulseTact
x pocketufo!World_ClickEndTurn
x pocketufo!World_TactEnemyAiTickThunk
x pocketufo!Tact_EndEnemyTurn
x pocketufo!World_TactSetPlayerInput
.echo SMOKE_CDB_READY
r @$t0 = 0
r @$t1 = 0
bu10 /w "(@rdx == 0x113) && ((@r8 == 0xC350) || (@r8 == 0xC353))" pocketufo!PocketUFO_WndProc ".if (@r8 == 0xC350) { .if (@$t0 == 0) { r @$t0 = 1; .echo SMOKE_WM_TIMER_C350; } } .elsif (@r8 == 0xC353) { .if (@$t1 == 0) { r @$t1 = 1; .echo SMOKE_WM_TIMER_C353; } } ; .if (@$t0 != 0) { .if (@$t1 != 0) { bc 10; } } ; gc"
bu /1 pocketufo!App_GameTickOnce ".echo SMOKE_APP_GAME_TICK_C350; k 3; gc"
bu /1 pocketufo!World_GameTickOnce ".echo SMOKE_WORLD_GAME_TICK_C350; k 3; gc"
bu /1 pocketufo!App_TickPulse ".echo SMOKE_APP_TICK_PULSE_C353; k 3; gc"
bu /1 pocketufo!World_PulseTact ".echo SMOKE_WORLD_PULSE_TACT_C353; k 3; gc"
bu /1 pocketufo!World_ClickEndTurn ".echo SMOKE_ENDTURN_CLICK; k 4; gc"
bu /1 pocketufo!World_TactEnemyAiTickThunk ".echo SMOKE_ENEMY_AI; k 4; gc"
bu /1 pocketufo!Tact_EndEnemyTurn ".echo SMOKE_TACT_END_ENEMY; bp /1 pocketufo!World_TactSetPlayerInput \"r @$t2 = @rdx; .echo .; .echo SMOKE_PLAYER_INPUT_CALLBACK; gc\"; gu; .echo .; .echo SMOKE_TACT_END_RETURN; .echo .; .echo SMOKE_PLAYER_INPUT_STATE; dt pocketufo!PufoWorldState bMapPlayerInput @$t2; dt pocketufo!PufoWorldState bMapEnemyTurnActive @$t2; dt pocketufo!PufoWorldState bTactFlag @$t2; dt pocketufo!PufoWorldState nUiScreen @$t2; .echo SMOKE_CDB_DETACH; qd"
g
'@
} else {
    $cdbTemplate = @'
.sympath+ "__APP_DIR__"
.reload /f pocketufo.exe
.logopen "__CDB_LOG__"
x pocketufo!PocketUFO_WndProc
x pocketufo!App_DrawFrame
x pocketufo!App_TickAnim
x pocketufo!App_HookNewGame
x pocketufo!World_NewGame
x pocketufo!World_DrawGeoscapeFrame
x pocketufo!g_color
x pocketufo!g_options
.echo SMOKE_CDB_READY
r @$t0 = 0
r @$t1 = 0
bu10 /w "(@rdx == 0x113) && ((@r8 == 0xC351) || (@r8 == 0xC352))" pocketufo!PocketUFO_WndProc ".if (@r8 == 0xC351) { .if (@$t0 == 0) { r @$t0 = 1; .echo SMOKE_WM_TIMER_C351; bp /1 pocketufo!App_DrawFrame \".echo SMOKE_APP_DRAW_C351; k 3; gc\"; } } .elsif (@r8 == 0xC352) { .if (@$t1 == 0) { r @$t1 = 1; .echo SMOKE_WM_TIMER_C352; bp /1 pocketufo!App_TickAnim \".echo SMOKE_APP_TICK_ANIM_C352; .echo SMOKE_STARTUP_GAMMA_OWNER_CHECK; .if (poi(pocketufo!g_color) == (pocketufo!g_options+0x18)) { .echo SMOKE_STARTUP_GAMMA_OWNER_PASS } .else { .echo SMOKE_STARTUP_GAMMA_OWNER_FAIL }; .echo SMOKE_STARTUP_GAMMA_VALUE; ? dwo(pocketufo!g_options+0x18); .echo SMOKE_STARTUP_GAMMA_OWNER_CHECKED; k 3; gc\"; } } ; .if (@$t0 != 0) { .if (@$t1 != 0) { bc 10; } } ; gc"
bu /1 pocketufo!App_HookNewGame ".echo SMOKE_APP_HOOK_NEW_GAME; k 3; gc"
bu /1 pocketufo!World_NewGame ".echo SMOKE_WORLD_NEW_GAME; k 3; gc"
bu /1 pocketufo!World_DrawGeoscapeFrame ".echo SMOKE_WORLD_DRAW_STATE; dt pocketufo!PufoWorldState bGeoscapeUi @rcx; dt pocketufo!PufoWorldState nBaseCount @rcx; dt pocketufo!PufoWorldState nSoldierCount @rcx; dt pocketufo!PufoWorldState screen. @rcx; .echo SMOKE_GAME_CLOCK; dt pocketufo!PufoWorldState gameClock. @rcx; .echo SMOKE_TIMER_ADAPTERS; dt pocketufo!s_timers; .echo SMOKE_TIMER_KILL_POINTER; ln poi(pocketufo!s_timers); .echo SMOKE_TIMER_START_POINTER; ln poi(pocketufo!s_timers+8); .echo SMOKE_TIMER_ADAPTERS_END; .echo SMOKE_CDB_DETACH; qd"
g
'@
}
$cdbText = $cdbTemplate.Replace('__APP_DIR__', $appDir).Replace('__CDB_LOG__', $cdbLog)
[IO.File]::WriteAllText($cdbCommands, $cdbText, [Text.Encoding]::ASCII)

function Read-SharedLogSnapshot {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = $null
    $reader = $null
    try {
        $share = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, $share)
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::Default, $true, 4096, $true)
        return $reader.ReadToEnd()
    }
    finally {
        if ($null -ne $reader) {
            $reader.Dispose()
        }
        if ($null -ne $stream) {
            $stream.Dispose()
        }
    }
}

function Get-ExactLogMarkerCount {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Marker
    )

    $pattern = '(?m)^[\t ]*' + [regex]::Escape($Marker) + '[\t ]*\r?$'
    return [regex]::Matches($Text, $pattern).Count
}

function Wait-LogMarker {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Marker,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds,
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Debugger
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Debugger.Refresh()
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            try {
                $contents = Read-SharedLogSnapshot -Path $Path
                if ((Get-ExactLogMarkerCount -Text $contents -Marker $Marker) -gt 0) {
                    return $contents
                }
            }
            catch [IO.IOException] {
                # CDB may still be appending or briefly hold an incompatible open handle.
            }
        }
        if ($Debugger.HasExited) {
            throw "CDB exited before writing marker '$Marker' (exit $($Debugger.ExitCode))."
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out after $TimeoutSeconds seconds waiting for CDB marker '$Marker'."
}

function Read-StableLogSnapshot {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Marker,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $previousText = $null
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            try {
                $contents = Read-SharedLogSnapshot -Path $Path
                $markerCount = Get-ExactLogMarkerCount -Text $contents -Marker $Marker
                if ($markerCount -gt 0 -and $null -ne $previousText -and
                    [String]::Equals($contents, $previousText, [StringComparison]::Ordinal)) {
                    return $contents
                }
                $previousText = $contents
            }
            catch [IO.IOException] {
                # CDB has exited, but its final log handle may still be closing.
                $previousText = $null
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out after $TimeoutSeconds seconds waiting for a stable CDB log snapshot containing '$Marker'."
}

function Wait-PrivateWindow {
    param(
        [Parameter(Mandatory = $true)][uint32]$ProcessId,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds,
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Child
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Child.Refresh()
        if ($Child.HasExited) {
            throw "Private PocketUFO process exited during startup (exit $($Child.ExitCode))."
        }
        $hwnd = [WindowSmokeNative]::FindWindowForProcess($ProcessId, $windowClass, $windowTitle)
        if ($hwnd -ne [IntPtr]::Zero) {
            return $hwnd
        }
        Start-Sleep -Milliseconds 100
    }
    throw "No $windowClass / $windowTitle window appeared for private PID $ProcessId."
}

function Wait-AppNativeTactInfo {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds,
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Child
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Child.Refresh()
        if ($Child.HasExited) {
            throw "Private app exited before tactical prep completed (exit $($Child.ExitCode))."
        }
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            try {
                $contents = Read-SharedLogSnapshot -Path $Path
                $matches = [regex]::Matches($contents, '(?m)^pocketufo: native_tact_ready=.*\r?$')
                if ($matches.Count -gt 1) {
                    throw 'Native tactical readiness line was printed more than once.'
                }
                if ($matches.Count -eq 1) {
                    return $matches[0].Value.TrimEnd("`r")
                }
            }
            catch [IO.IOException] {
                # The app may still be flushing the line.
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out after $TimeoutSeconds seconds waiting for native tactical prep output."
}

function Get-ExactLogMarkerIndex {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Marker
    )

    $pattern = '(?m)^[\t ]*' + [regex]::Escape($Marker) + '[\t ]*\r?$'
    $matches = [regex]::Matches($Text, $pattern)
    if ($matches.Count -ne 1) {
        throw "Expected one executed CDB marker '$Marker', got $($matches.Count)."
    }
    return $matches[0].Index
}

function Get-DtInteger {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Field
    )

    $pattern = '(?im)^\s*\+0x[0-9a-f]+\s+' + [regex]::Escape($Field) + '\s*:\s*(0x[0-9a-f]+|0n-?\d+|-?\d+)'
    $match = [regex]::Match($Text, $pattern)
    if (-not $match.Success) {
        throw "CDB did not print the expected PufoWorldState field '$Field'."
    }
    $valueText = $match.Groups[1].Value
    if ($valueText.StartsWith('0x', [StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToInt64($valueText.Substring(2), 16)
    }
    if ($valueText.StartsWith('0n', [StringComparison]::OrdinalIgnoreCase)) {
        $valueText = $valueText.Substring(2)
    }
    return [Int64]::Parse($valueText, [Globalization.CultureInfo]::InvariantCulture)
}

function Test-OwnedPrivateProcess {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$ExpectedImage,
        [Parameter(Mandatory = $true)][DateTime]$ExpectedStartTime
    )

    $Process.Refresh()
    if ($Process.HasExited) {
        return $false
    }
    $imagePath = [IO.Path]::GetFullPath($Process.MainModule.FileName)
    $startTimeUtc = $Process.StartTime.ToUniversalTime()
    $wrongImage = -not [String]::Equals([IO.Path]::GetFullPath($imagePath), $ExpectedImage,
                                        [StringComparison]::OrdinalIgnoreCase)
    $wrongStartTime = $startTimeUtc -ne $ExpectedStartTime
    if ($wrongImage -or $wrongStartTime) {
        throw "Refusing to message PID $($Process.Id) because its image or start time no longer matches the process created here."
    }
    return $true
}

function Write-PrivateProcessStatus {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$ExpectedImage,
        [object]$ExpectedStartTime,
        [Parameter(Mandatory = $true)][string[]]$LogPaths
    )

    $logText = $LogPaths -join ', '
    $expectedStartText = if ($null -ne $ExpectedStartTime) {
        ([DateTime]$ExpectedStartTime).ToUniversalTime().ToString('o')
    } else {
        '<unavailable>'
    }
    try {
        $Process.Refresh()
        if ($Process.HasExited) {
            Write-Warning ("{0} exited: PID {1}, image {2}, expected_start_utc {3}, exit {4}; logs: {5}" -f `
                $Label, $Process.Id, $ExpectedImage, $expectedStartText, $Process.ExitCode, $logText)
            return
        }

        $imagePath = [IO.Path]::GetFullPath($Process.MainModule.FileName)
        $startTimeUtc = $Process.StartTime.ToUniversalTime()
        $matchesOwner = [String]::Equals($imagePath, $ExpectedImage, [StringComparison]::OrdinalIgnoreCase) -and
            $null -ne $ExpectedStartTime -and $startTimeUtc -eq [DateTime]$ExpectedStartTime
        $identity = if ($matchesOwner) { 'owned exact process' } else { 'identity mismatch' }
        Write-Warning ("{0} remains live ({1}): PID {2}, image {3}, start_utc {4:o}; expected image {5}, start_utc {6}; logs: {7}" -f `
            $Label, $identity, $Process.Id, $imagePath, $startTimeUtc, $ExpectedImage, $expectedStartText, $logText)
    }
    catch {
        Write-Warning ("Could not read {0} PID {1} image/start; no process action was taken. Expected image {2}, start_utc {3}; logs: {4}; detail: {5}" -f `
            $Label, $Process.Id, $ExpectedImage, $expectedStartText, $logText, $_.Exception.Message)
    }
}

$childProcess = $null
$debuggerProcess = $null
$childStartTimeUtc = $null
$debuggerStartTimeUtc = $null
$smokePassed = $false
$smokePassMessage = $null
$cleanupFailed = $false
$mainHwnd = [IntPtr]::Zero
$nativeInfo = $null

try {
    if ($Tactical) {
        $appArguments = "--script $nativeFlow --native-loop"
        $childProcess = Start-Process -FilePath $privateExe -ArgumentList $appArguments `
            -WorkingDirectory $appDir -WindowStyle Hidden -RedirectStandardOutput $appStdoutLog `
            -RedirectStandardError $appStderrLog -PassThru
    } else {
        $childProcess = Start-Process -FilePath $privateExe -WorkingDirectory $appDir `
            -WindowStyle Hidden -RedirectStandardOutput $appStdoutLog `
            -RedirectStandardError $appStderrLog -PassThru
    }
    $childStartTimeUtc = $childProcess.StartTime.ToUniversalTime()
    $childId = [uint32]$childProcess.Id
    $childImage = [IO.Path]::GetFullPath($childProcess.MainModule.FileName)
    if (-not [String]::Equals($childImage, $privateExe, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Started process image is not the private copy: $childImage"
    }

    $mainHwnd = Wait-PrivateWindow -ProcessId $childId -TimeoutSeconds 15 -Child $childProcess
    [void][WindowSmokeNative]::ShowWindow($mainHwnd, 0) # hide only this private HWND; no global mouse input is used
    Write-Host ("Private app PID {0}; HWND {1}; image {2}" -f $childId, $mainHwnd, $childImage)

    if ($Tactical) {
        $nativeInfoLine = Wait-AppNativeTactInfo -Path $appStdoutLog -TimeoutSeconds 180 -Child $childProcess
        $nativeInfoPattern = '^pocketufo: native_tact_ready=(?<ready>\d+) screen=(?<screen>\d+) tact=(?<tact>\d+) live=(?<live>\d+) input=(?<input>\d+) enemyturn=(?<enemyturn>\d+) busy=(?<busy>\d+) phase=(?<phase>-?\d+) queue=(?<queue>-?\d+) queueactive=(?<queueactive>-?\d+) xcom=(?<xcom>-?\d+) enemy=(?<enemy>-?\d+) selected=(?<selected>-?\d+) endturn_enabled=(?<enabled>\d+) endturn_click=(?<click>\d+) logical=(?<x>-?\d+),(?<y>-?\d+) size=(?<width>\d+),(?<height>\d+) center=(?<centerx>-?\d+),(?<centery>-?\d+)$'
        $nativeInfoMatch = [regex]::Match($nativeInfoLine, $nativeInfoPattern)
        if (-not $nativeInfoMatch.Success) {
            throw "Cannot parse native tactical readiness line: $nativeInfoLine"
        }
        $nativeInfo = [ordered]@{}
        foreach ($field in @('ready', 'screen', 'tact', 'live', 'input', 'enemyturn', 'busy',
                             'phase', 'queue', 'queueactive', 'xcom', 'enemy', 'selected', 'enabled', 'click', 'x', 'y',
                             'width', 'height', 'centerx', 'centery')) {
            $nativeInfo[$field] = [int]$nativeInfoMatch.Groups[$field].Value
        }
        if ($nativeInfo.ready -ne 1 -or $nativeInfo.screen -ne 0 -or $nativeInfo.tact -ne 1 -or
            $nativeInfo.live -ne 1 -or $nativeInfo.input -ne 1 -or $nativeInfo.enemyturn -ne 0 -or
            $nativeInfo.busy -ne 0 -or $nativeInfo.phase -ne 0 -or $nativeInfo.queue -ne 0 -or
            $nativeInfo.queueactive -ne 0 -or $nativeInfo.xcom -lt 1 -or $nativeInfo.enemy -lt 1 -or
            $nativeInfo.selected -lt 0 -or $nativeInfo.selected -gt 8 -or
            $nativeInfo.enabled -ne 1 -or $nativeInfo.click -ne 1 -or
            $nativeInfo.width -lt 1 -or $nativeInfo.height -lt 1 -or
            $nativeInfo.centerx -lt 0 -or $nativeInfo.centerx -ge 240 -or
            $nativeInfo.centery -lt 0 -or $nativeInfo.centery -ge 320 -or
            $nativeInfo.centerx -lt $nativeInfo.x -or
            $nativeInfo.centerx -ge ($nativeInfo.x + $nativeInfo.width) -or
            $nativeInfo.centery -lt $nativeInfo.y -or
            $nativeInfo.centery -ge ($nativeInfo.y + $nativeInfo.height)) {
            throw "Native tactical prep is not ready for End Turn: $nativeInfoLine"
        }
        Write-Host ("Native tactical ready: screen {0}; XCOM {1}; enemy {2}; End Turn center {3},{4}" -f `
            $nativeInfo.screen, $nativeInfo.xcom, $nativeInfo.enemy, $nativeInfo.centerx, $nativeInfo.centery)
    }

    $debuggerProcess = Start-Process -FilePath $cdbExe `
        -ArgumentList @('-p', "$childId", '-pd', '-cf', $cdbCommands) `
        -WorkingDirectory $appDir -WindowStyle Hidden -PassThru
    $debuggerStartTimeUtc = $debuggerProcess.StartTime.ToUniversalTime()
    Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_CDB_READY' -TimeoutSeconds 15 -Debugger $debuggerProcess | Out-Null

    if (-not $Tactical) {
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_STARTUP_GAMMA_OWNER_CHECKED' `
            -TimeoutSeconds 15 -Debugger $debuggerProcess | Out-Null
        $startupGammaLog = Read-SharedLogSnapshot -Path $cdbLog
        $gammaPassCount = Get-ExactLogMarkerCount -Text $startupGammaLog `
            -Marker 'SMOKE_STARTUP_GAMMA_OWNER_PASS'
        $gammaFailCount = Get-ExactLogMarkerCount -Text $startupGammaLog `
            -Marker 'SMOKE_STARTUP_GAMMA_OWNER_FAIL'
        if ($gammaPassCount -ne 1 -or $gammaFailCount -ne 0) {
            throw 'CDB startup check failed: g_color.pnGamma is not &g_options.nGamma before NewGame.'
        }
    }

    if ($Tactical) {
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_WM_TIMER_C350' -TimeoutSeconds 20 -Debugger $debuggerProcess | Out-Null
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_APP_GAME_TICK_C350' -TimeoutSeconds 20 -Debugger $debuggerProcess | Out-Null
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_WORLD_GAME_TICK_C350' -TimeoutSeconds 20 -Debugger $debuggerProcess | Out-Null
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_WM_TIMER_C353' -TimeoutSeconds 20 -Debugger $debuggerProcess | Out-Null
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_APP_TICK_PULSE_C353' -TimeoutSeconds 20 -Debugger $debuggerProcess | Out-Null
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_WORLD_PULSE_TACT_C353' -TimeoutSeconds 20 -Debugger $debuggerProcess | Out-Null
    } else {
        # C351 is the real render timer; its WndProc break arms the matching App_DrawFrame one-shot.
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_APP_DRAW_C351' -TimeoutSeconds 15 -Debugger $debuggerProcess | Out-Null
        # C352 is the animation timer; its WndProc break arms the matching App_TickAnim one-shot.
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_APP_TICK_ANIM_C352' -TimeoutSeconds 15 -Debugger $debuggerProcess | Out-Null
    }

    $clientRect = [WindowSmokeNative+RECT]::new()
    if (-not [WindowSmokeNative]::GetClientRect($mainHwnd, [ref]$clientRect)) {
        throw 'GetClientRect failed for the private PocketUFO window.'
    }
    $clientWidth = $clientRect.right - $clientRect.left
    $clientHeight = $clientRect.bottom - $clientRect.top
    $scale = [Math]::Min([int][Math]::Truncate($clientWidth / 240.0),
                         [int][Math]::Truncate($clientHeight / 320.0))
    if ($scale -lt 1) {
        $scale = 1
    }
    $offsetX = [int][Math]::Truncate(($clientWidth - 240 * $scale) / 2.0)
    $offsetY = [int][Math]::Truncate(($clientHeight - 320 * $scale) / 2.0)
    if ($Tactical) {
        $logicalX = $nativeInfo.centerx
        $logicalY = $nativeInfo.centery
    } else {
        $logicalX = 120
        $logicalY = 124
    }
    $clientX = $offsetX + $logicalX * $scale + [int][Math]::Floor($scale / 2.0)
    $clientY = $offsetY + $logicalY * $scale + [int][Math]::Floor($scale / 2.0)
    $roundTripX = [int][Math]::Truncate(($clientX - $offsetX) / [double]$scale)
    $roundTripY = [int][Math]::Truncate(($clientY - $offsetY) / [double]$scale)
    $badRoundTrip = $roundTripX -ne $logicalX -or $roundTripY -ne $logicalY
    $outsideClient = $clientX -lt 0 -or $clientX -ge $clientWidth -or $clientY -lt 0 -or $clientY -ge $clientHeight
    if ($badRoundTrip -or $outsideClient) {
        throw "Client mapping failed: logical $logicalX,$logicalY -> client $clientX,$clientY ($clientWidth x $clientHeight, scale $scale, offset $offsetX,$offsetY)."
    }
    Write-Host ("Posting private-HWND mouse messages for logical {0},{1} -> client {2},{3}; client {4}x{5}, scale {6}, offset {7},{8}" -f `
        $logicalX, $logicalY, $clientX, $clientY, $clientWidth, $clientHeight, $scale, $offsetX, $offsetY)

    if (-not [WindowSmokeNative]::PostClientMouse($mainHwnd, 0x0200, 0, $clientX, $clientY)) {
        throw 'PostMessage(WM_MOUSEMOVE) failed for the private window.'
    }
    Start-Sleep -Milliseconds 40
    if (-not [WindowSmokeNative]::PostClientMouse($mainHwnd, 0x0201, 1, $clientX, $clientY)) {
        throw 'PostMessage(WM_LBUTTONDOWN) failed for the private window.'
    }
    Start-Sleep -Milliseconds 40
    if (-not [WindowSmokeNative]::PostClientMouse($mainHwnd, 0x0202, 0, $clientX, $clientY)) {
        throw 'PostMessage(WM_LBUTTONUP) failed for the private window.'
    }

    if ($Tactical) {
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_ENDTURN_CLICK' -TimeoutSeconds 15 -Debugger $debuggerProcess | Out-Null
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_ENEMY_AI' -TimeoutSeconds 180 -Debugger $debuggerProcess | Out-Null
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_TACT_END_ENEMY' -TimeoutSeconds 300 -Debugger $debuggerProcess | Out-Null
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_PLAYER_INPUT_CALLBACK' -TimeoutSeconds 15 -Debugger $debuggerProcess | Out-Null
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_TACT_END_RETURN' -TimeoutSeconds 15 -Debugger $debuggerProcess | Out-Null
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_PLAYER_INPUT_STATE' -TimeoutSeconds 15 -Debugger $debuggerProcess | Out-Null
    } else {
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_APP_HOOK_NEW_GAME' -TimeoutSeconds 15 -Debugger $debuggerProcess | Out-Null
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_WORLD_NEW_GAME' -TimeoutSeconds 15 -Debugger $debuggerProcess | Out-Null
        Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_WORLD_DRAW_STATE' -TimeoutSeconds 15 -Debugger $debuggerProcess | Out-Null
    }
    Wait-LogMarker -Path $cdbLog -Marker 'SMOKE_CDB_DETACH' -TimeoutSeconds 15 -Debugger $debuggerProcess | Out-Null

    $debuggerProcess.Refresh()
    if (-not $debuggerProcess.HasExited) {
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        while (-not $debuggerProcess.HasExited -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 100
            $debuggerProcess.Refresh()
        }
    }
    if (-not $debuggerProcess.HasExited) {
        throw 'CDB logged qd but did not exit after detaching.'
    }

    # CDB has exited. Wait for the final log snapshot to become readable and stable;
    # strict callback and state checks below still validate the complete snapshot.
    $allLogText = Read-StableLogSnapshot -Path $cdbLog `
        -Marker 'SMOKE_CDB_DETACH' -TimeoutSeconds 5
    if ($Tactical) {
        foreach ($marker in @(
            'SMOKE_CDB_READY',
            'SMOKE_WM_TIMER_C350', 'SMOKE_APP_GAME_TICK_C350', 'SMOKE_WORLD_GAME_TICK_C350',
            'SMOKE_WM_TIMER_C353', 'SMOKE_APP_TICK_PULSE_C353', 'SMOKE_WORLD_PULSE_TACT_C353',
            'SMOKE_ENDTURN_CLICK', 'SMOKE_ENEMY_AI', 'SMOKE_TACT_END_ENEMY',
            'SMOKE_PLAYER_INPUT_CALLBACK', 'SMOKE_TACT_END_RETURN',
            'SMOKE_PLAYER_INPUT_STATE', 'SMOKE_CDB_DETACH'
        )) {
            $markerCount = Get-ExactLogMarkerCount -Text $allLogText -Marker $marker
            if ($markerCount -ne 1) {
                throw "CDB log must contain exactly one executed marker '$marker'; got $markerCount."
            }
        }

        $idxC350 = Get-ExactLogMarkerIndex -Text $allLogText -Marker 'SMOKE_WM_TIMER_C350'
        $idxAppTick = Get-ExactLogMarkerIndex -Text $allLogText -Marker 'SMOKE_APP_GAME_TICK_C350'
        $idxWorldTick = Get-ExactLogMarkerIndex -Text $allLogText -Marker 'SMOKE_WORLD_GAME_TICK_C350'
        $idxC353 = Get-ExactLogMarkerIndex -Text $allLogText -Marker 'SMOKE_WM_TIMER_C353'
        $idxAppPulse = Get-ExactLogMarkerIndex -Text $allLogText -Marker 'SMOKE_APP_TICK_PULSE_C353'
        $idxWorldPulse = Get-ExactLogMarkerIndex -Text $allLogText -Marker 'SMOKE_WORLD_PULSE_TACT_C353'
        if ($idxC350 -ge $idxAppTick -or $idxAppTick -ge $idxWorldTick -or
            $idxC353 -ge $idxAppPulse -or $idxAppPulse -ge $idxWorldPulse) {
            throw 'The OS timer markers did not precede their app and world callbacks.'
        }

        $idxEndTurn = Get-ExactLogMarkerIndex -Text $allLogText -Marker 'SMOKE_ENDTURN_CLICK'
        $idxEnemyAi = Get-ExactLogMarkerIndex -Text $allLogText -Marker 'SMOKE_ENEMY_AI'
        $idxEndEnemy = Get-ExactLogMarkerIndex -Text $allLogText -Marker 'SMOKE_TACT_END_ENEMY'
        $idxInputCallback = Get-ExactLogMarkerIndex -Text $allLogText -Marker 'SMOKE_PLAYER_INPUT_CALLBACK'
        $idxEndEnemyReturn = Get-ExactLogMarkerIndex -Text $allLogText -Marker 'SMOKE_TACT_END_RETURN'
        $idxInputState = Get-ExactLogMarkerIndex -Text $allLogText -Marker 'SMOKE_PLAYER_INPUT_STATE'
        $idxDetach = Get-ExactLogMarkerIndex -Text $allLogText -Marker 'SMOKE_CDB_DETACH'
        if ($idxEndTurn -ge $idxEnemyAi -or $idxEnemyAi -ge $idxEndEnemy -or
            $idxEndEnemy -ge $idxInputCallback -or $idxInputCallback -ge $idxEndEnemyReturn -or
            $idxEndEnemyReturn -ge $idxInputState -or
            $idxInputState -ge $idxDetach) {
            throw 'End Turn, enemy AI, input callback, full enemy-turn return, restored input, and detach were not in order.'
        }

        $stateStart = $idxInputState + ([regex]::Match(
            $allLogText.Substring($idxInputState), '(?m)^[\t ]*SMOKE_PLAYER_INPUT_STATE[\t ]*\r?$')).Length
        $stateLength = $idxDetach - $stateStart
        if ($stateLength -lt 0) {
            throw 'CDB did not print the player-input state before detach.'
        }
        $tacticalStateText = $allLogText.Substring($stateStart, $stateLength)
        $state = [ordered]@{
            bMapPlayerInput = Get-DtInteger -Text $tacticalStateText -Field 'bMapPlayerInput'
            bMapEnemyTurnActive = Get-DtInteger -Text $tacticalStateText -Field 'bMapEnemyTurnActive'
            bTactFlag = Get-DtInteger -Text $tacticalStateText -Field 'bTactFlag'
            nUiScreen = Get-DtInteger -Text $tacticalStateText -Field 'nUiScreen'
        }
        $stateText = ($state.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ', '
        if ($state.bMapPlayerInput -ne 1 -or $state.bMapEnemyTurnActive -ne 0 -or
            $state.bTactFlag -ne 1 -or $state.nUiScreen -ne 0) {
            throw "End Turn did not return to player input: $stateText."
        }

        $smokePassed = $true
        $smokePassMessage = ("PASS: native tactical pump; C350/GameTick and C353/Pulse; posted End Turn -> AI -> player input ({0})" -f `
            (($state.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ', '))
    } else {
    foreach ($marker in @(
        'SMOKE_CDB_READY', 'SMOKE_STARTUP_GAMMA_OWNER_CHECK',
        'SMOKE_STARTUP_GAMMA_OWNER_PASS', 'SMOKE_STARTUP_GAMMA_VALUE',
        'SMOKE_STARTUP_GAMMA_OWNER_CHECKED',
        'SMOKE_WM_TIMER_C351', 'SMOKE_APP_DRAW_C351',
        'SMOKE_WM_TIMER_C352', 'SMOKE_APP_TICK_ANIM_C352',
        'SMOKE_APP_HOOK_NEW_GAME', 'SMOKE_WORLD_NEW_GAME',
        'SMOKE_WORLD_DRAW_STATE', 'SMOKE_GAME_CLOCK',
        'SMOKE_TIMER_ADAPTERS', 'SMOKE_TIMER_KILL_POINTER',
        'SMOKE_TIMER_START_POINTER', 'SMOKE_TIMER_ADAPTERS_END',
        'SMOKE_CDB_DETACH'
    )) {
        $markerCount = Get-ExactLogMarkerCount -Text $allLogText -Marker $marker
        if ($markerCount -ne 1) {
            throw "CDB log must contain exactly one executed marker '$marker'; got $markerCount."
        }
    }

    $statePattern = '(?m)^[\t ]*SMOKE_WORLD_DRAW_STATE[\t ]*\r?$'
    $detachPattern = '(?m)^[\t ]*SMOKE_CDB_DETACH[\t ]*\r?$'
    $stateMarker = [regex]::Match($allLogText, $statePattern)
    $detachMarker = [regex]::Match($allLogText, $detachPattern)
    $stateStart = $stateMarker.Index + $stateMarker.Length
    $stateLength = $detachMarker.Index - $stateStart
    if (-not $stateMarker.Success -or -not $detachMarker.Success -or $stateLength -lt 0) {
        throw 'CDB did not produce an ordered world-frame state block before detach.'
    }
    $geoStateText = $allLogText.Substring($stateStart, $stateLength)
    $state = [ordered]@{
        bGeoscapeUi = Get-DtInteger -Text $geoStateText -Field 'bGeoscapeUi'
        nBaseCount = Get-DtInteger -Text $geoStateText -Field 'nBaseCount'
        nSoldierCount = Get-DtInteger -Text $geoStateText -Field 'nSoldierCount'
        nGeoscapeScreen = Get-DtInteger -Text $geoStateText -Field 'nGeoscapeScreen'
    }
    $expected = @{ bGeoscapeUi = 1; nBaseCount = 1; nSoldierCount = 8; nGeoscapeScreen = 2 }
    foreach ($field in $expected.Keys) {
        if ($state[$field] -ne $expected[$field]) {
            throw "World_DrawGeoscapeFrame state mismatch: $field=$($state[$field]), expected $($expected[$field])."
        }
    }

    $clockPattern = '(?m)^[\t ]*SMOKE_GAME_CLOCK[\t ]*\r?$'
    $adapterPattern = '(?m)^[\t ]*SMOKE_TIMER_ADAPTERS[\t ]*\r?$'
    $killPattern = '(?m)^[\t ]*SMOKE_TIMER_KILL_POINTER[\t ]*\r?$'
    $startPattern = '(?m)^[\t ]*SMOKE_TIMER_START_POINTER[\t ]*\r?$'
    $adapterEndPattern = '(?m)^[\t ]*SMOKE_TIMER_ADAPTERS_END[\t ]*\r?$'
    $clockMarker = [regex]::Match($allLogText, $clockPattern)
    $adapterMarker = [regex]::Match($allLogText, $adapterPattern)
    $killMarker = [regex]::Match($allLogText, $killPattern)
    $startMarker = [regex]::Match($allLogText, $startPattern)
    $adapterEndMarker = [regex]::Match($allLogText, $adapterEndPattern)
    $markersFound = $clockMarker.Success -and $adapterMarker.Success -and
        $killMarker.Success -and $startMarker.Success -and $adapterEndMarker.Success
    $markersOrdered = $clockMarker.Index -lt $adapterMarker.Index -and
        $adapterMarker.Index -lt $killMarker.Index -and
        $killMarker.Index -lt $startMarker.Index -and
        $startMarker.Index -lt $adapterEndMarker.Index
    if (-not $markersFound -or -not $markersOrdered) {
        throw 'CDB did not produce the ordered clock and timer-adapter evidence blocks.'
    }
    $clockText = $allLogText.Substring(
        $clockMarker.Index + $clockMarker.Length,
        $adapterMarker.Index - ($clockMarker.Index + $clockMarker.Length))
    $timerSpeed = Get-DtInteger -Text $clockText -Field 'nSpeed'
    if ($timerSpeed -lt 0 -or $timerSpeed -gt 4) {
        throw "The live game clock has an out-of-range speed ($timerSpeed)."
    }
    $timerAdapterText = $allLogText.Substring(
        $adapterMarker.Index + $adapterMarker.Length,
        $killMarker.Index - ($adapterMarker.Index + $adapterMarker.Length))
    $hasTimerFields = ($timerAdapterText -match '(?im)\bpfnKillAll\b') -and
        ($timerAdapterText -match '(?im)\bpfnStartAll\b')
    if (-not $hasTimerFields) {
        throw 'CDB did not show both fields in the installed GameTimers record.'
    }
    $killPointerText = $allLogText.Substring(
        $killMarker.Index + $killMarker.Length,
        $startMarker.Index - ($killMarker.Index + $killMarker.Length))
    $startPointerText = $allLogText.Substring(
        $startMarker.Index + $startMarker.Length,
        $adapterEndMarker.Index - ($startMarker.Index + $startMarker.Length))
    $killPointerMatches = $killPointerText -match '(?i)pocketufo!Bridge_KillAll'
    $startPointerMatches = $startPointerText -match '(?i)pocketufo!Bridge_StartAll'
    if (-not $killPointerMatches -or -not $startPointerMatches) {
        throw 'The installed timer callbacks do not resolve to Bridge_KillAll and Bridge_StartAll.'
    }

    $smokePassed = $true
    $smokePassMessage = ("PASS: native window path; C351/App_DrawFrame and C352/App_TickAnim; World_NewGame -> frame {0}; GameSpeed_Set adapters Bridge_KillAll/Bridge_StartAll, speed {1}" -f `
        (($state.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ', '), $timerSpeed)
    }
    Write-Host "CDB log: $cdbLog"
    Write-Host "Private app: $appDir"
}
finally {
    if (-not $smokePassed) {
        Write-Warning 'Probe did not pass; no process was stopped and no window was closed.'
        if ($null -ne $debuggerProcess) {
            Write-PrivateProcessStatus -Process $debuggerProcess -Label 'CDB' `
                -ExpectedImage ([IO.Path]::GetFullPath($cdbExe)) -ExpectedStartTime $debuggerStartTimeUtc `
                -LogPaths @($cdbLog, $cdbCommands)
        }
        if ($null -ne $childProcess) {
            Write-PrivateProcessStatus -Process $childProcess -Label 'Private PocketUFO' `
                -ExpectedImage $privateExe -ExpectedStartTime $childStartTimeUtc `
                -LogPaths @($appStdoutLog, $appStderrLog, $cdbLog)
        }
    } else {
        $debuggerDetached = ($null -eq $debuggerProcess)
        if ($null -ne $debuggerProcess) {
            try {
                $debuggerProcess.Refresh()
                $debuggerDetached = $debuggerProcess.HasExited
            }
            catch {
                $cleanupFailed = $true
                Write-Warning "Could not confirm CDB exit; preserving the CDB and app: $_"
                Write-PrivateProcessStatus -Process $debuggerProcess -Label 'CDB' `
                    -ExpectedImage ([IO.Path]::GetFullPath($cdbExe)) -ExpectedStartTime $debuggerStartTimeUtc `
                    -LogPaths @($cdbLog, $cdbCommands)
                if ($null -ne $childProcess) {
                    Write-PrivateProcessStatus -Process $childProcess -Label 'Private PocketUFO' `
                        -ExpectedImage $privateExe -ExpectedStartTime $childStartTimeUtc `
                        -LogPaths @($appStdoutLog, $appStderrLog, $cdbLog)
                }
            }
        }

        if (-not $debuggerDetached) {
            if (-not $cleanupFailed) {
                $cleanupFailed = $true
                Write-Warning 'CDB is still live after the probe; preserving CDB and app without process action.'
                Write-PrivateProcessStatus -Process $debuggerProcess -Label 'CDB' `
                    -ExpectedImage ([IO.Path]::GetFullPath($cdbExe)) -ExpectedStartTime $debuggerStartTimeUtc `
                    -LogPaths @($cdbLog, $cdbCommands)
                Write-PrivateProcessStatus -Process $childProcess -Label 'Private PocketUFO' `
                    -ExpectedImage $privateExe -ExpectedStartTime $childStartTimeUtc `
                    -LogPaths @($appStdoutLog, $appStderrLog, $cdbLog)
            }
        } elseif ($null -ne $childProcess) {
            try {
                $childProcess.Refresh()
                if ($childProcess.HasExited) {
                    Write-Host ("Private PocketUFO PID {0} already exited; no close message was needed." -f $childProcess.Id)
                } elseif (Test-OwnedPrivateProcess -Process $childProcess -ExpectedImage $privateExe `
                            -ExpectedStartTime $childStartTimeUtc) {
                    if ($mainHwnd -eq [IntPtr]::Zero) {
                        throw 'The private app HWND is missing; no close message was sent.'
                    }
                    if (-not [WindowSmokeNative]::PostWindowCloseForProcess(
                            $mainHwnd, [uint32]$childProcess.Id, $windowClass, $windowTitle)) {
                        throw 'WM_CLOSE was not posted for the expected private window.'
                    }
                    Write-Host ("Posted WM_CLOSE to owned private HWND {0}; waiting for PID {1} to exit." -f `
                        $mainHwnd, $childProcess.Id)
                    $closeDeadline = [DateTime]::UtcNow.AddSeconds(5)
                    do {
                        Start-Sleep -Milliseconds 100
                        $childProcess.Refresh()
                    } while (-not $childProcess.HasExited -and [DateTime]::UtcNow -lt $closeDeadline)
                    if (-not $childProcess.HasExited) {
                        $cleanupFailed = $true
                        Write-Warning 'WM_CLOSE was posted, but the private app remains live; it was preserved.'
                        Write-PrivateProcessStatus -Process $childProcess -Label 'Private PocketUFO' `
                            -ExpectedImage $privateExe -ExpectedStartTime $childStartTimeUtc `
                            -LogPaths @($appStdoutLog, $appStderrLog, $cdbLog)
                    }
                } else {
                    $childProcess.Refresh()
                    if ($childProcess.HasExited) {
                        Write-Host ("Private PocketUFO PID {0} exited before close; no close message was needed." -f $childProcess.Id)
                    } else {
                        $cleanupFailed = $true
                        Write-Warning 'Could not confirm ownership of the live private app; no window message was sent.'
                        Write-PrivateProcessStatus -Process $childProcess -Label 'Private PocketUFO' `
                            -ExpectedImage $privateExe -ExpectedStartTime $childStartTimeUtc `
                            -LogPaths @($appStdoutLog, $appStderrLog, $cdbLog)
                    }
                }
            }
            catch {
                $cleanupFailed = $true
                Write-Warning "Could not verify or close the owned private window: $_"
                Write-PrivateProcessStatus -Process $childProcess -Label 'Private PocketUFO' `
                    -ExpectedImage $privateExe -ExpectedStartTime $childStartTimeUtc `
                    -LogPaths @($appStdoutLog, $appStderrLog, $cdbLog)
            }
        }
    }
}

if (-not $smokePassed -or $cleanupFailed) {
    exit 1
}
Write-Host $smokePassMessage
exit 0
