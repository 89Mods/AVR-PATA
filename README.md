# AVR-PATA

This library makes it possible to connect any Parallel ATA HDD to an AVR AtMega Microcontroller.

Requirements:
RAM: 768 bytes without FatFS, 8KB (preferrably more) with FatFS
Speed: up to 20MHz
I/O Pins: 25

Default pin config:
(AVR)     -     (HDD)
PA0 - 7         DD0 - 7
PC0 - 7         DD8 - 15
PD7             DIOW-
PD6             DIOR-
PD5             IORDY
PB0 - 2         DA0 - 2
PB3             CS3FX-
PB4             CS1FX-

DMACK needs to be connected to VCC or Ground
All ground pins, SPSYNC, DMARQ, INTRQ and IOCS16- need to be connected to Ground
You can connect an LED to DASP- as an activity LED
Lastly, it is recommended to connect the Reset pin on the HDD with the Reset pin on the AVR

PATA pinout diagram: https://i.imgur.com/gx2Lhfm.png (Source: ATA Interface Reference Manual - Seagate)
Pinout for 2.5'' drives: https://i.imgur.com/zYXHgGj.png (Same source as above)

This library has also been confirmed to work with SATA to PATA adapters.

If you are using an older drive, it is possible that the library runs too fast for it to keep up. In this case, either decrease the AVRs clock speed or edit ata.c and replace all blocks of "asm("nop")" functions with _ delay_us(1 - 10) (The length of the required delay can be different depending on your drive).

All functions in ata.c have a short description of their function, arguments and returned value above them.
example.c contains a full example program.

This library requires for the connected drive to have a valid MBR and partition table. If the connected drive does not have a valid MBR and/or partition table, use the ata_reqwriteMBR and ata_createPartition functions to create them (Warning! These functions will overwrite and existing MBR and/or partition table, which can cause data loss. Use them carefully!).

Due to a limitation in ATA-1, LBA addresses are only 28-bit, meaning the maximum usable drive capacity is 128GiB (137 GB). Larger drives can be connected, but partitions that go beyond this limit are marked as invalid and ignored. It is recommended to set up one primary partition that ends at exactly sector # 2^28. The first partition is also required to not start before sector 2048.
