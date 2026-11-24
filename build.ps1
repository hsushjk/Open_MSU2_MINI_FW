# Build script for the standalone firmware (no make required).
#
#   .\build.ps1                 build the delivery firmware (proto) into build\
#   .\build.ps1 -Disasm         build, then dump disassembly
#   .\build.ps1 -Clean          remove the build directory

param(
    [switch]$Disasm,
    [switch]$Clean,
    [ValidateSet('proto')]
    [string]$Variant = 'proto'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$toolchain = if ($env:RISCV_TOOLCHAIN) { $env:RISCV_TOOLCHAIN } else { 'C:\Users\hsush\Tools\riscv\xpack\xpack-riscv-none-elf-gcc-14.2.0-3\bin' }
$cc      = Join-Path $toolchain 'riscv-none-elf-gcc.exe'
$objcopy = Join-Path $toolchain 'riscv-none-elf-objcopy.exe'
$size    = Join-Path $toolchain 'riscv-none-elf-size.exe'
$objdump = Join-Path $toolchain 'riscv-none-elf-objdump.exe'

$build = Join-Path $root 'build'
if ($Clean) {
    if (Test-Path $build) { Remove-Item -Recurse -Force $build }
    Write-Host 'cleaned'
    return
}
if (-not (Test-Path $build)) { New-Item -ItemType Directory -Path $build | Out-Null }

# Only one main() may be linked in: proto uses main_proto.c.
$main_name = 'main_proto.c'
$skip = @(Get-ChildItem (Join-Path $root 'src') -File -Filter 'main*.c' | ForEach-Object { $_.Name }) |
        Where-Object { $_ -ne $main_name }
$src = @(Get-ChildItem (Join-Path $root 'src') -File |
         Where-Object { $_.Extension -in '.c', '.S' -and $skip -notcontains $_.Name } |
         ForEach-Object { $_.FullName })

$tag = 'msu2_proto_fw'
$elf = Join-Path $build 'firmware.elf'
$hex = Join-Path $build ($tag + '.hex')
$bin = Join-Path $build ($tag + '.bin')

$arch   = @('-march=rv32imac_zicsr', '-mabi=ilp32')
$cflags = $arch + @('-Os', '-std=c11', '-ffreestanding', '-fno-common',
                    '-Wall', '-Wextra', '-ffunction-sections', '-fdata-sections', '-g')
$ldflags = $arch + @('-nostdlib', '-nostartfiles', '-Wl,--gc-sections',
                     '-Wl,--no-warn-rwx-segments', '-T', 'link.ld')

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
$hexfix = Join-Path $root 'tools\hexfix.js'
$node = (Get-Command node -ErrorAction SilentlyContinue).Source
if (-not $node) { $node = 'C:\Program Files\nodejs\node.exe' }
Write-Host ("   node: " + $node)
& $node $hexfix $hex $hex $bin
Write-Host ("   hexfix exit: " + $LASTEXITCODE)
if ($LASTEXITCODE -ne 0) { throw "hexfix failed ($LASTEXITCODE)" }

"$tag.hex: $((Get-Item $hex).Length) bytes, $tag.bin: $((Get-Item $bin).Length) bytes"

if ($Disasm) {
    Write-Host '== disassembly'
    & $objdump -d $elf
}