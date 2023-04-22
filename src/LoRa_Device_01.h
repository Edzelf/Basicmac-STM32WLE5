// LoRa_Device_01.h
// Definitions for an end device.
// Include this file in the main program.
//
#define JOINMODE_OTAA 1
#define JOINMODE_ABP  2

// Configuraton end device
int           JoinMode = JOINMODE_OTAA ;                        // Select join mode for this device
int           LoraBand = REGION_EU868 ;                         // LoRa band used for this device

//For OTAA:
uint8_t       JoinEui[] = { 0x12, 0x15, 0x18, 0x78, 0x66, 0x13, 0xA2, 0x11 } ;
uint8_t       AppKey[]  = { 0x36, 0x9C, 0x9E, 0x8C, 0x5D, 0x68, 0x61, 0x3E,
                            0xD9, 0xB9, 0xDD, 0x55, 0x43, 0x7A, 0xA9, 0x70 } ;
uint8_t       DevEui[]  = { 0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x05, 0xC8, 0x28 } ;

// For ABP:
uint32_t      DevAddr = 0x260B05E3 ;                            // From TTN
uint8_t       NwkSKey[] = { 0x37, 0x84, 0x9B, 0x30, 0x33, 0xEE, 0x5A, 0xD1,
                            0x37, 0xDB, 0xDE, 0x8C, 0x00, 0xD3, 0x51, 0x8B } ;
uint8_t       AppSKey[] = { 0x5C, 0x4E, 0x2A, 0x28, 0x22, 0xBB, 0x93, 0x4E,
                            0x99, 0xA4, 0x19, 0xA9, 0xDA, 0x34, 0x5F, 0x8E } ;

uint32_t      tx_interval_sec = 60 ;                            // Send a message every 60 seconds

