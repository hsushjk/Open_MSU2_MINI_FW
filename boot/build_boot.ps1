param(
    [switch]$Clean
)

# 构建常驻引导（0x0000，4 KB），产出 build_boot\boot.hex
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$toolchain = if ($env:RISCV_TOOLCHAIN) { $env:RISCV_TOOLCHAIN } else { 'C:\Users\hsush\Tools\riscv\xpack\xpack-riscv-none-elf-gcc-14.2.0-3\bin' }
$cc      = Join-Path $toolchain 'riscv-none-elf-gcc.exe'
$objcopy = Join-Path $toolchain 'riscv-none-elf-objcopy.exe'
$size    = Join-Path $toolchain 'riscv-none-elf-size.exe'

$out = Join-Path $root '..\build_boot'
if ($Clean) {
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    Write-Host 'cleaned'
    return
}
if (-not (Test-Path $out)) { New-Item -ItemType Directory -Path $out | Out-Null }

$arch = @('-march=rv32imac_zicsr', '-mabi=ilp32')
$cflags = $arch + @('-Os', '-std=c11', '-ffreestanding', '-fno-common', '-Wall', '-Wextra',
                    '-ffunction-sections', '-fdata-sections', '-g')
$ldflags = $arch + @('-nostdlib', '-nostartfiles', '-Wl,--gc-sections', '-T', (Join-Path $root 'link_boot.ld'))

$elf = Join-Path $out 'boot.elf'
$hex = Join-Path $out 'boot.hex'

Write-Host '== compile bootloader'
& $cc @cflags @ldflags -o $elf (Join-Path $root 'boot.c') (Join-Path $root 'start.S')
if ($LASTEXITCODE -ne 0) { throw "gcc failed ($LASTEXITCODE)" }
& $size $elf
& $objcopy -O ihex $elf $hex
if ($LASTEXITCODE -ne 0) { throw "objcopy failed ($LASTEXITCODE)" }
"boot hex: $((Get-Item $hex).Length) bytes"