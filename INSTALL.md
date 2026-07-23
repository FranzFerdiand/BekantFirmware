# Building and uploading

These instructions assume a macOS/Linux host, an PICkit 3 programmer, and a
fresh out-of-the-box BEKANT controller that you are about to overwrite.

**Flashing the firmware is irreversible without a backup of the original
`.hex`**, so read the [Disclaimer](README.md#disclaimer) first.

> **Before you start** — please read
> [Hardware variants & code protection](#hardware-variants--code-protection)
> below. Newer BEKANT desks (manufactured 2019 or later) have code protection
> enabled on the PIC, which means you cannot read the original firmware back
> from the chip.
>
> If yours is a newer unit, back up with a known-good dump or release instead
> (the hex release from ivanwick's project, or the backup contributed by another
> user) and accept that you may not be able to roll back to the OEM firmware.

## 1. Get the toolchain

You need Microchip's XC8 C compiler (the free one is fine):

- Download XC8: <https://www.microchip.com/mplab/compilers>

Confirm installation works (`xc8-cc` should be added to the PATH during
installation):

```sh
xc8-cc --version
```

## 2. Build the new firmware

Make sure you have GNU Make installed. It should come with most/all Linux
distros (otherwise search for the specific `make` package and install it), while
on macOS you might need to install in one of two ways:

1. Xcode Command Line Tools: `xcode-select --install`
2. Homebrew: `brew install make`

Then you should be able to simply run:

```sh
make
```

The build should give you `dist/bekantfirmware.hex` (about 16 KB in size).

## 3. Disassemble the BEKANT controller box

The BEKANT controller is a small plastic box attached to the desk, with two
buttons on the front and a power cord at the back.

1. **Unplug the desk from mains.** Capacitors inside the controller hold enough
   charge to damage your programmer and the PIC, so after unplugging the desk
   press the up/down buttons to fully discharge them.
2. Now open the controller enclosure. IKEA has welded the plastic back plate, so
   do not expect the case to come apart easily. Two excellent community guides
   for getting inside the case without destroying it were originally collected
   in
   [ivanwick/bekantfirmware issue #11](https://github.com/ivanwick/bekantfirmware/issues/11)
   - YouTube walkthrough: <https://www.youtube.com/watch?v=jCPlM2KYwDQ>
   - iFixit teardown:
     <https://www.ifixit.com/Teardown/Ikea+Bekant+Controller+Teardown/142516>
3. Locate the **ICSP header** on the PCB — a 6-pin (2×3) header labelled `ICSP`,
   `PICKIT`, or similar. The original project's
   [Installation Guide](https://github.com/ivanwick/bekantfirmware/wiki/Installation-Guide)
   has photos of the header location.
4. Plug your PICkit programmer into the ICSP header. The arrow / notch on the
   PICkit connector indicates pin 1 — match it to the silkscreen on the PCB (pin
   1 is usually marked with a square pad or a "1", see ivanwick's
   [picture](https://github.com/ivanwick/bekantfirmware/wiki/images/pickit3.jpg)).
5. **Check the date code** on the white sticker on the surface that touches the
   underside of the desk. It's formatted like `2017/34` (year / week of year).
   See the [Hardware variants](#hardware-variants--code-protection) section
   below for what to do with that information.

## 4. Setting up the programmer

### 4.1 Limitations

Legacy PICkit's are no longer actively supported by Microchip Technology Inc.
meaning you would not be able to use current versions of either

- [MPLAB X IDE](https://www.microchip.com/en-us/tools-resources/develop/mplab-x-ide)
- [MPLAB Tools for VS Code](https://www.microchip.com/en-us/tools-resources/develop/mplab-tools-vs-code)

Older versions of the _MPLAB X IDE_ might not run on current operating systems
as macOS 26, and one would need a much more expensive _PICkit 5_ or _MPLAB SNAP_
in order to use these tools today.

Fortunately enough, there are free tools available for the older and cheaper
PICkit programmer that still work (at least mid-2026).

I will give examples for both options in later sections.

### 4.2 Option 1: jpecmd.jar from MPLAB X v6.20 (for PICkit 3 / 4 / 5)

Use _jpecmd.jar_ provided by Microchip by installing the retired/archived
[MPLAB X v6.20](https://www.microchip.com/en-us/tools-resources/archives/mplab-ecosystem).

You might want to create an _ipecmd_ alias, as this is what I'll use in the
examples below. Here are setup instructions for macOS 26 (you'll need to have
MPLAB X v6.20 already installed):

```sh
# set one of 3, 4 or 5
PICKIT_VERSION=3
PIC_SELECTOR=16LF1938

MPLAB_JRE_JAVA=`find /Applications/microchip/mplabx/v6.20 -name java -type file`
MPLAB_IPECMD_JAR=`find /Applications/microchip/mplabx/v6.20 -name ipecmd.jar -type file`

echo "alias ipecmd='${MPLAB_JRE_JAVA} -jar ${MPLAB_IPECMD_JAR} -P${PIC_SELECTOR} -TPPK${PICKIT_VERSION} -W'" >> ~/.zshrc
alias ipecmd='${MPLAB_JRE_JAVA} -jar ${MPLAB_IPECMD_JAR} -P${PIC_SELECTOR} -TPPK${PICKIT_VERSION} -W'
```

Now test it by running a blank check using _ipecmd_:

```sh
ipecmd -C
```

Expect the output to look something like this:

```text
DFP Version Used : PIC12-16F1xxx_DFP,1.9.258,Microchip
*****************************************************
Connecting to MPLAB PICkit 3...
Currently loaded firmware on PICkit 3
Firmware Suite Version.....01.56.09
Firmware type..............Enhanced Midrange
Programmer to target power is enabled - VDD = 3,250000 volts.
Target device PIC16LF1938 found.
Device Revision ID = 3
Blank Checking...
program memory
Address: 0 Expected Value: 3fff Received Value: 3180
Blank check complete, device is not blank.
PICkit 3 removed
```

### 4.3 Option 2: pk2cmd and PICkitminus (for PICkit 2 / 3)

In order to be able to use the [pk2cmd](https://github.com/jaka-fi/pk2cmd)
command line tool, you might need to downgrade your PICkit's firmware from
"MPLAB mode" it was delivered with to the "scripting firmware" using the
[PICkitminus](https://github.com/jaka-fi/PICkitminus) GUI software suite which
runs on Linux and Windows only. Unfortunately there is no macOS release, but
only need it once (or twice for resetting back to MPLAB mode).

In order to change the firmware, choose "Tools > Download PICkit Operating
System" from the menu. It will open the folder you called the executable from
and this contains a PK3OS*.hex file. Choose that file and wait until updated.

The _pk2cmd_ command line tool meanwhile is available for all OS'es. Make it
available somehow for ease of access - here is an alias variant for macOS:

```sh
PK2CMD_EXECUTABLE=~/Downloads/pk2cmd_1_27_01/pk2cmd

echo "alias pk2cmd='~/Downloads/pk2cmd_1_27_01/pk2cmd -PPIC16LF1938'" >> ~/.zshrc
alias pk2cmd='~/Downloads/pk2cmd_1_27_01/pk2cmd -PPIC16LF1938'
```

Now test it by running _pk2cmd_ from the local folder:

```sh
pk2cmd -I
```

Expect the output to look something like this:

```text
Auto-Detect: Found part PIC16LF1938.

Device ID = 24A0
Revision  = 0003
Device Name = PIC16LF1938

Operation Succeeded
```

... and if you see some about "MPLAB mode", well then you still need to
downgrade the programmers firmware using _PICkitminus_.

## 5. Back up the original firmware (strongly recommended, with caveats)

> **Read [Hardware variants](#hardware-variants--code-protection) first.** On
> desks manufactured 2019 or later the PIC is code-protected and a backup will
> read back as all zeros. You cannot recover the OEM firmware from a newer chip.

On a desk that is **not** code-protected (typically manufactured before 2019),
read the original PIC's flash and EEPROM and save the `.hex` file before you
write anything:

```sh
# Option 1: ipecmd.jar
ipecmd -GF"bekantfirmware-backup.hex"
# Option 2: pk2cmd
pk2cmd -GF bekantfirmware-backup.hex
```

Now verify the backup is real (not all zeros) by checking the file size and
contents:

```sh
# A real firmware dump is ~94 KB. A code-protected dump is 0 bytes
# or all 0x00 / 0xFF.
ls -l bekantfirmware-backup.hex
# Read the first 16 bytes; for a real dump you should see a mix of
# non-zero values that look like Intel HEX records.
head -3 bekantfirmware-backup.hex
```

If your backup is all zeros, your PIC is code-protected. Use the reference dump
[`orginafirm.hex.zip`](https://github.com/ivanwick/bekantfirmware/files/7874509/orginafirm.hex.zip)
instead — see [Hardware variants](#hardware-variants--code-protection) for the
caveats.

## 6. Flash the new firmware

You might want/need to erase the PIC16LF1938 on newer (post-2019) models of the
Bekant desk to disable the code protection:

```sh
# Option 1: ipecmd.jar
ipecmd -E
# Option 2: pk2cmd
pk2cmd -E
```

Now you can load the new firmware to the PIC16LF1938:

```sh
# Option 1: ipecmd.jar
ipecmd -F"dist/bekantfirmware.hex" -M -Y -OL
# Option 2: pk2cmd
pk2cmd -F dist/bekantfirmware.hex -M -Y
```

## 7. Restore the EEPROM defaults (optional)

Ivanwick's project firmware stores the memory positions in the PIC's EEPROM at
offsets `0x00` and `0x02`, with defaults `0x0636` and `0x1600`. If you flashed a
fresh unit, these defaults are already correct. If you reprogrammed a unit that
already had custom positions, you have two options:

**Set manually** — move to each height, then hold both buttons for 3 s to save:

- Move the desk to the desired sitting height, then press and hold both buttons
  for 3 seconds.
- Move the desk to the desired standing height, then press and hold both buttons
  for 3 seconds.

**Factory reset** — hold both buttons for the full 8 seconds. The save gesture
fires at 3 s, then the factory-reset fires at 8 s and clears both memory slots
to the fresh-chip state. The very next 3-second both-buttons-hold will then
populate both slots with the same target (first-save rules apply).

## 8. Close the controller and test

1. Unplug the programmer (USB cable)
2. Remove the programmer from the board
3. Reassemble the controller enclosure.
4. Plug the desk back into mains.
5. Test the basic motion (hold `△` / `▽`).
6. Test the memory positions

# Hardware variants & code protection

The BEKANT controller has shipped in at least two distinct hardware revisions.
The variants differ in two ways that matter for this project: **how easy the
case is to open**, and **whether the firmware can be read back from the PIC**.

## Disassembly difficulty

The controller case is ultrasonically welded. IKEA has progressively added more
weld spots to deter modifications. Newer units (2019+) have noticeably more
welds and may require cutting or prying to get the back plate off. The damage is
cosmetic and the case still holds together when you put it back. See the
disassembly section above.

## Code protection on newer units

**The most important variant to be aware of:** desks manufactured roughly 2019
or later ship with the PIC's **code-protection (CP) config bit enabled**. When
CP is set, the PIC refuses to read back its own flash memory over ICSP. The
programmer will still **write** the chip (which is what you need to install the
new firmware), but a backup "read" will return all zeros.

Symptoms reported in
[ivanwick/bekantfirmware issue #5](https://github.com/ivanwick/bekantfirmware/issues/5)
and
[ivanwick/bekantfirmware issue #2 comments 1013629545 / 1013637468](https://github.com/ivanwick/bekantfirmware/issues/2)
include:

- _"I was able to flash the pic but not to be able to read ... the backup ...
  was zeros only."_ — code-protected.
- _"It did not autodetect it and was not able to make a working backup of the
  flash."_ — usually code-protected.
- _Backup dump is suspiciously small (0 bytes or only `0x00` / `0xFF` records)._
  — code-protected.

If your backup reads back as all zeros, **do not assume the firmware is gone** —
it is just locked. You can still flash the new firmware, you just cannot recover
the OEM firmware from your specific unit.

To flash a code-protected unit, you must temporarily erase the chip (which
clears the CP bit along with the firmware). After erasing, you can write the new
firmware normally (see instructions above).

## Reference firmware dump for code-protected units

For code-protected units where you cannot read the OEM firmware back, the
ivanwick project bundles a reference dump
([`orginafirm.hex.zip`](https://github.com/ivanwick/bekantfirmware/files/7874509/orginafirm.hex.zip))
that you can flash to restore factory behaviour. This dump is bundled with this
project too — see `oem_reconstruction/disassembly/orginafirm.hex`.

### Caveats

- The reference dump is **unverified by the project author**. It was posted in
  [issue #2 comment 1013637468](https://github.com/ivanwick/bekantfirmware/issues/2#issuecomment-1013637468)
  by a user reading their own pre-2019 unit ("This is NOT verified or tested,
  but its the read from my original firmware"). It was then tested by another
  user
  ([issue #2 comment 1013638297](https://github.com/ivanwick/bekantfirmware/issues/2#issuecomment-1013638297))
  who reported it worked on a 2017-vintage unit, but that is anecdotal.
- The dump is for a **specific hardware revision** of the BEKANT controller.
  Newer revisions may use a different PIC, a different LIN protocol, or a
  different leg controller, in which case the reference dump will not work. If
  flashing the reference dump causes the desk to misbehave (e.g. relay engaged
  but legs not moving, LIN errors, etc.), your hardware is incompatible.
- If you flash the reference dump and later want to go back to the ivanwick
  firmware, the round-trip has not been tested.

In short: **treat the reference dump as a recovery option, not a guaranteed
rollback**.
