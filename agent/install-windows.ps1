# Install the Pentadeck stats agent for this Windows user: it starts hidden at logon and serves this
# PC's stats as JSON on :8765 for the device on the LAN. No admin needed, except one prompt to allow
# the port through Windows Firewall.
#
#   Double-click install-windows.cmd, or in PowerShell:
#     irm https://raw.githubusercontent.com/lesyhau/pentadeck/main/agent/install-windows.ps1 | iex
#   With options (no questions asked):
#     powershell -ExecutionPolicy Bypass -File install-windows.ps1 -Name PC -Disks C:\,D:\ -Mic -Token abc
#     & ([scriptblock]::Create((irm <url above>))) -Name PC -Mic -Token abc
#
# What it does:
#   - copies pentadeck_agent.py to %LOCALAPPDATA%\Pentadeck (downloads it if it isn't next to this file)
#   - adds "Pentadeck agent" to your Startup folder (pythonw.exe: no window) and starts it now
#   - moves an older setup out of the way (timegate-agent.vbs and the like, and its running process)
#   - asks Windows for admin rights once, to allow TCP <port> from your local subnet in the firewall
#   - checks http://127.0.0.1:<port>/stats and prints the URL for the device's settings page
# Run it again to upgrade or change settings: it keeps your previous answers as defaults.
# Undo with uninstall-windows.ps1.
#
#   -Name      the label the device shows (default: this computer's name)
#   -Disks     drives to report (default: every fixed drive)
#   -Port      the port to serve on (default 8765)
#   -Services  local TCP services to check once a second, as name:port (default none)
#   -Mic       also play the device's microphone into VB-CABLE, so apps can use it as a microphone
#   -Token     the device's access token, for -Mic (saved as your PENTADECK_TOKEN environment variable)
#   -Yes       ask nothing: use the options given, the previous install's, or the defaults
#   -NoFirewall  don't touch the firewall
#   -MakePrivate if this PC's network is set to Public, set it to Private (Windows blocks the port on
#                Public networks); without -Yes it asks
#   -Sandbox DIR (for testing) install under DIR instead: no real Startup entry, firewall or
#                environment variable is touched. Also read from $env:PENTADECK_SANDBOX.
param(
    [string]$Name = '',
    [string[]]$Disks = @(),
    [int]$Port = 0,
    [string[]]$Services = @(),
    [switch]$Mic,
    [string]$Token = '',
    [switch]$Yes,
    [switch]$NoFirewall,
    [switch]$MakePrivate,
    [string]$Sandbox = ''
)

$ErrorActionPreference = 'Stop'
$PentadeckBound = @{}      # the options given (under irm | iex there are none)
if ($PSBoundParameters) { foreach ($k in $PSBoundParameters.Keys) { $PentadeckBound[$k] = $PSBoundParameters[$k] } }
$PentadeckAgentUrl = 'https://raw.githubusercontent.com/lesyhau/pentadeck/main/agent/pentadeck_agent.py'

function Say([string]$msg) { Write-Host $msg }
function Note([string]$msg) { Write-Host $msg -ForegroundColor Yellow }
function Good([string]$msg) { Write-Host $msg -ForegroundColor Green }

function Ask([string]$question, [string]$default) {
    $hint = ''
    if ($default -ne '') { $hint = " [$default]" }
    $a = Read-Host "$question$hint"
    if ($null -eq $a -or $a.Trim() -eq '') { return $default }
    return $a.Trim()
}

function AskYes([string]$question, [bool]$default) {
    $d = 'y/N'
    if ($default) { $d = 'Y/n' }
    $a = Read-Host "$question [$d]"
    if ($null -eq $a -or $a.Trim() -eq '') { return $default }
    return ($a.Trim() -match '^[yY]')
}

# One argument for a Windows command line (CommandLineToArgvW rules: backslashes before a quote
# or the closing quote are doubled; C:\ stays bare because it has no space).
function Quote-Arg([string]$s) {
    if ($s -ne '' -and $s -notmatch '[\s"]') { return $s }
    $s = $s -replace '(\\*)"', '$1$1\"'
    $s = $s -replace '(\\+)$', '$1$1'
    return '"' + $s + '"'
}

function Split-List($items) {
    return @($items | ForEach-Object { "$_" -split '[,; ]+' } | Where-Object { $_ -ne '' })
}

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Runs a PowerShell snippet as administrator (one UAC prompt). Returns $true if it ran and succeeded.
function Invoke-Elevated([string]$script, [string]$what) {
    $log = Join-Path $env:TEMP "pentadeck-elevated-$PID.txt"
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    $logQ = $log -replace "'", "''"
    $wrapped = "`$ErrorActionPreference = 'Stop'; try { $script; exit 0 } " +
               "catch { Set-Content -LiteralPath '$logQ' -Value `$_.Exception.Message; exit 1 }"
    $enc = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($wrapped))
    $argv = '-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', $enc
    try {
        if (Test-Admin) {
            $p = Start-Process powershell.exe -ArgumentList $argv -WindowStyle Hidden -Wait -PassThru
        } else {
            Say "Windows will ask for admin rights to $what."
            $p = Start-Process powershell.exe -ArgumentList $argv -Verb RunAs -WindowStyle Hidden -Wait -PassThru
        }
    } catch {
        Note "You declined the admin prompt, so this was skipped: $what."
        return $false
    }
    if ($p.ExitCode -ne 0 -and (Test-Path $log)) { Note "Error: $((Get-Content $log -Raw).Trim())" }
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    return ($p.ExitCode -eq 0)
}

# ---------------------------------------------------------------------------------------------- Python

function Get-PythonVersion([string]$exe) {
    $ErrorActionPreference = 'Continue'   # a native program's stderr must not throw
    try {
        $v = & $exe -I -c "import sys; print('%d.%d' % sys.version_info[:2])" 2>$null
        if ($LASTEXITCODE -eq 0 -and $v -match '^\d+\.\d+$') { return [version]"$v" }
    } catch { }
    return $null
}

# The newest usable python.exe with a pythonw.exe beside it, or $null. Skips the Microsoft Store's
# python.exe stub (and Store Python, which can't be started from its real path at logon).
function Find-Python {
    $ErrorActionPreference = 'Continue'
    $candidates = New-Object System.Collections.Generic.List[string]
    if (Get-Command py.exe -CommandType Application -ErrorAction SilentlyContinue) {
        try {
            $exe = & py.exe -3 -c "import sys; print(sys.executable)" 2>$null
            if ($LASTEXITCODE -eq 0 -and $exe) { $candidates.Add("$exe".Trim()) }
        } catch { }
    }
    foreach ($n in 'python.exe', 'pythonw.exe') {
        foreach ($c in @(Get-Command $n -CommandType Application -All -ErrorAction SilentlyContinue)) {
            $candidates.Add((Join-Path (Split-Path $c.Path) 'python.exe'))
        }
    }
    foreach ($root in "$env:LOCALAPPDATA\Programs\Python", "$env:ProgramFiles") {
        if (Test-Path $root) {
            Get-ChildItem $root -Directory -Filter 'Python3*' -ErrorAction SilentlyContinue |
                Sort-Object { [int]($_.Name -replace '\D', '') } -Descending |
                ForEach-Object { $candidates.Add((Join-Path $_.FullName 'python.exe')) }
        }
    }
    $sawStub = $false
    foreach ($exe in $candidates) {
        if ($exe -like '*\WindowsApps\*') { $sawStub = $true; continue }
        if (-not (Test-Path $exe)) { continue }
        $w = Join-Path (Split-Path $exe) 'pythonw.exe'
        if (-not (Test-Path $w)) { continue }
        $v = Get-PythonVersion $exe
        if ($v -and $v -ge [version]'3.9') {
            return [pscustomobject]@{ Python = $exe; Pythonw = $w; Version = $v }
        }
    }
    if ($sawStub) { Note 'Skipped the Microsoft Store "python.exe": it can''t be started hidden at logon.' }
    return $null
}

function Get-Python([bool]$ask) {
    $py = Find-Python
    if ($py) { return $py }
    Note 'Python 3.9 or newer was not found (the Microsoft Store shortcut does not count).'
    Say  'Install it from https://www.python.org/downloads/ or with:'
    Say  '    winget install -e --id Python.Python.3.13'
    $winget = Get-Command winget.exe -CommandType Application -ErrorAction SilentlyContinue
    if ($ask -and $winget -and (AskYes 'Run that now?' $true)) {
        $ErrorActionPreference = 'Continue'
        & winget.exe install -e --id Python.Python.3.13 --scope user --accept-package-agreements --accept-source-agreements
        $py = Find-Python
        if ($py) { return $py }
    }
    throw 'Python is needed. Install it, then run this installer again.'
}

function Test-PythonFile([string]$python, [string]$file) {
    $ErrorActionPreference = 'Continue'
    & $python -I -c "import ast, sys; ast.parse(open(sys.argv[1], 'rb').read())" $file 2>$null | Out-Null
    return ($LASTEXITCODE -eq 0)
}

function Test-Has([string]$text, [string]$part) {   # case-insensitive, no wildcards
    return $text.ToLowerInvariant().Contains($part.ToLowerInvariant())
}

# ---------------------------------------------------------------------------------------------- older setups

# Python processes running a Pentadeck / Times Gate agent: Id, Port, Path, CommandLine.
function Get-AgentProcesses {
    $out = @()
    $procs = Get-CimInstance Win32_Process -Filter "Name like 'py%.exe'" -ErrorAction SilentlyContinue
    foreach ($p in @($procs)) {
        $cl = "$($p.CommandLine)"
        if ($cl -notmatch '(?i)(timegate|pentadeck)_agent\.py') { continue }
        $port = 8765
        if ($cl -match '--port[= ]+(\d+)') { $port = [int]$Matches[1] }
        $out += [pscustomobject]@{ Id = $p.ProcessId; Port = $port; CommandLine = $cl }
    }
    return $out
}

function Wait-PortFree([int]$port) {
    for ($i = 0; $i -lt 30; $i++) {
        $l = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
        if (-not $l) { return $null }
        Start-Sleep -Milliseconds 200
    }
    return @($l)[0].OwningProcess
}

# Startup-folder entries that start an agent, other than ours.
function Get-OldStartupEntries([string]$dir, [string]$ours) {
    $found = @()
    if (-not (Test-Path $dir)) { return $found }
    $sh = New-Object -ComObject WScript.Shell
    foreach ($f in Get-ChildItem $dir -File -ErrorAction SilentlyContinue) {
        if ($f.Name -eq $ours) { continue }
        $text = ''
        if ($f.Extension -eq '.lnk') {
            $l = $sh.CreateShortcut($f.FullName)
            $text = "$($l.TargetPath) $($l.Arguments)"
        } elseif ($f.Extension -match '^\.(vbs|bat|cmd|ps1|js)$') {
            $text = Get-Content $f.FullName -Raw -ErrorAction SilentlyContinue
        }
        if ($f.Name -match '(?i)timegate|pentadeck' -or $text -match '(?i)(timegate|pentadeck)_agent\.py') {
            $found += $f
        }
    }
    return $found
}

# ---------------------------------------------------------------------------------------------- firewall

function Get-LanConfig {
    $c = Get-NetIPConfiguration -ErrorAction SilentlyContinue |
        Where-Object { $_.IPv4DefaultGateway -and $_.NetAdapter.Status -eq 'Up' } | Select-Object -First 1
    if (-not $c) { return $null }
    $ip = @($c.IPv4Address | Where-Object { $_.IPAddress -notlike '169.254.*' })[0].IPAddress
    $prof = Get-NetConnectionProfile -InterfaceIndex $c.InterfaceIndex -ErrorAction SilentlyContinue
    return [pscustomobject]@{ IP = $ip; Index = $c.InterfaceIndex; Alias = $c.InterfaceAlias
                              Network = "$($prof.Name)"; Category = "$($prof.NetworkCategory)" }
}

# What the firewall already does for TCP <port> served by this pythonw.exe: AllowMask, the network
# profiles (1 Domain, 2 Private, 4 Public) where an enabled inbound rule lets it in; Blocks, the enabled
# inbound Block rules for this pythonw.exe (a dismissed "allow access?" prompt leaves those), which win
# over any Allow rule.
function Get-FirewallState([int]$port, [string]$pythonw) {
    $ids = @{}
    Get-NetFirewallPortFilter -ErrorAction SilentlyContinue |
        Where-Object { @($_.LocalPort) -contains "$port" } | ForEach-Object { $ids[$_.InstanceID] = 1 }
    Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue |
        Where-Object { "$($_.Program)" -eq $pythonw } | ForEach-Object { $ids[$_.InstanceID] = 1 }
    $mask = 0
    $blocks = @()
    foreach ($id in $ids.Keys) {
        $r = Get-NetFirewallRule -Name $id -ErrorAction SilentlyContinue
        if (-not $r -or "$($r.Enabled)" -ne 'True' -or "$($r.Direction)" -ne 'Inbound') { continue }
        $prog = "$(($r | Get-NetFirewallApplicationFilter).Program)"
        if ($prog -ne 'Any' -and $prog -ne $pythonw) { continue }
        $pf = $r | Get-NetFirewallPortFilter
        $portOk = ("$($pf.Protocol)" -in 'TCP', 'Any', '6') -and ("$($pf.LocalPort)" -eq 'Any' -or @($pf.LocalPort) -contains "$port")
        if ("$($r.Action)" -eq 'Block' -and $prog -eq $pythonw) { $blocks += $r.Name; continue }
        if ("$($r.Action)" -eq 'Allow' -and $portOk) {
            $pm = [int]$r.Profile
            if ($pm -eq 0) { $pm = 7 }
            $mask = $mask -bor $pm
        }
    }
    return [pscustomobject]@{ AllowMask = $mask; Blocks = $blocks }
}

# ---------------------------------------------------------------------------------------------- main

function Install-PentadeckAgent {
    $sandboxDir = $Sandbox
    if (-not $sandboxDir -and $env:PENTADECK_SANDBOX) { $sandboxDir = $env:PENTADECK_SANDBOX }
    if ($sandboxDir) {
        $sandboxDir = [IO.Path]::GetFullPath($sandboxDir)
        $installDir = Join-Path $sandboxDir 'Pentadeck'
        $startupDir = Join-Path $sandboxDir 'Startup'
        New-Item -ItemType Directory -Force $startupDir | Out-Null
        Note "Sandbox: installing under $sandboxDir (no real Startup entry, firewall or environment change)."
    } else {
        $installDir = Join-Path $env:LOCALAPPDATA 'Pentadeck'
        $startupDir = [Environment]::GetFolderPath('Startup')
    }
    $backupDir = Join-Path (Split-Path $installDir) 'Pentadeck-backup'
    $lnkName = 'Pentadeck agent.lnk'
    $agentPath = Join-Path $installDir 'pentadeck_agent.py'
    $settingsPath = Join-Path $installDir 'settings.json'

    # Questions only when no option was given (double-click, or irm | iex) and someone can answer.
    $given = @($PentadeckBound.Keys | Where-Object { $_ -ne 'Sandbox' })
    $nonInteractive = [Environment]::GetCommandLineArgs() | Where-Object { $_ -match '^-NonI' }
    $ask = (-not $Yes) -and ($given.Count -eq 0) -and [Environment]::UserInteractive -and (-not $nonInteractive)

    Say ''
    Say 'Pentadeck stats agent: installer for Windows'
    Say ''

    $py = Get-Python $ask
    Say "Python $($py.Version): $($py.Pythonw)"

    # Defaults: the previous install's settings, else this machine's.
    $prev = $null
    if (Test-Path $settingsPath) {
        try { $prev = Get-Content $settingsPath -Raw | ConvertFrom-Json } catch { $prev = $null }
    }
    $fixed = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.DriveType -eq 'Fixed' -and $_.IsReady } |
        ForEach-Object { $_.Name })
    $dName = $env:COMPUTERNAME; $dDisks = $fixed; $dPort = 8765; $dServices = @(); $dMic = $false
    if ($prev) {
        if ($prev.name) { $dName = $prev.name }
        if ($prev.disks) { $dDisks = @($prev.disks) }
        if ($prev.port) { $dPort = [int]$prev.port }
        if ($prev.services) { $dServices = @($prev.services) }
        $dMic = [bool]$prev.mic
    }
    # an option not given keeps the previous install's value
    if (-not $Name) { $Name = $dName }
    if ($Disks.Count -eq 0) { $Disks = $dDisks }
    if ($Port -eq 0) { $Port = $dPort }
    if (-not $PentadeckBound.ContainsKey('Services')) { $Services = $dServices }
    if (-not $PentadeckBound.ContainsKey('Mic')) { $Mic = [switch]$dMic }

    $cable = @(Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render' -ErrorAction SilentlyContinue |
        Where-Object { (Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue).DeviceState -eq 1 } |
        ForEach-Object { (Get-ItemProperty (Join-Path $_.PSPath 'Properties') -ErrorAction SilentlyContinue).'{a45c254e-df1c-4efd-8020-67d146a850e0},2' } |
        Where-Object { "$_" -match '^CABLE' })
    $savedToken = [Environment]::GetEnvironmentVariable('PENTADECK_TOKEN', 'User')

    if ($ask) {
        Say 'Press Enter to keep the value in [brackets].'
        $Name = Ask 'Name shown on the device' $Name
        $Disks = Split-List (Ask 'Drives to show' ($Disks -join ' '))
        $Port = [int](Ask 'Port' "$Port")
        $Services = Split-List (Ask 'Local services to check, as name:port (blank for none)' ($Services -join ' '))
        $micHint = 'needs VB-CABLE, which is not installed'
        if ($cable.Count) { $micHint = "VB-CABLE found: $($cable -join ', ')" }
        $Mic = [switch](AskYes "Use the device's microphone as a PC microphone? ($micHint)" ([bool]$Mic))
        if ($Mic -and -not $Token) {
            if ($savedToken) {
                $Token = Ask 'Device access token (Enter keeps the saved one)' ''
            } else {
                $Token = Ask 'Device access token (from the firmware''s secrets.h)' ''
            }
        }
    }
    $Disks = @(Split-List $Disks | ForEach-Object {
        $d = $_.TrimEnd('\')
        if ($d -match '^[A-Za-z]:?$') { $d = $d.Substring(0, 1).ToUpper() + ':' }
        $d + '\'
    })
    $Services = Split-List $Services
    foreach ($s in $Services) { if ($s -notmatch '^[^:\s]*:\d+$') { throw "A service is name:port, like web:8080 (got '$s')." } }
    if ($Port -lt 1 -or $Port -gt 65535) { throw "Port $Port is not a valid port." }
    if ($Mic -and -not $Token -and -not $savedToken) {
        throw '-Mic needs the device''s access token: pass -Token (it is in the firmware''s main/secrets.h).'
    }
    if ($Mic -and $cable.Count -eq 0) {
        Note 'No VB-CABLE playback device found. Install it from https://vb-audio.com/Cable/ (free);'
        Note 'the agent finds it by itself once it is there.'
    }

    # ---- files
    New-Item -ItemType Directory -Force $installDir | Out-Null
    $here = ''
    if ($PSScriptRoot) { $here = Join-Path $PSScriptRoot 'pentadeck_agent.py' }
    $tmp = Join-Path $installDir 'pentadeck_agent.py.new'
    if ($here -and (Test-Path $here)) {
        Copy-Item $here $tmp -Force
    } else {
        Say "Downloading $PentadeckAgentUrl"
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri $PentadeckAgentUrl -OutFile $tmp
    }
    if (-not (Test-PythonFile $py.Python $tmp)) {
        Remove-Item $tmp -Force
        throw 'The agent file is not valid Python (a failed download?).'
    }

    # ---- stop what holds the port: an older agent, or ours (upgrade)
    $old = @(Get-AgentProcesses | Where-Object { $_.Port -eq $Port -or (Test-Has $_.CommandLine $agentPath) })
    foreach ($p in $old) {
        if (-not (Test-Has $p.CommandLine $agentPath)) { Note "Stopping an older agent (pid $($p.Id)): $($p.CommandLine)" }
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    }
    $holder = Wait-PortFree $Port
    if ($holder) {
        $pn = (Get-Process -Id $holder -ErrorAction SilentlyContinue).ProcessName
        Remove-Item $tmp -Force
        throw "Port $Port is in use by $pn (pid $holder). Stop it, or pick another port with -Port."
    }
    Move-Item $tmp $agentPath -Force

    # ---- older Startup entries
    foreach ($f in Get-OldStartupEntries $startupDir $lnkName) {
        New-Item -ItemType Directory -Force $backupDir | Out-Null
        $dest = Join-Path $backupDir $f.Name
        if (Test-Path $dest) { $dest = Join-Path $backupDir ("{0}.{1:yyyyMMdd-HHmmss}" -f $f.Name, (Get-Date)) }
        Move-Item $f.FullName $dest
        Note "Replaced an older Startup entry: $($f.Name) (moved to $dest)"
    }

    # ---- token
    $env:PENTADECK_TOKEN_PREV = $env:PENTADECK_TOKEN
    if ($Mic) {
        if (-not $Token) { $Token = $savedToken }
        if ($sandboxDir) {
            Say 'Sandbox: not saving the token as a user environment variable.'
        } elseif ($Token -ne $savedToken) {
            [Environment]::SetEnvironmentVariable('PENTADECK_TOKEN', $Token, 'User')
            Say 'Saved the device token as your PENTADECK_TOKEN environment variable.'
        }
        $env:PENTADECK_TOKEN = $Token        # for the copy started below
    }

    # ---- start at logon, and now
    $argList = @('-I', (Quote-Arg $agentPath), '--name', (Quote-Arg $Name), '--port', "$Port")
    foreach ($d in $Disks) { $argList += @('--disk', (Quote-Arg $d)) }
    foreach ($s in $Services) { $argList += @('--service', (Quote-Arg $s)) }
    if ($Mic) { $argList += '--mic' }
    $arguments = $argList -join ' '

    $sh = New-Object -ComObject WScript.Shell
    $lnk = $sh.CreateShortcut((Join-Path $startupDir $lnkName))
    $lnk.TargetPath = $py.Pythonw
    $lnk.Arguments = $arguments
    $lnk.WorkingDirectory = $installDir
    $lnk.Description = "Pentadeck stats agent: this PC's stats on port $Port for the Pentadeck"
    $lnk.Save()
    Say "Starts at logon: $(Join-Path $startupDir $lnkName)"

    @{ name = $Name; disks = @($Disks); port = $Port; services = @($Services); mic = [bool]$Mic
       python = $py.Pythonw; arguments = $arguments } | ConvertTo-Json | Set-Content $settingsPath -Encoding UTF8

    # ---- firewall (before the agent starts listening, so Windows doesn't pop up its own prompt)
    $lan = Get-LanConfig
    $fw = @()
    $fwWhat = @()
    if (-not $NoFirewall) {
        $state = Get-FirewallState $Port $py.Pythonw
        if ($state.AllowMask -band 2) {
            Say "Firewall: TCP $Port is already allowed on private networks."
        } else {
            $fw += "Remove-NetFirewallRule -Group 'Pentadeck' -ErrorAction SilentlyContinue"
            $fw += "New-NetFirewallRule -DisplayName 'Pentadeck stats agent (TCP $Port)' -Group 'Pentadeck' " +
                   "-Description 'Lets the Pentadeck on your LAN read this PC''s stats' -Direction Inbound -Action Allow " +
                   "-Program '$($py.Pythonw -replace "'", "''")' -Protocol TCP -LocalPort $Port -RemoteAddress LocalSubnet -Profile Private,Domain | Out-Null"
            $fwWhat += "allow TCP $Port from your local network"
        }
        $blocks = @($state.Blocks)
        if ($blocks.Count) {
            Note "Firewall: $($blocks.Count) rule(s) block pythonw.exe (from a dismissed 'allow access' prompt); they would win over the allow rule."
            $fw += "Disable-NetFirewallRule -Name " + (($blocks | ForEach-Object { "'" + ($_ -replace "'", "''") + "'" }) -join ',')
            $fwWhat += 'turn off the rules that block pythonw.exe'
        }
        if ($lan -and $lan.Category -eq 'Public' -and -not ($state.AllowMask -band 4)) {
            Note "Your network `"$($lan.Network)`" is set to Public, so Windows blocks the device from reaching this PC."
            $priv = [bool]$MakePrivate
            if (-not $priv -and $ask) { $priv = AskYes 'Set it to Private (right for a home network)?' $true }
            if ($priv) {
                $fw += "Set-NetConnectionProfile -InterfaceIndex $($lan.Index) -NetworkCategory Private"
                $fwWhat += "set `"$($lan.Network)`" to Private"
            } else {
                Note 'Left as Public. Set it to Private in Settings > Network & internet > (your network) > Network profile type.'
            }
        }
    }
    if ($fw.Count) {
        $what = $fwWhat -join ', '
        if ($sandboxDir) {
            Say "Sandbox: would run as admin, to $($what):"
            $fw | ForEach-Object { Say "    $_" }
        } elseif (Invoke-Elevated ($fw -join '; ') $what) {
            Good "Firewall: done ($what)."
        } else {
            Note "Firewall: not changed. The device can't reach this PC until TCP $Port is allowed."
            Note 'Run this installer again and accept the admin prompt, or ask an administrator to.'
        }
    }

    Start-Process -FilePath $py.Pythonw -ArgumentList $arguments -WorkingDirectory $installDir -WindowStyle Hidden
    $env:PENTADECK_TOKEN = $env:PENTADECK_TOKEN_PREV
    Remove-Item Env:\PENTADECK_TOKEN_PREV -ErrorAction SilentlyContinue

    # ---- check
    $stats = $null
    for ($i = 0; $i -lt 20 -and -not $stats; $i++) {
        Start-Sleep -Milliseconds 500
        try { $stats = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/stats" -TimeoutSec 2 } catch { $stats = $null }
    }
    Say ''
    if (-not $stats) {
        Note "The agent did not answer on http://127.0.0.1:$Port/stats. To see why, run it in a console:"
        Say  "    $(Quote-Arg $py.Python) $arguments"
        throw 'The agent did not start.'
    }
    Good "The agent is running: $($stats.name), CPU $($stats.cpu.load)%, $(@($stats.disks).Count) drive(s)."
    $ip = '<this-pc-ip>'
    if ($lan -and $lan.IP) { $ip = $lan.IP }
    Say ''
    Say "In the device's settings page (Data card), add this source:"
    Good "    http://$($ip):$Port/stats"
    Say ''
    Say 'Remove it with uninstall-windows.ps1 (or uninstall-windows.cmd).'
}

$rc = 0
try {
    Install-PentadeckAgent
} catch {
    Write-Host ''
    Write-Host "Install failed: $($_.Exception.Message)" -ForegroundColor Red
    $rc = 1
}
if ($PSCommandPath) { exit $rc }
