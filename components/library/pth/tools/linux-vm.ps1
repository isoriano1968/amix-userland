param(
    [ValidateSet('status', 'start', 'stop', 'ssh')]
    [string]$Action = 'status',
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RemoteCommand
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vmRoot = Join-Path $projectRoot 'local/linuxvm'
$env:VBOX_USER_HOME = Join-Path $vmRoot 'vbox-config'
$vboxExe = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
switch ($Action) {
    'status' { & $vboxExe showvminfo pthreads-amix-linux --machinereadable }
    'start' { & $vboxExe startvm pthreads-amix-linux --type headless }
    'stop' { & $vboxExe controlvm pthreads-amix-linux acpipowerbutton }
    'ssh' {
        & ssh.exe -i (Join-Path $vmRoot 'id_ed25519') -p 2228 `
            -o BatchMode=yes -o StrictHostKeyChecking=accept-new `
            -o "UserKnownHostsFile=$(Join-Path $vmRoot 'known_hosts')" `
            builder@127.0.0.1 @RemoteCommand
    }
}
exit $LASTEXITCODE
