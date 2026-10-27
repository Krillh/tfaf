param()

$ErrorActionPreference = 'Stop'

$clang = (Get-Command clang -ErrorAction Stop).Source
$testDir = Join-Path $PSScriptRoot 'tests'
$outDir = Join-Path $PSScriptRoot 'out/native'
$testExe = Join-Path $outDir 'tfaf1-tests.exe'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$arguments = @(
  '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-pedantic',
  '-I', (Join-Path $PSScriptRoot 'include'),
  '-I', (Join-Path $PSScriptRoot 'dependencies'),
  (Join-Path $PSScriptRoot 'src/tfaf1.c'),
  (Join-Path $PSScriptRoot 'src/tfaf1_hal.c'),
  (Join-Path $testDir 'test_support.c'),
  (Join-Path $testDir 'test_core.c'),
  (Join-Path $testDir 'test_hal.c'),
  (Join-Path $testDir 'test_main.c'),
  '-o', $testExe
)

& $clang @arguments
if ($LASTEXITCODE -ne 0) { throw 'TFAF1 test compilation failed.' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'TFAF1 tests failed.' }
