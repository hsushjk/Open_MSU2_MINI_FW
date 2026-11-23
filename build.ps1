# Build script for the standalone firmware (no make required).
#
#   .\build.ps1                 build the bars demo into build\
#   .\build.ps1 -Variant erase  build the external flash eraser instead
#   .\build.ps1 -Disasm         build, then dump disassembly
#   .\build.ps1 -Clean          remove the build directory

param(
    [switch]$Disasm,
    [switch]$Clean,
    [ValidateSet('bars', 'erase', 'dump', 'probe', 'usb', 'stage', 'csrtest', 'min', 'mincsr', 'stage_nousb', 'stage_usb', 'ladder', 'usbstage', 'usbstep', 'usbregs', 'usbforce', 'host','proto', 'protorx', 'ping','direct', 'isrmin', 'poll', 'all', 'timed')]
    [string]$Variant = 'bars'
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

# main.c and main_erase.c both define main(); keep exactly one of them
if ($Variant -eq 'all') { foreach ($v in @('bars','erase','dump','probe','usb')) { & $PSCommandPath -Variant $v; if ($LASTEXITCODE -ne 0) { exit 1 } }; exit 0 }
$main_name = switch ($Variant) {
    'erase' { 'main_erase.c' }
    'dump'  { 'main_dump.c' }
    'probe' { 'main_probe.c' }
'usb'   { 'main_usb.c' }
'stage' { 'main_stage.c' }
'csrtest' { 'main_csr.c' }
'min' { 'main_min.c' }
'mincsr' { 'main_min.c' }
'stage_nousb' { 'main_stage_nousb.c' }
'stage_usb' { 'main_stage_usb.c' }
'ladder' { 'main_ladder.c' }
    'usbstep' { 'main_usbstep.c' } 'usbregs' { 'main_usbregs.c' } 'usbforce' { 'main_usbforce.c' } 'host' { 'main_usbhost.c' } 'proto' { 'main_proto.c' } 'protorx' { 'main_protorx.c' } 'ping' { 'main_ping.c' } 'direct' { 'main_usbdirect.c' } 'isrmin' { 'main_usbforce.c' } 'poll' { 'main_poll.c' } 'timed' { 'main_timed.c' }
    'usbstage' { 'main_usbstage.c' }
    default { 'main.c' }
}
$skip = @(Get-ChildItem (Join-Path $root 'src') -File -Filter 'main*.c' | ForEach-Object { $_.Name }) |
        Where-Object { $_ -ne $main_name }
$src = @(Get-ChildItem (Join-Path $root 'src') -File |
         Where-Object { $_.Extension -in '.c', '.S' -and $skip -notcontains $_.Name } |
         ForEach-Object { $_.FullName })

$tag = switch ($Variant) { 'erase' { 'msu2_erase_fw' } 'dump' { 'msu2_dump_fw' } 'probe' { 'msu2_probe_fw' } 'usb' { 'msu2_usb_fw' } 'stage' { 'msu2_stage_fw' } 'csrtest' { 'msu2_csr_fw' } 'min' { 'msu2_min_fw' } 'mincsr' { 'msu2_mincsr_fw' } 'stage_nousb' { 'msu2_stage_nousb_fw' } 'stage_usb' { 'msu2_stage_usb_fw' } 'ladder' { 'msu2_ladder_fw' } 'usbstep' { 'msu2_usbstep_fw' } 'usbregs' { 'msu2_usbregs_fw' } 'usbforce' { 'msu2_usbforce_fw' } 'host' { 'msu2_host_fw' } 'proto' { 'msu2_proto_fw' } 'protorx' { 'msu2_protorx_fw' } 'ping' { 'msu2_ping_fw' } 'direct' { 'msu2_direct_fw' } 'isrmin' { 'msu2_isrmin_fw' } 'poll' { 'msu2_poll_fw' } 'timed' { 'msu2_timed_fw' } 'usbstage' { 'msu2_usbstage_fw' } default { 'msu2_bars_fw' } }
$elf = Join-Path $build 'firmware.elf'
$hex = Join-Path $build ($tag + '.hex')
$bin = Join-Path $build ($tag + '.bin')

$arch   = @('-march=rv32imac_zicsr', '-mabi=ilp32')
$cflags = $arch + @('-Os', '-std=c11', '-ffreestanding', '-fno-common',
                    '-Wall', '-Wextra', '-ffunction-sections', '-fdata-sections', '-g')
$ldflags = $arch + @('-nostdlib', '-nostartfiles', '-Wl,--gc-sections', '-T', 'link.ld')

Write-Host "== compile + link ($Variant)"
if ($Variant -eq 'csrtest' -or $Variant -eq 'min') { $cflags += '-DSTARTUP_NO_CSR' }
if ($Variant -eq 'isrmin') { $cflags += '-DMIN_ISR' }
if ($Variant -eq 'poll') { $cflags += '-DPOLL_MODE' }
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