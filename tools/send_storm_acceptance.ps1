#requires -Version 5.1
<# One wired Windows sender for the isolated GE1/0/2 / VLAN4090 experiment.
   Default mode validates only. Does not change IP, routes, firewall or adapters.
   All send modes have fixed destinations, rates, packet and time limits. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$InterfaceAlias,
    [ValidateSet('Validate','Probe','Send')][string]$Mode = 'Validate',
    [switch]$IsolatedVlanConfirmed,
    [switch]$BdiReady,
    [string]$OutputPath = '.\storm-sender-result.json'
)
$ErrorActionPreference = 'Stop'
$sourceIp = '198.18.0.2'
$destination = '198.18.0.255'
$destinationPort = 49000
$runId = [Guid]::NewGuid().ToString('N')
$client = $null
$sent = 0
$clock = [Diagnostics.Stopwatch]::StartNew()
$result = [ordered]@{
    schema_version=1; kind='bounded_windows_broadcast_sender'; run_id=$runId
    mode=$Mode; interface_alias=$InterfaceAlias; source=$sourceIp
    destination=$destination; destination_port=$destinationPort
    expected_switch_port='GigabitEthernet1/0/2'; required_switch_vlan=4090
    sent_packets=0; sender_stopped=$false; stop_reason='not_started'
    bdi_isolation_verified=$false
    note='Socket sends do not prove switch receipt or BDI isolation. Verify ESP status separately.'
}
try {
    $adapters = @(Get-NetAdapter | Where-Object { $_.Name -eq $InterfaceAlias })
    if ($adapters.Count -ne 1) { throw 'Exact adapter name not found. Run Get-NetAdapter first.' }
    $adapter = $adapters[0]
    if ($adapter.Status -ne 'Up') { throw 'Test adapter must be Up before starting.' }
    $ips = @(Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4)
    if ($ips.Count -ne 1 -or $ips[0].IPAddress -ne $sourceIp -or $ips[0].PrefixLength -ne 24) {
        throw 'Require only 198.18.0.2/24 on the test adapter. No IP settings were changed.'
    }
    $gateways = @(Get-NetRoute -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue)
    if ($gateways.Count -ne 0) { throw 'Test adapter must have no IPv4 default gateway.' }
    $result['interface_index'] = [int]$adapter.ifIndex
    if ($Mode -eq 'Validate') {
        $result['stop_reason'] = 'validation_only_no_packets'
        Write-Host 'Validation passed. No packets sent. Switch VLAN4090 must still be independently verified.'
    } else {
        if (-not $IsolatedVlanConfirmed) { throw 'Confirm Console verified port 2 alone in isolated VLAN4090 before sending.' }
        if ($Mode -eq 'Send' -and -not $BdiReady) { throw 'Require ESP r18 storm_acceptance.state=ready before Send.' }
        $pps = 5; $seconds = 5
        if ($Mode -eq 'Send') { $pps = 50; $seconds = 90 }
        $maximumPackets = $pps * $seconds
        $result['rate_limit_pps'] = $pps
        $result['maximum_seconds'] = $seconds
        $result['maximum_packets'] = $maximumPackets
        $client = New-Object System.Net.Sockets.UdpClient
        $client.Client.Bind((New-Object System.Net.IPEndPoint([Net.IPAddress]::Parse($sourceIp),0)))
        $client.EnableBroadcast = $true
        $client.Ttl = 1
        $client.DontFragment = $true
        $client.Client.SendTimeout = 250
        $endpoint = New-Object System.Net.IPEndPoint([Net.IPAddress]::Parse($destination),$destinationPort)
        $clock.Restart()
        $utcDeadline = [DateTime]::UtcNow.AddSeconds($seconds)
        $nextAdapterCheck = 0.0
        $result['started_utc'] = [DateTime]::UtcNow.ToString('o')
        $result['stop_reason'] = 'hard_limit'
        Write-Host "Run $runId : at most $pps packets/s, $seconds seconds, $maximumPackets packets. Ctrl+C stops."
        while ($sent -lt $maximumPackets -and $clock.Elapsed.TotalSeconds -lt $seconds -and [DateTime]::UtcNow -lt $utcDeadline) {
            if ($clock.Elapsed.TotalSeconds -ge $nextAdapterCheck) {
                $live = Get-NetAdapter | Where-Object { $_.ifIndex -eq $adapter.ifIndex }
                if ($null -eq $live -or $live.Status -ne 'Up') { $result['stop_reason']='adapter_link_down'; break }
                $nextAdapterCheck = $clock.Elapsed.TotalSeconds + 0.5
            }
            $text = ('ESP-STORM-LAB-1|run=' + $runId + '|seq=' + $sent).PadRight(128,'.')
            $bytes = [Text.Encoding]::ASCII.GetBytes($text)
            $before = $clock.Elapsed.TotalSeconds
            $written = $client.Send($bytes,$bytes.Length,$endpoint)
            if ($written -ne $bytes.Length) { throw 'Partial datagram send; stopping without retry.' }
            $sent++
            # Never catch up after a pause by bursting queued packets.
            $sleepMs = [Math]::Ceiling((1.0/$pps - ($clock.Elapsed.TotalSeconds-$before))*1000.0)
            if ($sleepMs -gt 0) { Start-Sleep -Milliseconds ([int]$sleepMs) }
        }
    }
} catch {
    $result['stop_reason'] = 'error_no_retry'
    $result['error'] = $_.Exception.Message
    Write-Warning $_.Exception.Message
} finally {
    if ($null -ne $client) { $client.Close(); $client.Dispose() }
    $result['sender_stopped'] = $true
    $result['sent_packets'] = $sent
    $result['elapsed_seconds'] = [Math]::Round($clock.Elapsed.TotalSeconds,3)
    $result['effective_pps'] = [Math]::Round($sent / [Math]::Max(0.001,$clock.Elapsed.TotalSeconds),2)
    $result['stopped_utc'] = [DateTime]::UtcNow.ToString('o')
    $json = $result | ConvertTo-Json -Depth 5
    $json | Set-Content -LiteralPath $OutputPath -Encoding UTF8
    Write-Host 'SENDER STOPPED. No automatic restart or port restore.'
    Write-Output $json
}
