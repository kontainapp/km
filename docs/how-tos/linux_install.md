# Notes on Development System Setup

Go to fedora.org man get the Workstation verision for X86_64.
You will create a bootable image on a USB drive.

## Lenovo M715q

The 715q is a tiny footprint system that comes pre-installed with Windows 10.
Out of the box, the M715q  will not boot Fedora USB until the following
changes are made:

* Upgrade the BIOS
* Change BIOS Boot Settings

You'll need to bring Windows up in order to perform these operations.

### BIOS Upgrade

The 'L' icon on the task bar is the Lenovo support application.
Use this application to update Lenovo provided drivers and firmware.

For Example: After auto upgrade, BIOS went from M1XKT34A to M1XKT39A.

### BIOS Boot Settings: Boot Order

Move the USB disk to the top of the boot order

### BIOS Boot Settings: EUFI

Out-of-the-box, the BIOS requires EUFI boot images and won't boot
the Fedora USB boot image.

How to fix this:

1. Choose Settings -> Update & Security -> Recovery.
2. Under 'Advanced Setup': press 'Restart Now'.
3. Choose Troubleshoot -> Advanced Options -> UEFI Firmware Settings -> Restart
4. After restart you'll have a test-gui style app:
..* Select 'Startup'
..* Set 'CSM' to 'Enabled'
..* Set 'Boot Mode' to 'Auto'
..* Under 'Primary Boot Sequence' set USB devices to the top.
..* Save and Exit
