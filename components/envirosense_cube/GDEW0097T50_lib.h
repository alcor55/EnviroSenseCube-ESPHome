#ifndef GDEW0097T50_LIB_H
#define GDEW0097T50_LIB_H

#include <Arduino.h>
#include <SPI.h>

#define isEPD_W21_BUSY digitalRead( 12 ) 
#define EPD_W21_RST_0 digitalWrite(  0, LOW )
#define EPD_W21_RST_1 digitalWrite(  0, HIGH )
#define EPD_W21_DC_0  digitalWrite( 15, LOW )
#define EPD_W21_DC_1  digitalWrite( 15, HIGH )
#define EPD_W21_CS_0  digitalWrite(  2, LOW )
#define EPD_W21_CS_1  digitalWrite(  2, HIGH )

#define EPD_WIDTH   88
#define EPD_HEIGHT  184
#define EPD_ARRAY  2024  

void SPI_Write(unsigned char value);
void EPD_W21_WriteCMD(unsigned char command);
void EPD_W21_WriteDATA(unsigned char datas);
void EPD_BeginOperation(unsigned long timeout_ms);
void EPD_EndOperation(void);
bool EPD_OperationTimedOut(void);
bool lcd_chkstatus(const char *phase);
bool EPD_lut(unsigned char LUTsel);
bool EPD_Init(void);
bool EPD_Init_Part(void);
bool EPD_DeepSleep(void);
void EPD_ForceSleep(void);
bool EPD_Update(void);
bool EPD_SetRAMValue_BaseMap( const unsigned char * datas);
bool EPD_SetRAMValue_Partial(const unsigned char *old_datas, const unsigned char *new_datas);

#endif



/*
    That's all folks!
*/
