param(
  [ValidateSet('native', 'wasm')]
  [string]$Target = 'native'
)

$ErrorActionPreference = 'Stop'
$clang = (Get-Command clang -ErrorAction Stop).Source
$outDir = Join-Path $PSScriptRoot (Join-Path 'out' $Target)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$common = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
            '-ffreestanding', '-fno-builtin', '-I', (Join-Path $PSScriptRoot 'include'))
if ($Target -eq 'wasm') {
  $common += @('-target', 'wasm32-unknown-unknown', '-nostdlib')
}

& $clang @common -c (Join-Path $PSScriptRoot 'src/tfaf1.c') `
  -o (Join-Path $outDir 'tfaf1.o')
if ($LASTEXITCODE -ne 0) { throw 'TFAF core compilation failed.' }
& $clang @common -c (Join-Path $PSScriptRoot 'src/tfaf1_hal.c') `
  -o (Join-Path $outDir 'tfaf1_hal.o')
if ($LASTEXITCODE -ne 0) { throw 'TFAF HAL adapter compilation failed.' }
