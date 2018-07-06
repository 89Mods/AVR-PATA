/*
 * ata.c
 *
 * Created: 7/2/2018 11:18:32 PM
 *  Author: lucah
 */ 

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include "ata.h"
#include <stdint.h>
#include <util/delay.h>

typeDriveInfo driveInfo;

partition partitions[4];

uint8_t ata_init() {
	//Set up ports
	DDRA = 0x00;
	DDRC = 0x00;
	DDRD |= (1 << PD7) | (1 << PD6);
	PORTD |= (1 << PD7) | (1 << PD6);
	DDRD &= ~(1 << PD5);
	DDRB = 0x1F;
	PORTB = NOP;
	
	//Wait for drive to be ready
	for(uint8_t i = 0; i < 10; i++) _delay_ms(10);
	while((PIND & (1 << PD5)) == 0){
		_delay_ms(2);
	}
	
	ata_reset();
		
	//Enable LBA mode
	ata_writeByte(REG_DRIVE_HEAD, DRV_HEAD_BASE);
	_delay_ms(1);
	ata_busyWait();
	
	//Get info
	ata_writeByte(REG_STAT_CMD, CMD_IDENT);
	_delay_us(10);
	ata_busyWait();
	ata_waitForData();
	_delay_us(200);
	
	uint8_t buff[512];
	ata_readBuffer(buff, 512);
	
	driveInfo.LBAsupported = (buff[49 * 2 + 1] & 2) == 2 ? 1 : 0;
	if(!driveInfo.LBAsupported) return ERR_LBA_NOT_SUPPORTED;
	driveInfo.sizeinsectors = * ((unsigned long*) (buff + 120));
	for(uint8_t i = 0; i < 20; i+=2) {
		driveInfo.serial[i] = buff[20 + i + 1];
		driveInfo.serial[i + 1] = buff[20 + i];
	}
	for(uint8_t i = 0; i < 40; i+=2) {
		driveInfo.model[i] = buff[54 + i + 1];
		driveInfo.model[i + 1] = buff[54 + i];
	}
	if((buff[53 * 2] & 1) == 0) return ERR_DATA_INVALID;
	
	//Detect partitions
	uint8_t err = ata_readSector(buff, 0); //Read sector 0 (contains MBR)
	if(err != 0) return err | 0b00100000; //Make sure bit 5 is always one, so this error code isn't mistaken with any other errors this function can return
	if(buff[510] != 0x55 || buff[511] != 0xAA){
		return ERR_INVALID_MBR;
	}
	
	for(uint8_t i = 0; i < 4; i++){
		partitions[i].id = i;
		partitions[i].start = * ((unsigned long*) (buff + (446 + i * 16 + 8)));
		partitions[i].size = * ((unsigned long*) (buff + (446 + i * 16 + 12)));
		partitions[i].end = partitions[i].start + partitions[i].size;
		partitions[i].type = buff[446 + i * 16 + 4];
		if(partitions[i].start > driveInfo.sizeinsectors || partitions[i].end > driveInfo.sizeinsectors || partitions[i].type == 0){ //Invalid partition
			partitions[i].start = 0; //Clear info
			partitions[i].size = 0;
			partitions[i].end = 0;
			partitions[i].type = 0;
		}
	}
	
	return ata_runDiag();
}

//Does a software reset
void ata_reset() {
	//Software reset
	ata_writeByte(REG_ASTA_CTRL, 0x06);
	for(uint8_t i = 0; i < 10; i++) _delay_ms(10);
	ata_writeByte(REG_ASTA_CTRL, 0x00);
	for(uint8_t i = 0; i < 10; i++) _delay_ms(10);
	ata_busyWait();
}

//Run drive diagnostics
//Returns: 0 when no error was found, error code otherwise
uint8_t ata_runDiag() {
	//Check for errors
	ata_writeByte(REG_STAT_CMD, CMD_DIAG);
	_delay_ms(1);
	ata_busyWait();
	uint8_t errRes = (uint8_t)ata_readByte(REG_ERR_FEAT);
	return errRes == 0x01 ? 0x00 : errRes;
}

//Writes a single byte to the drive
//Arguments: target address, data
void ata_writeByte(uint8_t addr, uint8_t dat) {
	PORTB = addr;
	DDRC = 0xFF;
	DDRA = 0xFF;
	PORTA = dat;
	PORTC = 0;
	PORTD &= ~(1 << PD7);
	asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
	PORTD |= (1 << PD7);
	DDRC = 0x00;
	DDRA = 0x00;
	PORTB = NOP;
}

//Reads a single byte from the drive
//Arguments: source address
//Returns: read byte
uint8_t ata_readByte(uint8_t addr){
	PORTB = addr;
	DDRC = 0x00;
	DDRA = 0x00;
	PORTC = 0xFF;
	PORTA = 0xFF;
	PORTD &= ~(1 << PD6);
	asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
	char l = PINA;
	PORTD |= (1 << PD6);
	PORTB = NOP;
	return l;
}

//Wait for the BSY bit to be 0 and for the DRDY bit to be 1
void ata_busyWait(){
	while(1){
		uint16_t t = ata_readByte(REG_STAT_CMD);
		if((t & 0b10000000) == 0 && (t & 0b01000000) != 0){
			break;
		}
		_delay_us(25);
	}
}

//Wait for the DRY bit to be 1
void ata_waitForData(){
	while(1){
		uint8_t t = ata_readByte(REG_STAT_CMD);
		if((t & 0b00001000) != 0){
			break;
		}
		_delay_us(25);
	}
}

//Read the drive's buffer
//Arguments: destination array, number of bytes to read (must be divisible by 2)
void ata_readBuffer(uint8_t *Buffer, uint16_t numBytes){
	DDRC = 0x00;
	DDRA = 0x00;
	PORTC = 0xFF;
	PORTA = 0xFF;
	for(uint16_t i = 0; i < numBytes; i+=2){
		while((ata_readByte(REG_STAT_CMD) & 0b00001000) == 0) {}
		PORTB = REG_DATA;
		PORTD &= ~(1 << PD6);
		asm("nop");
		asm("nop");
		asm("nop");
		asm("nop");
		Buffer[i] = PINA;
		Buffer[i + 1] = PINC;
		PORTD |= (1 << PD6);
		asm("nop");
		asm("nop");
		asm("nop");
		asm("nop");
	}
	PORTB = NOP;
}

//Write to the drive's buffer
//Arguments: source array, number of bytes to write (must be divisible by 2)
void ata_writeBuffer(uint8_t *Buffer, uint16_t numBytes) {
	DDRC = 0xFF;
	DDRA = 0xFF;
	PORTB = REG_DATA;
	for(uint16_t i = 0; i < numBytes; i+=2){
		PORTA = Buffer[i];
		PORTC = Buffer[i + 1];
		PORTD &= ~(1 << PD7);
		asm("nop");
		asm("nop");
		asm("nop");
		asm("nop");
		PORTD |= (1 << PD7);
		asm("nop");
		asm("nop");
		asm("nop");
		asm("nop");
	}
	PORTB = NOP;
}

//Reads a sector
//Parameters: destination array, LBA address
//Returns: error code (0 = no error)
uint8_t ata_readSector(uint8_t *Buffer, uint32_t lba) {
	ata_busyWait();
	
	ata_writeByte(REG_SECTOR_NUM, lba & 0x000000FFL);
	ata_writeByte(REG_CYLINDER_LOW, (lba >> 8) & 0x000000FFL);
	ata_writeByte(REG_CYLINDER_HIGH, (lba >> 16) & 0x000000FFL);
	ata_writeByte(REG_DRIVE_HEAD, DRV_HEAD_BASE | ((lba >> 24) & 0x0FL));
	ata_writeByte(REG_SECTOR_COUNT, 1);
	
	ata_writeByte(REG_STAT_CMD, CMD_READ_SEC);
	_delay_us(0.25);
	ata_busyWait();
	
	if(ata_readByte(REG_STAT_CMD) & 0x01)
		return ata_readByte(REG_ERR_FEAT);
	
	ata_waitForData();
	
	ata_readBuffer(Buffer, 512);
	
	return (ata_readByte(REG_STAT_CMD) & 0x01) ? ata_readByte(REG_ERR_FEAT) : 0;
}

//Writes a sector
//Parameters: source array, LBA address
//Returns: error code (0 = no error)
uint8_t ata_writeSector(uint8_t *Buffer, uint32_t lba) {
	ata_busyWait();
	
	ata_writeByte(REG_SECTOR_NUM, lba & 0x000000FFL);
	ata_writeByte(REG_CYLINDER_LOW, (lba >> 8) & 0x000000FFL);
	ata_writeByte(REG_CYLINDER_HIGH, (lba >> 16) & 0x000000FFL);
	ata_writeByte(REG_DRIVE_HEAD, DRV_HEAD_BASE | ((lba >> 24) & 0x0FL));
	ata_writeByte(REG_SECTOR_COUNT, 1);
	
	ata_writeByte(REG_STAT_CMD, CMD_WRITE_SEC);
	_delay_us(0.25);
	ata_busyWait();
	ata_waitForData();
	
	ata_writeBuffer(Buffer, 512);
	
	ata_busyWait();
	
	return (ata_readByte(REG_STAT_CMD) & 0x01) ? ata_readByte(REG_ERR_FEAT) : 0;
}

//Rewrites this drive's MBR
//THE PREVIOUS MBR, INCLUDING THE PARTITION TABLE WILL BE OVERWRITEN! BE CAREFULL WITH THIS!
//Returns returned error code from write operation (0 = no error)
uint8_t ata_rewriteMBR() {
	uint8_t newMBR[512];
	for(uint64_t i = 0; i < 512; i++) newMBR[i] = 0x00;
	newMBR[510] = 0x55;
	newMBR[511] = 0xAA;
	uint8_t err = ata_writeSector(newMBR, 0);
	if(err != 0) return err;
	//Clear local partition table
	for(uint8_t i = 0; i < 4; i++){
		partitions[i].type = 0x00;
		partitions[i].start = 0x00;
		partitions[i].start = 0x00;
		partitions[i].end = 0x00;
	}
	return 0;
}

//Creates a new primary partition
//THE PREVIOUS PARTITION ENTRY AT id WILL BE OVERWRITEN! BE CAREFULL WITH THIS ALSO!
//Returns error code (0 = no error)
uint8_t ata_createPartition(uint8_t id, uint32_t start, uint32_t size, uint8_t type) {
	if(id > 4 || type == 0x00 || start + size > driveInfo.sizeinsectors || start == 0 || size == 0) return ERR_INVALID_PARTITION; //There can't be more then 4 partitions and type 0x00 stands for unallocated space (can't be a partition)
	//Check if new partition does not overlap with existing ones
	for(uint8_t i = 0; i < 4; i++){
		if(i == id || partitions[i].size == 0) continue;
		if(start >= partitions[i].start && start <= partitions[i].end) return ERR_INVALID_PARTITION;
		if(start + size >= partitions[i].start) return ERR_INVALID_PARTITION;
	}
	//Edit MBR to include partition
	uint8_t MBR[512];
	uint8_t err = ata_readSector(MBR, 0);
	if(err != 0) return err;
	MBR[446 + id * 16 + 8 + 3] = (uint8_t)(start >> (uint32_t)24);
	MBR[446 + id * 16 + 8 + 2] = (uint8_t)(start >> (uint32_t)16);
	MBR[446 + id * 16 + 8 + 1] = (uint8_t)(start >> (uint32_t)8);
	MBR[446 + id * 16 + 8 + 0] = (uint8_t)(start);
	MBR[446 + id * 16 + 12 + 3] = (uint8_t)(size >> (uint32_t)24);
	MBR[446 + id * 16 + 12 + 2] = (uint8_t)(size >> (uint32_t)16);
	MBR[446 + id * 16 + 12 + 1] = (uint8_t)(size >> (uint32_t)8);
	MBR[446 + id * 16 + 12 + 0] = (uint8_t)(size);
	MBR[446 + id * 16] = 0x80;
	MBR[446 + id * 16 + 4] = type;
	//Rewrite modded MBR
	err = ata_writeSector(MBR, 0);
	if(err != 0) return err;
	//Update local partition table
	partitions[id].start = start;
	partitions[id].size = size;
	partitions[id].end = start + size;
	partitions[id].type = type;
	return 0;
}

//Getters
char *ata_getModel() {
	return driveInfo.model;
}

char *ata_getSerial() {
	return driveInfo.serial;
}

uint32_t ata_getSectorCount() {
	return driveInfo.sizeinsectors;
}

uint8_t ata_isLBASupported() {
	return driveInfo.LBAsupported;
}

uint64_t ata_getSizeInBytes() {
	return (uint64_t)driveInfo.sizeinsectors * (uint64_t)512;
}

uint32_t ata_getPartitionLocation(uint8_t partitionID) {
	return partitions[partitionID].start;
}

uint32_t ata_getPartitionSize(uint8_t partitionID) {
	return partitions[partitionID].size;
}

uint32_t ata_getEndOfPartition(uint8_t partitionID) {
	return partitions[partitionID].end;
}

uint8_t ata_getPartitionType(uint8_t partitionID) {
	return partitions[partitionID].type;
}

uint8_t ata_getPartitionCount() {
	return 4;
}