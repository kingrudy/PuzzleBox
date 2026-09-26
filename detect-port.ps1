# Prints the first non-Bluetooth serial port PlatformIO can see (typically
# the CP210x/CH340/FTDI USB-UART bridge on whichever board is plugged in).
# Exits 1 with no output if nothing matches. Used by build-upload.bat when
# no COM port is given explicitly.
#
# If you have more than one board plugged in at once, this will just grab
# the first one it finds in `pio device list` order — pass an explicit port
# to build-upload.bat instead in that case.

param(
    [Parameter(Mandatory = $true)]
    [string]$PioPath
)

$lines = & $PioPath device list 2>$null
$currentPort = $null

foreach ($line in $lines) {
    if ($line -match '^(COM\d+)$') {
        $currentPort = $Matches[1]
    } elseif ($line -match '^Description:\s*(.+)$') {
        $desc = $Matches[1]
        if ($currentPort -and $desc -notmatch 'Bluetooth') {
            Write-Output $currentPort
            exit 0
        }
        $currentPort = $null
    }
}

exit 1
