/*
 * example.c
 *
 * Created: 7/6/2018 3:41:50 PM
 * Author : lucah
 *
 * This example will write a pattern to sector 1024 before reading from the same sector
 * and checking the pattern to test the connection to the drive and if data is stored correctly.
 * 
 * If the test failed, pin PD4 is switched to high, if it succeeds, pin PD3 is switched to high.
 *
 * This test will also fail if any partition currently occupies said sector to prevent data loss.
 */ 

#ifndef F_CPU
#define F_CPU 16000000UL
#endif
 
#include <avr/io.h>
#include <util/delay.h>
#include "ata.h"

void error() {
	PORTD |= (1 << PD4);
	while(1);
}
 
int main(void) {
	DDRD |= (1 << PD4) | (1 << PD3);
	uint8_t err = ata_init();
	if(err != 0) error();
	
	for(uint8_t i = 0; i < 4; i++) {
		if(ata_getPartitionSize(0) != 0 && ata_getPartitionLocation(i) <= 1024) error();
	}
	
	uint8_t buff[512];
	for(uint16_t i = 0; i < 512; i++){
		buff[i] = i % 64 + 7;
	}
	
	ata_writeSector(buff, 1024);
	
	for(uint8_t i = 0; i < 100; i++) _delay_ms(10);
	
	ata_readSector(buff, 1024);
	
	for(uint16_t i = 0; i < 512; i++){
		if(buff[i] != i % 64 + 7) error();
	}
	
	ata_writeByte(REG_STAT_CMD, CMD_STANDBY_NOW);
	
	PORTD |= (1 << PD3);
	
	while(1);
}