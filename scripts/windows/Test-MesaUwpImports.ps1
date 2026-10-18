# SPDX-FileCopyrightText: 2026 KytyPS5 contributors
# SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
# SPDX-License-Identifier: GPL-2.0-only
# Adapted from KytyPS5 uwp.ps1 Test-Imports, cd0c360aa7b432645b915c19fd1ceed394378b75.
param([Parameter(Mandatory=$true)][string]$Binary)
$ErrorActionPreference = 'Stop'
$sdkLib = Join-Path $env:WindowsSdkDir ('Lib\' + $env:WindowsSDKVersion.TrimEnd('\') + '\um\x64\WindowsApp.lib')
if (-not (Test-Path $sdkLib)) { throw "Cannot audit imports: missing $sdkLib" }
$symbols = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($line in (& dumpbin /nologo /linkermember:1 $sdkLib)) {
    if ($line -match '__imp_(\S+)\s*$') { [void]$symbols.Add($Matches[1]) }
}
if ($LASTEXITCODE -or $symbols.Count -eq 0) { throw 'Cannot read WindowsApp import symbols.' }
$dll = ''
$seen = 0
$missing = [System.Collections.Generic.List[string]]::new()
foreach ($line in (& dumpbin /nologo /imports $Binary)) {
    if ($line -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { $dll = $Matches[1]; continue }
    if ($dll -and $line -match '^\s+[0-9A-Fa-f]+\s+([A-Za-z_?][A-Za-z0-9_?@$]*)\s*$') {
        $symbol = $Matches[1]
        $seen++
        if ($dll -match '^api-ms-win-crt-' -or $symbols.Contains($symbol)) { continue }
        $missing.Add("${dll}:$symbol")
    }
    if ($dll -and $line -match 'Ordinal\s+\d+') { $missing.Add("${dll}:ordinal_import_not_certified") }
}
if ($LASTEXITCODE -or $seen -eq 0) { throw 'Cannot parse Mesa DLL imports; refusing an unaudited package.' }
if ($missing.Count) { throw ('Mesa imports unavailable in WindowsApp.lib: ' + ($missing -join ', ')) }
Write-Host "Mesa UWP import audit passed ($seen imports). Runtime behavior still needs physical Xbox validation."
