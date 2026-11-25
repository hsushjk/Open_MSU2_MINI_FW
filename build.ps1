# Build script for the standalone firmware (no make required).
#
#   pwsh ./build.ps1                 build the delivery firmware (proto) into build/
#   pwsh ./build.ps1 -Disasm         build, then dump disassembly
#   pwsh ./build.ps1 -Clean          remove the build directory
#   pwsh ./build.ps1 -Toolchain DIR  use the toolchain whose binaries live in DIR
#
# The toolchain is xpack riscv-none-elf-gcc 14.2.0-3. Resolution order:
#   -Toolchain, $env:RISCV_TOOLCHAIN, the documented Windows default path,
#   then riscv-none-elf-gcc from PATH. Only a "bin" directory is needed.

param(
    [switch]$Disasm,
    [switch]$Clean,
    [string]$Toolchain = '',
    [ValidateSet('proto')]
    [string]$Variant = 'proto'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$onWindows = ($env:OS -eq 'Windows_NT')
$exeSuffix = if ($onWindows) { '.exe' } else { '' }

function New-Item-if-missing([string]$path) {
    if (-not (Test-Path $path)) { New-Item -ItemType Directory -Path $path | Out-Null }
}

$build = Join-Path $root 'build'
if ($Clean) {
    if (Test-Path $build) { Remove-Item -Recurse -Force $build }
    Write-Host 'cleaned'
    return
}
New-Item-if-missing $build

# --- toolchain -------------------------------------------------------------
$ccName = 'riscv-none-elf-gcc' + $exeSuffix
$candidates = @()
if ($Toolchain) { $candidates += $Toolchain }
if ($env:RISCV_TOOLCHAIN) { $candidates += $env:RISCV_TOOLCHAIN }
if ($onWindows) {
    $candidates += 'C:\Users\hsush\Tools\riscv\xpack\xpack-riscv-none-elf-gcc-14.2.0-3\bin'
}

$toolDir = $null
foreach ($dir in $candidates) {
    if ($dir -and (Test-Path (Join-Path $dir $ccName))) { $toolDir = $dir; break }
}
if (-not $toolDir) {
    $cmd = Get-Command $ccName -ErrorAction SilentlyContinue
    if ($cmd) { $toolDir = Split-Path -Parent $cmd.Source }
}
if (-not $toolDir) {
    throw ("riscv toolchain not found: set RISCV_TOOLCHAIN (or pass -Toolchain) to the " +
           "directory that contains $ccName")
}

$cc      = Join-Path $toolDir $ccName
$objcopy = Join-Path $toolDir ('riscv-none-elf-objcopy' + $exeSuffix)
$size    = Join-Path $toolDir ('riscv-none-elf-size' + $exeSuffix)
$objdump = Join-Path $toolDir ('riscv-none-elf-objdump' + $exeSuffix)

# --- sources --------------------------------------------------------------
# Only one main() may be linked in: proto uses main_proto.c.
$srcDir = Join-Path $root 'src'
$main_name = 'main_proto.c'
$skip = @(Get-ChildItem $srcDir -File -Filter 'main*.c' | ForEach-Object { $_.Name }) |
        Where-Object { $_ -ne $main_name }
$src = @(Get-ChildItem $srcDir -File |
         Where-Object { $_.Extension -in '.c', '.S' -and $skip -notcontains $_.Name } |
         ForEach-Object { $_.FullName })
if ($src.Count -eq 0) { throw "no sources under $srcDir" }

$tag = 'msu2_proto_fw'
$elf = Join-Path $build 'firmware.elf'
$hex = Join-Path $build ($tag + '.hex')
$bin = Join-Path $build ($tag + '.bin')

$arch   = @('-march=rv32imac_zicsr', '-mabi=ilp32')
$cflags = $arch + @('-Os', '-std=c11', '-ffreestanding', '-fno-common',
                    '-Wall', '-Wextra', '-ffunction-sections', '-fdata-sections', '-g')
$ldflags = $arch + @('-nostdlib', '-nostartfiles', '-Wl,--gc-sections',
                     '-Wl,--no-warn-rwx-segments', '-T', 'link.ld')

Write-Host "== toolchain: $toolDir"
Write-Host "== compile + link ($Variant)"
& $cc @cflags @ldflags -o $elf @src
if ($LASTEXITCODE -ne 0) { throw "gcc failed ($LASTEXITCODE)" }
& $size $elf

Write-Host '== objcopy'
& $objcopy -O ihex $elf $hex
if ($LASTEXITCODE -ne 0) { throw "objcopy hex failed ($LASTEXITCODE)" }
& $objcopy -O binary $elf $bin
if ($LASTEXITCODE -ne 0) { throw "objcopy bin failed ($LASTEXITCODE)" }

Write-Host '== hexfix'
# Fill inter-section holes with 0xFF and use CRLF, or WCHISPStudio may fail
# to convert the hex (and then the chip boots nothing).
$hexfix = Join-Path $root 'tools/hexfix.js'
$node = (Get-Command node -ErrorAction SilentlyContinue).Source
if (-not $node -and $onWindows) { $node = 'C:\Program Files\nodejs\node.exe' }
if (-not $node) { throw 'node not found: hexfix.js needs Node.js on PATH' }
Write-Host ("   node: " + $node)
& $node $hexfix $hex $hex $bin
Write-Host ("   hexfix exit: " + $LASTEXITCODE)
if ($LASTEXITCODE -ne 0) { throw "hexfix failed ($LASTEXITCODE)" }

"$tag.hex: $((Get-Item $hex).Length) bytes, $tag.bin: $((Get-Item $bin).Length) bytes"

if ($Disasm) {
    Write-Host '== disassembly'
    & $objdump -d $elf
}