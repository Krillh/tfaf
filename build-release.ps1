param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('gen1')]
  [string]$Generation
)

$ErrorActionPreference = 'Stop'

$clang = (Get-Command clang -ErrorAction Stop).Source
$llvmAr = (Get-Command llvm-ar -ErrorAction Stop).Source
$clangVersionOutput = & $clang --version
if ($LASTEXITCODE -ne 0) { throw 'Could not query the Clang version.' }
$llvmArVersionOutput = & $llvmAr --version
if ($LASTEXITCODE -ne 0) { throw 'Could not query the llvm-ar version.' }
$clangVersion = ($clangVersionOutput -join "`n").Trim()
$llvmArVersion = ($llvmArVersionOutput -join "`n").Trim()
$Generation = $Generation.ToLowerInvariant()
$generationConfig = @{
  gen1 = @{
    format_name = 'TFAF1'
    generation_number = 1
    api_prefix = 'tfaf1'
    specification = 'docs/TFAF1.md'
    package_readme = 'docs/driver-package.md'
  }
}[$Generation]
if ($null -eq $generationConfig) { throw "No build configuration for $Generation." }

$generationDir = Join-Path $PSScriptRoot $Generation
$buildDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "build/$Generation"))
$releaseRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'release'))
$releaseName = "tfaf-$Generation-wasm"
$apiPrefix = $generationConfig.api_prefix
$formatName = $generationConfig.format_name
$archiveName = "$apiPrefix-wasm.a"
$packageDir = Join-Path $releaseRoot $releaseName
$zipPath = Join-Path $releaseRoot "$releaseName.zip"
$stageName = ".$releaseName-stage-$([guid]::NewGuid().ToString('N'))"
$stageDir = Join-Path $releaseRoot $stageName
$stageZip = Join-Path $releaseRoot "$stageName.zip"

function Assert-ReleaseChild([string]$Path) {
  $fullPath = [IO.Path]::GetFullPath($Path)
  $prefix = $releaseRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) +
            [IO.Path]::DirectorySeparatorChar
  if (-not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to modify a path outside the release directory: $fullPath"
  }
  return $fullPath
}

New-Item -ItemType Directory -Force -Path $buildDir, $releaseRoot | Out-Null

# Produce LLVM bitcode objects so consumers can perform LTO at final link time.
$common = @(
  '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
  '-ffreestanding', '-fno-builtin', '-flto',
  '-target', 'wasm32-unknown-unknown', '-nostdlib',
  '-I', (Join-Path $generationDir 'include')
)

$objects = @()
foreach ($unit in @($apiPrefix, "${apiPrefix}_hal")) {
  $source = Join-Path $generationDir "src/$unit.c"
  $object = Join-Path $buildDir "$unit-wasm.o"
  & $clang @common -c $source -o $object
  if ($LASTEXITCODE -ne 0) { throw "Compilation failed for $source." }
  $objects += $object
}

$archive = Join-Path $buildDir $archiveName
if (Test-Path -LiteralPath $archive) {
  Remove-Item -LiteralPath $archive -Force
}
& $llvmAr rcs $archive @objects
if ($LASTEXITCODE -ne 0) { throw "Archive creation failed for $archive." }

New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
New-Item -ItemType Directory -Force -Path `
  (Join-Path $stageDir 'include'), `
  (Join-Path $stageDir 'lib'), `
  (Join-Path $stageDir 'docs') | Out-Null

Copy-Item -LiteralPath $archive -Destination (Join-Path $stageDir "lib/$archiveName")
Copy-Item -LiteralPath (Join-Path $generationDir "include/$apiPrefix.h") `
  -Destination (Join-Path $stageDir "include/$apiPrefix.h")
Copy-Item -LiteralPath (Join-Path $generationDir "include/${apiPrefix}_hal.h") `
  -Destination (Join-Path $stageDir "include/${apiPrefix}_hal.h")
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'LICENSE') `
  -Destination (Join-Path $stageDir 'LICENSE')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot $generationConfig.specification) `
  -Destination (Join-Path $stageDir "docs/$([IO.Path]::GetFileName($generationConfig.specification))")
Copy-Item -LiteralPath (Join-Path $PSScriptRoot $generationConfig.package_readme) `
  -Destination (Join-Path $stageDir 'README.md')

$archiveHash = (Get-FileHash -LiteralPath (Join-Path $stageDir "lib/$archiveName") `
  -Algorithm SHA256).Hash.ToLowerInvariant()
"$archiveHash  lib/$archiveName" | Set-Content -LiteralPath `
  (Join-Path $stageDir 'SHA256SUMS.txt') -Encoding ascii

$metadata = [ordered]@{
  metadata_schema_version = 1
  format = $formatName
  driver_generation = $Generation
  format_generation_number = $generationConfig.generation_number
  release_name = $releaseName
  target_triple = 'wasm32-unknown-unknown'
  archive = "lib/$archiveName"
  archive_type = 'LLVM LTO static archive'
  archive_members = @("${apiPrefix}-wasm.o", "${apiPrefix}_hal-wasm.o")
  source_files = @("$Generation/src/$apiPrefix.c", "$Generation/src/${apiPrefix}_hal.c")
  lto = $true
  clang_version = $clangVersion
  llvm_ar_version = $llvmArVersion
  compile_flags = @(
    '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
    '-ffreestanding', '-fno-builtin', '-flto',
    '-target wasm32-unknown-unknown', '-nostdlib'
  )
  archive_size_bytes = (Get-Item -LiteralPath (Join-Path $stageDir "lib/$archiveName")).Length
  archive_sha256 = $archiveHash
}
$metadataJson = ConvertTo-Json -InputObject $metadata -Depth 4
[IO.File]::WriteAllText(
  (Join-Path $stageDir 'BUILD-METADATA.json'),
  $metadataJson + [Environment]::NewLine,
  [Text.UTF8Encoding]::new($false)
)

Compress-Archive -Path (Join-Path $stageDir '*') -DestinationPath $stageZip `
  -CompressionLevel Optimal

# Replace only this named package and zip, after building the complete staging copy.
$safePackageDir = Assert-ReleaseChild $packageDir
$safeZipPath = Assert-ReleaseChild $zipPath
$safeStageDir = Assert-ReleaseChild $stageDir
$safeStageZip = Assert-ReleaseChild $stageZip
if (Test-Path -LiteralPath $safePackageDir) {
  Remove-Item -LiteralPath $safePackageDir -Recurse -Force
}
if (Test-Path -LiteralPath $safeZipPath) {
  Remove-Item -LiteralPath $safeZipPath -Force
}
Move-Item -LiteralPath $safeStageDir -Destination $safePackageDir
Move-Item -LiteralPath $safeStageZip -Destination $safeZipPath

Write-Host "Created $Generation wasm LTO archive: $archive"
Write-Host "Created release package: $packageDir"
Write-Host "Created release zip: $zipPath"
