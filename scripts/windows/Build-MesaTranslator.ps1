# SPDX-FileCopyrightText: 2026 KytyPS5 contributors
# SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
# SPDX-License-Identifier: GPL-2.0-only
# Adapted from KytyPS5 .github/workflows/xbox.yml, revision cd0c360aa7b432645b915c19fd1ceed394378b75.
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$mesaCommit = 'e24dc5bd1e7fe6101bdc866fb16a15a8fcae1aae'
$headersCommit = '9e393d6d8a3b30dcc6f2806ef604ec16a27b0d7e'
$patchHash = '326b1367c84959a363feeec28c4d1c3adcc6b935ff891dfde1ce1d87b44b29e1'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$patch = Join-Path $repo 'third_party\kyty_spirv_to_dxil\spirv_to_dxil.patch'
if ((Get-FileHash $patch -Algorithm SHA256).Hash.ToLowerInvariant() -ne $patchHash) {
    throw 'The audited Kyty Mesa patch changed; review and repin it before building.'
}
if (Test-Path $OutputDirectory) {
    if (@(Get-ChildItem $OutputDirectory -Force).Count -ne 0) {
        throw 'Use a new empty output directory, or the CI cache. Existing files are never removed.'
    }
}
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path $OutputDirectory).Path
$work = Join-Path ([System.IO.Path]::GetTempPath()) ('shad-mesa-' + [guid]::NewGuid())
New-Item -ItemType Directory $work | Out-Null
$mesa = Join-Path $work 'mesa'
& git init -q $mesa
if ($LASTEXITCODE) { throw 'Mesa git init failed.' }
# Source objects are verified by the exact Git commit, not a mutable branch/tag.
& git -C $mesa fetch -q --depth 1 https://github.com/chaotic-cx/mesa-mirror.git $mesaCommit
if ($LASTEXITCODE) { throw 'Mesa source fetch failed.' }
& git -C $mesa checkout -q FETCH_HEAD
if ($LASTEXITCODE) { throw 'Mesa checkout failed.' }
if ((& git -C $mesa rev-parse HEAD) -ne $mesaCommit) { throw 'Mesa revision mismatch.' }
& git -C $mesa apply --check $patch
if ($LASTEXITCODE) { throw 'Kyty patch does not apply to the pinned Mesa.' }
& git -C $mesa apply $patch
if ($LASTEXITCODE) { throw 'Mesa patch application failed.' }
$headers = Join-Path $mesa 'subprojects\DirectX-Headers-1.0'
& git init -q $headers
& git -C $headers fetch -q --depth 1 https://github.com/microsoft/DirectX-Headers.git $headersCommit
if ($LASTEXITCODE) { throw 'DirectX-Headers source fetch failed.' }
& git -C $headers checkout -q FETCH_HEAD
if ($LASTEXITCODE) { throw 'DirectX-Headers checkout failed.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -property installationPath
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
& meson setup "$mesa\build" $mesa --backend=ninja --wrap-mode=nodownload -Dbuildtype=release `
    -Db_ndebug=true -Db_vscrt=mt -Dplatforms=windows -Dgallium-drivers= -Dvulkan-drivers=microsoft-experimental `
    -Dspirv-to-dxil=true -Dllvm=disabled -Dglx=disabled -Degl=disabled -Dgbm=disabled `
    -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dspirv-tools=disabled -Dbuild-tests=false `
    -Dvideo-codecs= -Dmin-windows-version=10 -Dzlib=disabled -Dzstd=disabled
if ($LASTEXITCODE) { throw "Mesa configure failed; sources retained at $mesa" }
& ninja -C "$mesa\build" -j 2 src/microsoft/spirv_to_dxil/spirv_to_dxil.dll
if ($LASTEXITCODE) { throw "Mesa compile failed; sources retained at $mesa" }
& "$PSScriptRoot\Test-MesaUwpImports.ps1" -Binary "$mesa\build\src\microsoft\spirv_to_dxil\spirv_to_dxil.dll"
New-Item -ItemType Directory "$OutputDirectory\include", "$OutputDirectory\lib", "$OutputDirectory\bin", "$OutputDirectory\licenses" -Force | Out-Null
Copy-Item "$mesa\src\microsoft\spirv_to_dxil\spirv_to_dxil.h", "$mesa\src\microsoft\compiler\dxil_versions.h" "$OutputDirectory\include"
Copy-Item "$mesa\build\src\microsoft\spirv_to_dxil\spirv_to_dxil.lib" "$OutputDirectory\lib"
Copy-Item "$mesa\build\src\microsoft\spirv_to_dxil\spirv_to_dxil.dll" "$OutputDirectory\bin"
Copy-Item "$mesa\docs\license.rst" "$OutputDirectory\licenses\Mesa-license.rst"
Copy-Item "$repo\third_party\kyty_spirv_to_dxil\NOTICE.md", $patch "$OutputDirectory\licenses"
Copy-Item "$repo\LICENSES\GPL-2.0-only.txt" "$OutputDirectory\licenses"
@{ mesa_commit=$mesaCommit; patch_sha256=$patchHash; headers_commit=$headersCommit;
   donor_commit='cd0c360aa7b432645b915c19fd1ceed394378b75' } |
    ConvertTo-Json | Set-Content "$OutputDirectory\provenance.json" -Encoding utf8
Write-Host "Mesa translator built from source. Work tree retained at $mesa"
