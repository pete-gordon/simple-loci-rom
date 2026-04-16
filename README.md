# Simple .TAP/.DSK selector ROM for LOCI

> Warning The LOCI ABI is in development. Unmatched ROM and firmware are not guaranteed to work together.

This is a simplified selector for LOCI, for users who just want to select a .tap or .dsk image and boot it. It is intended to be copied to the USB drive as "locirom.rp6502" so that you can easily go back to the full featured LOCI ROM by just renaming or removing it.

## Users
For information on how to use a LOCI device, please refer to the [LOCI User Manual](https://github.com/sodiumlb/loci-hardware/wiki/LOCI-User-Manual)

## Build instructions
The current setup requires a working install of CC65 and GNU Make
    sudo apt install cc65 make

The environmental variable CC65_HOME must point to the main CC65 SDK directory 
    export CC65_HOME=/usr/share/cc65

Build the ROM from the src/ directory
    cd src
    make

The output ROM file is `locirom.rp6502` to be copied to the USB drive.
