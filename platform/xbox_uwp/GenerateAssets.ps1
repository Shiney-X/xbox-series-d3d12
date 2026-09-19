$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$assetDirectory = Join-Path $PSScriptRoot 'Assets'
New-Item -ItemType Directory -Force -Path $assetDirectory | Out-Null

function New-ProbeAsset {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][int]$Width,
        [Parameter(Mandatory = $true)][int]$Height
    )

    $bitmap = [System.Drawing.Bitmap]::new($Width, $Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(16, 24, 32))
        $inset = [Math]::Max(2, [Math]::Floor([Math]::Min($Width, $Height) * 0.16))
        $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(25, 190, 93))
        try {
            $graphics.FillRectangle($brush, $inset, $inset, $Width - (2 * $inset), $Height - (2 * $inset))
        } finally {
            $brush.Dispose()
        }

        $bitmap.Save((Join-Path $assetDirectory $Name), [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

New-ProbeAsset -Name 'StoreLogo.png' -Width 50 -Height 50
New-ProbeAsset -Name 'Square44x44Logo.png' -Width 44 -Height 44
New-ProbeAsset -Name 'Square150x150Logo.png' -Width 150 -Height 150
New-ProbeAsset -Name 'Wide310x150Logo.png' -Width 310 -Height 150
New-ProbeAsset -Name 'SplashScreen.png' -Width 620 -Height 300
