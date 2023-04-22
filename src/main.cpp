//***************************************************************************************************
//  Test program for SEEED Lora-E5 - Main program.                                                  *
//***************************************************************************************************
// OTAA mode:                                                                                       *
// On powerup, OTAA is used to join the network.  This will take about 34 seconds.  The TTN reply   *
// returns the (dynamic) keys that can be used to send the consecutive packets, just like ABP.      *
// The keys from TTN are saved in EEPROM for later use.                                             *
// The unit goes into deep sleep mode when a packet has been sent.                                  *
// After wake-up, ABP mode is used, using the dynamic keys from EEPROM.                             *
// This will take about 4 seconds.  After 300 packets send, a rejoin is made with OTAA.             *
// This is an escape in case the join gets broken (key lost at TTN, ...).                           *
// In deep sleep, the unit consumes 4 mA.  This is probably caused by the on-board USB chip.        *
//***************************************************************************************************
//                                                                                                  *
// Revision    Auth.  Remarks                                                                       *
// ----------  -----  ----------------------------------------------------------------------------- *
// 31-03-2023  ES     First set-up.                                                                 *
// 04-04-2023  ES     Most AU channels disabled.                                                    *
// 18-04-2023  ES     Version with modified basicmac.                                               *
// 20-04-2023  ES     Working ABP version including deep sleep mode.                                *
// 22-04-2023  ES     Working ABP and OTAA version including deep sleep mode.                       *
//***************************************************************************************************
#include <Arduino.h>
#include <lmic.h>
#include <STM32RTC.h>
#include <STM32LowPower.h>

//***************************************************************************************************
// Configuration of end device.
#include "LoRa_Device_01.h"            // Definition for end device (Freq band, keys, ...)
//***************************************************************************************************

#include <EEPROM.h>                                       // Access to simulated EEPROM in Flash
#include <SPI.h>                                          // Needed fro correct compilation

//#include <STM32LowPower.h>

// Pin definitions.  The marking of the pins is strange.  Both the internal name and the marking
// on the module is listed in the following definition(s).
#define LED              PB5                              // LED is on PB5

// Data in RTC back-up registers
#define DATAVALID        67329752                         // Code for data valid (RTC and EEPROM)
#define BKP_R_DATAVALID  RTC_BKP_DR10                     // Position of data valid register
#define BKP_R_FCNT       RTC_BKP_DR11                     // Position of uplink frame counter
#define BKP_R_XMITCNT    RTC_BKP_DR12                     // Count number of transmits for rejoin

#define REJOIN_LIMIT     300                              // Rejoin after this number of transmits

// Data kept in EEPROM
struct eepromdata_t
{
  uint32_t datavalid ;                                    // Code for valid data
  uint32_t fcnt ;                                         // Uplink counter for ABP
  bool     joinedFlag ;                                   // If true: devaddr, nwkSKey and appSkey are valid
  uint32_t devaddr ;                                      // devaddr after OTAA join, from TTN
  uint8_t  nwkSKey[16] ;                                  // nwkSkey after OTAA join, from TTN
  uint8_t  appSKey[16] ;                                  // appSkey after OTAA join, from TTN
} ;


//**************************************************************************************************
// Local data.                                                                                     *
//**************************************************************************************************
static osjob_t    sendjob ;                               // Handle for send_packet
eepromdata_t      eepromdata ;                            // Data to and from EEPROM
STM32RTC&         rtc = STM32RTC::getInstance() ;         // Object for RTC clock (and RTC data)
bool              tx_finished = false ;                   // True if send finished
int32_t           xmitcount ;                             // Transmitcount from BKP register


//***************************************************************************************************
//                            C A L L B A C K S   F O R   O T A A                                   *
//***************************************************************************************************
// These callbacks are only used in over-the-air activation, not for ABP.                           *
//***************************************************************************************************
void os_getJoinEui (u1_t* buf)
{
  for ( int i = 0 ; i < 8 ; i++ )
  {
    buf [i] = JoinEui[7 - i] ;
  }
}

// This should also be in little endian format, see above.
void os_getDevEui (u1_t* buf)
{
  for ( int i = 0 ; i < 8 ; i++ )
  {
    {
      buf [i] = DevEui[7 - i] ;
    }
  }
}

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from ttnctl can be copied as-is.
// The key shown here is the semtech default key.
void os_getNwkKey (u1_t* buf)
{
  memcpy ( buf, AppKey, 16 ) ;
}


// Return the region code used.  Will be called by LMIC_reset().
u1_t os_getRegion()
{
  return LMIC_regionCode ( LoraBand ) ;
}


//**************************************************************************************************
//                                R E A D E E P R O M D A T A                                      *
//**************************************************************************************************
// Read EEPROM data.                                                                               *
//**************************************************************************************************
void readEEPROMdata()
{
  uint8_t* p = (uint8_t*)&eepromdata ;                      // Set pointer to destination

  for ( int i = 0 ; i < sizeof(eepromdata_t) ; i++ )        // Fill destination
  {
    *p++ = EEPROM.read ( i ) ;                              // Copy one byte
  }
}


//**************************************************************************************************
//                                S A V E E E P R O M D A T A                                      *
//**************************************************************************************************
// Save EEPROM data.                                                                               *
//**************************************************************************************************
void saveEEPROMdata()
{
  uint8_t* p = (uint8_t*)&eepromdata ;                      // Set pointer to source

  for ( int i = 0 ; i < sizeof(eepromdata_t) ; i++ )        // Fill destination
  {
    EEPROM.update ( i, *p++ ) ;                             // Copy one byte
  }
}


//***************************************************************************************************
//                                   S A V E O T A A K E Y S                                        *
//***************************************************************************************************
// Save the TTN keys after join.  They can be used for subsequent transmits to TTN without the      *
// need to rejoin.                                                                                  *
//***************************************************************************************************
void saveOTAAkeys()
{
  eepromdata.devaddr = LMIC.devaddr ;                       // Save devaddr
  memcpy ( eepromdata.nwkSKey, LMIC.lceCtx.nwkSKey, 16 ) ; 	// Save nwkSkey
  memcpy ( eepromdata.appSKey, LMIC.lceCtx.appSKey, 16 ) ;  // Save appSKey
  eepromdata.joinedFlag = true ;                            // Join data is valid now
  saveEEPROMdata() ;                                        // Save in EEPROM
  debug_printf ( "OTAA join keys saved in EEPROM\n" ) ;
}


//***************************************************************************************************
//                                   S H O W O T A A K E Y S                                        *
//***************************************************************************************************
// Show the saved OTAA keys.                                                                        *
//***************************************************************************************************
void showOTAAkeys()
{
  char buf1[50] ;
  char buf2[50] ;

  for ( int j = 0 ; j < 16 ; j++ )
  {
    sprintf ( buf1 + j * 3, "%02X ", eepromdata.nwkSKey[j] ) ;
    sprintf ( buf2 + j * 3, "%02X ", eepromdata.appSKey[j] ) ;
  }
  debug_printf ( "LoRa devaddr is %08X\n", eepromdata.devaddr  ) ;
  debug_printf ( "LoRa nwkSKey is %s\n",  buf1 ) ;
  debug_printf ( "LoRa appSKey is %s\n",  buf2 ) ;
}


//***************************************************************************************************
//                                O N L M I C E V E N T                                             *
//***************************************************************************************************
// Handle events.                                                                                   *
//***************************************************************************************************
void onLmicEvent (ev_t ev)
{
    debug_printf ( "Event EV_%e\n", ev ) ;
    switch(ev)
    {
        case EV_JOINED:
            LMIC_setLinkCheckMode(0);
            break;
        case EV_TXDONE:
            //debug_printf ( "EV_TXDONE\n" ) ;
            //extern char dbgtext[] ;
            //Serial.print ( dbgtext ) ;
            break ;
        case EV_TXCOMPLETE:
            eepromdata.fcnt = LMIC.seqnoUp ;                          // Get uplink frame counter
            setBackupRegister ( BKP_R_FCNT,                           // Save reset count plus one
                                eepromdata.fcnt ) ;
            debug_printf ( "EV_TXCOMPLETE (includes waiting for RX windows)\n" ) ;
            tx_finished = true ;                                      // Signal finished to main loop
            if (LMIC.txrxFlags & TXRX_ACK)
            {
              debug_printf ( "Received ack\n" ) ;
            }
            if ( LMIC.dataLen )
            {
              debug_printf ( "Received %d bytes of payload\n",
                              LMIC.dataLen ) ;
              for ( int i = 0 ; i < LMIC.dataLen ; i++ )
              {
                if ( i )
                {
                  debug_printf ( ", " ) ;
                }
                debug_printf ( "0x%02X", LMIC.frame[LMIC.dataBeg + i] ) ;
              }
              debug_printf ( "\n" ) ;
            }
            break;
         default:
            break;
    }
}


//***************************************************************************************************
//                                S E N D _ P A C K E T                                             *
//***************************************************************************************************
// Setup and send a new packet to TTN.                                                              *
// The uplink frame counter is saved in RTC memory (and sometimes EEPROM) after completion.         *
//***************************************************************************************************
void send_packet(osjob_t* j)
{
  char       payload[64] ;                                  // Test data

  if ( LMIC.opmode & OP_TXRXPEND )                          // Current TX/RX job running?
  {
    debug_printf ( "OP_TXRXPEND, not sending" ) ;           // Yes, show error
    return ;                                                // And leave
  }
  digitalWrite ( LED, LOW ) ;                               // Show activity
  sprintf ( payload, "Test %d", eepromdata.fcnt ) ;         // Format test packet
  debug_printf ( "Queue package '%s'\n", payload ) ;        // Show packet to send
  LMIC_setTxData2 ( 1, (u1_t*)payload,                      // Queue the packet
                    strlen ( payload ), 0 ) ;
  digitalWrite ( LED, HIGH ) ;                              // End of activity
  if ( ( eepromdata.fcnt % 100 ) == 0 )                     // 100 packets sent?
  {
    saveEEPROMdata() ;                                      // Yes, save frame counter in EEPROM
    debug_printf ( "fcnt saved in EEPROM\n" ) ;
  }
}


//**************************************************************************************************
//                                     S E T C H A N N E L S                                       *
//**************************************************************************************************
// Set channels to be used..                                                                       *
// Channels depend on frequency plan.                                                              *
//**************************************************************************************************
void setchannels()
{
  if ( LoraBand == REGION_EU868 )
  {
    debug_printf ( "LoRa configured for Europe 868 MHz\n" ) ;
  }
  if ( LoraBand == REGION_AU915 )
  {
    debug_printf ( "LoRa configured Australia/New Zealand 916.8 to 918.2 MHz\n" ) ;
    for ( int band = 0 ; band < 72 ; band++ )              // Disable/enable bands
    {
      if ( band < 8 || band > 15 )                         // Only leave bands 8..15
      {
        LMIC_disableChannel ( band ) ;                     // Disable others
      }
    }
  }                                                        // to get all the 8 channels
}


//***************************************************************************************************
//                          R E T R I E V E _ F C N T                                               *
//***************************************************************************************************
// Read RTC back-up registers and EEPROM to retrieve the LoRa uplink counter needed for ABP.        *                                                        *
//***************************************************************************************************
void retrieve_fcnt()
{
  uint32_t    bckVal ;                                      // Value from backup register
  bool        bckValid ;                                    // Data valid or not
  bool        savflag = false ;                             // EEPROM will be updated if true

  bckVal = getBackupRegister ( BKP_R_DATAVALID ) ;          // Get datavalid word
  bckValid = ( bckVal == DATAVALID ) ;                      // Set/reset valid flag
  EEPROM.begin() ;                                          // Enable EEPROM access
  readEEPROMdata() ;                                        // Read EEPROM data
  if ( eepromdata.datavalid != DATAVALID )                  // Valid data in EEPROM?
  {
    debug_printf ( "Data in EEPROM is invalid\n" ) ;        // No, show it
    eepromdata.datavalid = DATAVALID ;                      // Initialize EEPROM
    eepromdata.fcnt = 0 ;                                   // Count is unknown
    eepromdata.joinedFlag = false ;                         // Assume not joined
    savflag = true ;                                        // Save initialized data later
  }
  else
  {
    debug_printf ( "Data in EEPROM is valid, "              // fcnt from EEPROM 100, 200, ...
                   "fcnt is %d\n", eepromdata.fcnt ) ;
    // The framecounter is saved in EEPROM only after every 100 packets in order to reduce wear-out.
    // So we make certain that the fcnt will be high enough.
    eepromdata.fcnt += 101 ;
  }
  if ( bckValid )                                           // Data in RTC memory is valid?
  {
    bckVal = getBackupRegister ( BKP_R_FCNT ) ;             // Yes, read fcnt from RTC memory
    xmitcount = getBackupRegister ( BKP_R_XMITCNT ) ;       // and xmit count
    debug_printf ( "Data in RTC is valid, "                 // And show
                   "fcnt is %d\n", bckVal  ) ;
    if ( bckVal > ( eepromdata.fcnt + 100 ) )               // Count in EEPROM lower than expected?
    {
      savflag = true ;                                      // Yes, set flag to update EEPROM
    }
    eepromdata.fcnt = bckVal ;                              // Use frame counter from RTC
  }
  else
  {
    debug_printf ( "Data in RTC is not valid\n" ) ;
    setBackupRegister ( BKP_R_DATAVALID, DATAVALID ) ;      // Invalid, write new data
    setBackupRegister ( BKP_R_FCNT, eepromdata.fcnt ) ;     // Insert uplink frame counter
    setBackupRegister ( BKP_R_XMITCNT, REJOIN_LIMIT ) ;     // Force rejoin
    xmitcount = REJOIN_LIMIT ;
  }
  if ( savflag )                                            // Data need to be saved?
  {
    saveEEPROMdata() ;                                      // Yes, update EEPROM
  }
  debug_printf ( "fcnt to be used for TTN is %d\n",         // Show final count
                 eepromdata.fcnt ) ;
}


//**************************************************************************************************
//                                  G E T _ R T C _ T I M E                                        *
//**************************************************************************************************
// Get a string with date and time.                                                                *
//**************************************************************************************************
char* get_rtc_time()
{
  static char tmbuf[64] ;                                   // For date and time as a string

  sprintf ( tmbuf, "%02d-%02d-%02d "                        // Format date
                   "%02d:%02d:%02d",                        // and time
                   rtc.getDay(),
                   rtc.getMonth(),
                   rtc.getYear(),
                   rtc.getHours(),
                   rtc.getMinutes(),
                   rtc.getSeconds() ) ;
  return tmbuf ;
}


//**************************************************************************************************
//                                  I N I T _ R T C _ C L O C K                                    *
//**************************************************************************************************
// Initialize the RTC.                                                                             *
// This is also needed to access the BCK registers.                                                *
//**************************************************************************************************
void init_rtc_clock()
{
  rtc.setClockSource ( STM32RTC::LSE_CLOCK ) ;  // LSI is default, use LSE
  rtc.begin() ;                                 // initialize RTC 24H format
  if ( ! rtc.isTimeSet() )                      // Time already set?
  {
    rtc.setHours   ( 0 ) ;                      // No, set the time
    rtc.setMinutes ( 0 ) ;
    rtc.setSeconds ( 0 ) ;
    rtc.setWeekDay ( 7 ) ;                      // and the date
    rtc.setDay     ( 1 ) ;
    rtc.setMonth   ( 1 ) ;
    rtc.setYear    ( 23 ) ;
  }
  enableBackupDomain() ;                        // Unlock to read/write back-up registers
}


//**************************************************************************************************
//                                     S E T U P                                                   *
//**************************************************************************************************
// Setup used peripherals and LoRa library.                                                        *
//**************************************************************************************************
void setup()
{
  unsigned    chan ;                                        // For channel selection

  Serial.begin ( 115200 ) ;                                 // Start serial IO (RX2/TX2 = PA3,PA2)
  Serial.printf ( "\n" ) ;
  pinMode ( LED,           OUTPUT_OPEN_DRAIN ) ;            // Enable build-in LED
  init_rtc_clock() ;                                        // Initialize the RTC clock
  for ( int i = 0 ; i < 31 ; i++ )                          // Loop for 30 LED flashes
  {                                                         // end with LED off
    digitalToggle ( LED ) ;                                 // Turn LED on/off
    delay ( 100 ) ;                                         // Time to start serial monitor
  }
  debug_printf ( "Started at %s...\n",                      // Show alive message
                 get_rtc_time() ) ;
  os_init ( NULL ) ;                                        // Initialize lmic
  LMIC_reset() ;                                            // Reset the MAC state
  setchannels() ;                                           // Set LoRa channels
  retrieve_fcnt() ;                                         // Retrieve Uplink counter from RTC/EEPROM
  if ( LoraBand == REGION_AU915 )                           // Are we in NZ?
  {
    // Default setting is all channels 0..71 are enabled, but we want only 8..15
    for ( chan = 0 ; chan < 72 ; chan++ )                   // Disable most bands
    {
      if ( chan < 8 || chan > 15 )                          // Only leave bands 8..15
      {
        LMIC_disableChannel ( chan ) ;                      // Disable others
      }
    }
  }
  debug_printf ( "Start JOIN...\n" ) ;                      // For ABP this should be fast
  digitalWrite ( LED, LOW ) ;                               // Signal activity
  if ( JoinMode == JOINMODE_OTAA )                          // OTAA method?
  {
    debug_printf ( "xmitcount is %d\n", xmitcount ) ;
    if ( ( xmitcount < REJOIN_LIMIT ) &&                    // Yes, already joined?
         ( eepromdata.joinedFlag == true ) )
    {
      DevAddr = eepromdata.devaddr ;                        // Yes, use saved devadress and keys
      memcpy ( NwkSKey, eepromdata.nwkSKey, 16 ) ;
      memcpy ( AppSKey, eepromdata.appSKey, 16 ) ;
      setBackupRegister ( BKP_R_XMITCNT, ++xmitcount ) ;    // Update xmitcount in BKP register
      JoinMode = JOINMODE_ABP ;                             // Force ABP-mode
      debug_printf ( "OTAA join already made\n" ) ;         // Show for debug
    }
    else
    {
      debug_printf ( "Join with OTAA\n" ) ;                 // (Re)Join
      xmitcount = 0 ;                                       // Reset xmit counter
      setBackupRegister ( BKP_R_XMITCNT, xmitcount ) ;      // Save in RTC memory
      eepromdata.joinedFlag = false ;                       // Set to not joined
      saveEEPROMdata() ;                                    // Save in EEPROM
    }
  }
  if ( JoinMode == JOINMODE_ABP )
  {
    LMIC_setSession ( 0x1, DevAddr,
                      (const uint8_t*)NwkSKey,
                      (const uint8_t*)AppSKey ) ;
    LMIC.seqnoUp = eepromdata.fcnt ;                          // Set uplink frame counter
    debug_printf ( "ABP Framecount set to %d\n",
                   eepromdata.fcnt ) ;
    #if defined(CFG_eu868)
    // Set up the channels used by the Things Network, which corresponds
    // to the defaults of most gateways. Without this, only three base
    // channels from the LoRaWAN specification are used, which certainly
    // works, so it is good for debugging, but can overload those
    // frequencies, so be sure to configure the full frequency range of
    // your network here (unless your network autoconfigures them).
    // Setting up channels should happen after LMIC_setSession, as that
    // configures the minimal channel set.
    // NA-US channels 0-71 are configured automatically
    // LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(SF12, SF7),  CAP_CENTI);      // g-band
    // LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(SF12, SF7B), CAP_CENTI);      // g-band
    // LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(SF12, SF7),  CAP_CENTI);      // g-band
    // LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(SF12, SF7),  CAP_CENTI);      // g-band
    // LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(SF12, SF7),  CAP_CENTI);      // g-band
    // LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(SF12, SF7),  CAP_CENTI);      // g-band
    // LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(SF12, SF7),  CAP_CENTI);      // g-band
    // LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(SF12, SF7),  CAP_CENTI);      // g-band
    // LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(FSK,  FSK),  CAP_MILLI);      // g2-band
    // TTN defines an additional channel at 869.525Mhz using SF9 for class B
    // devices' ping slots. LMIC does not have an easy way to define set this
    // frequency and support for class B is spotty and untested, so this
    // frequency is not configured here.
    #endif
  }
  digitalWrite ( LED, HIGH ) ;                                // End of activity
  send_packet ( &sendjob ) ;
}


//**************************************************************************************************
//                                       L O O P                                                   *
//**************************************************************************************************
// Main loop of the program.                                                                       *
//**************************************************************************************************
void loop()
{
  uint32_t sleeptime ;                                    // Computed time to sleep (msec)
  uint32_t sleeptime_sec ;                                // Sleeptime in seconds

  os_runstep() ;                                          // Keep lmic happy
  if ( tx_finished )                                      // Packet sent?
  {
    tx_finished = false ;
    if ( ! eepromdata.joinedFlag )                        // Need to write the EEPROM?
    {
      saveOTAAkeys() ;                                    // Yes, save the keys for later use
      showOTAAkeys() ;                                    // Show the keys
    }
    MODIFY_REG ( PWR->CR3, PWR_CR3_EWRFBUSY,              // Prevent radio busy interference with sleep
                 LL_PWR_RADIO_BUSY_TRIGGER_NONE ) ;
    sleeptime = tx_interval_sec * 1000 - millis() - 50 ;  // Compute sleep time
    sleeptime_sec = sleeptime / 1000 ;                    // Also in seconds
    if ( sleeptime_sec > tx_interval_sec )                // Run time > sleep time?
    {
      sleeptime = tx_interval_sec * 1000 ;                // Adjust to prevent a very long sleep
      sleeptime_sec = tx_interval_sec ;
    }
    debug_printf ( "Start deep sleep at %s for %d sec\n", // Show go to sleep
                   get_rtc_time(), sleeptime_sec ) ;
    delay ( 50 ) ;                                        // Time to print last line
    LowPower.begin() ;                                    // Init low power mode
    LowPower.shutdown ( sleeptime ) ;                     // Sleep till next xmit interval
    while ( true ) {} ;                                   // Will not be executed
  }
}
