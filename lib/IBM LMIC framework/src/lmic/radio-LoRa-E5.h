#ifndef _radiolorae5_h_
  #define _radiolorae5_h_
  extern "C"{
   uint16_t GetIrqStatus (void) ;
   void ClearIrqStatus (uint16_t mask) ;
  }
#endif
