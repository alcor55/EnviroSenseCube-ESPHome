#include "GDEW0097T50_lib.h"

#include "esphome/core/log.h"

static const char *const EPD_TAG = "envirosense_epaper";
static constexpr unsigned long EPD_BUSY_TIMEOUT_MS = 8000;
static unsigned long epd_operation_started_at = 0;
static unsigned long epd_operation_timeout_ms = 0;
static bool epd_operation_active = false;
static bool epd_operation_timed_out = false;

//SPI write byte
void SPI_Write(unsigned char value)
{				   			 
   SPI.transfer(value);
}

//SPI write command
void EPD_W21_WriteCMD(unsigned char command)
{
	EPD_W21_CS_0;
	EPD_W21_DC_0; // D/C# 0:command 1:data  
	SPI_Write(command);
	EPD_W21_CS_1;
}

//SPI write data
void EPD_W21_WriteDATA(unsigned char datas)
{
	EPD_W21_CS_0;
	EPD_W21_DC_1;  // D/C#   0:command  1:data
	SPI_Write(datas);
	EPD_W21_CS_1;
}

void EPD_BeginOperation(unsigned long timeout_ms)
{
  epd_operation_started_at = millis();
  epd_operation_timeout_ms = timeout_ms;
  epd_operation_active = true;
  epd_operation_timed_out = false;
}

void EPD_EndOperation(void)
{
  epd_operation_active = false;
}

bool EPD_OperationTimedOut(void)
{
  return epd_operation_timed_out;
}

//Busy function
bool lcd_chkstatus(const char *phase)
{ 
  const unsigned long busy_start = millis();
  while( millis() - busy_start < EPD_BUSY_TIMEOUT_MS &&
         (!epd_operation_active || millis() - epd_operation_started_at < epd_operation_timeout_ms) )
  { //=0 BUSY
    if(isEPD_W21_BUSY==1)
    {
      ESP_LOGD(EPD_TAG, "BUSY released during %s after %lu ms", phase, millis() - busy_start);
      return true;
    }
    ESP.wdtFeed(); // Feed dog to prevent system reset  
    yield();
  }
  const bool operation_limit_reached = epd_operation_active &&
      millis() - epd_operation_started_at >= epd_operation_timeout_ms;
  epd_operation_timed_out = operation_limit_reached;
  ESP_LOGE(EPD_TAG, "%s timeout during %s after %lu ms (operation=%lu ms, pin=%d)",
           operation_limit_reached ? "Operation" : "BUSY", phase, millis() - busy_start,
           epd_operation_active ? millis() - epd_operation_started_at : 0, isEPD_W21_BUSY);
  return false;
}  

bool EPD_lut(unsigned char LUTsel)      
{
  unsigned char tempfull,temppartial;
  EPD_W21_WriteCMD(0x40);
  if (!lcd_chkstatus(LUTsel == 0 ? "full LUT" : "partial LUT"))
    return false;
  tempfull =25;
  if((128>tempfull)&&(tempfull>54))   
    tempfull = 53 ;           
  else if(tempfull>128)         
    tempfull = 1;             
  
  temppartial = tempfull+60;      
  if(temppartial>128)           
    temppartial = 127;
  
  if(LUTsel==0)             
  {
    EPD_W21_WriteCMD(0xE0);         
    EPD_W21_WriteDATA(0x02);    
    EPD_W21_WriteCMD(0xE5);     
    EPD_W21_WriteDATA(tempfull); // define the Temperature value////      
  }
  else if(LUTsel==1)              
  {
    EPD_W21_WriteCMD(0xe0);
    EPD_W21_WriteDATA (0x02);
    EPD_W21_WriteCMD(0xe5);
    EPD_W21_WriteDATA (temppartial);
  }
  return true;
}

//JD79651
bool EPD_Init(void)
{ 
  EPD_W21_RST_0; // Module reset
  delay(10); // At least 10ms delay 
  EPD_W21_RST_1;
  delay(10); // At least 10ms delay 
  if (!lcd_chkstatus("full init reset"))
    return false;
  /******FiTi  init****************/
  EPD_W21_WriteCMD(0x4D); // FITIinternal code
  EPD_W21_WriteDATA (0xAA);

  EPD_W21_WriteCMD(0x87); // FITIinternal code
  EPD_W21_WriteDATA (0x28);

  EPD_W21_WriteCMD(0x88); // FITIinternal code
  EPD_W21_WriteDATA (0x80);

  EPD_W21_WriteCMD(0x90); // FITIinternal code
  EPD_W21_WriteDATA (0x02);

  EPD_W21_WriteCMD(0x91); // FITIinternal code
  EPD_W21_WriteDATA (0x8d);

  EPD_W21_WriteCMD(0xAA); // FITIinternal code
  EPD_W21_WriteDATA (0x0f);

  if (!EPD_lut(0))
    return false;

  EPD_W21_WriteCMD(0xE8);         
  EPD_W21_WriteDATA (0xA8); // A

  EPD_W21_WriteCMD(0x26);         
  EPD_W21_WriteDATA (0x0f); // A

  EPD_W21_WriteCMD(0x04);  
  if (!lcd_chkstatus("full init power-on"))
    return false;

  EPD_W21_WriteCMD(0x00); // panel setting
  EPD_W21_WriteDATA(0x1f); // KW-BF   KWR-AF  BWROTP 0f BWOTP 1f

  EPD_W21_WriteCMD(0X50);
  EPD_W21_WriteDATA(0x97);
  return true;
}

bool EPD_Init_Part(void)
{
  EPD_W21_RST_0; // Module reset
  delay(10); // At least 10ms delay 
  EPD_W21_RST_1;
  delay(10); // At least 10ms delay 
  if (!lcd_chkstatus("partial init reset"))
    return false;

  EPD_W21_WriteCMD(0x4D); // FITIinternal code
  EPD_W21_WriteDATA (0xAA);

  EPD_W21_WriteCMD(0x87); // FITIinternal code
  EPD_W21_WriteDATA (0x28);

  EPD_W21_WriteCMD(0x88); // FITIinternal code
  EPD_W21_WriteDATA (0x80);

  EPD_W21_WriteCMD(0x90); // FITIinternal code
  EPD_W21_WriteDATA (0x02);

  EPD_W21_WriteCMD(0x91); // FITIinternal code
  EPD_W21_WriteDATA (0x8d);

  EPD_W21_WriteCMD(0xAA); // FITIinternal code
  EPD_W21_WriteDATA (0x0f);

  if (!EPD_lut(1))
    return false;

  EPD_W21_WriteCMD(0xE8);         
  EPD_W21_WriteDATA (0xA8); // A

  EPD_W21_WriteCMD(0x26);         
  EPD_W21_WriteDATA (0x0f); // A

  EPD_W21_WriteCMD(0x04);  
  if (!lcd_chkstatus("partial init power-on"))
    return false;

  EPD_W21_WriteCMD(0x00); // panel setting
  EPD_W21_WriteDATA(0x1f); // KW-BF   KWR-AF  BWROTP 0f BWOTP 1f

  EPD_W21_WriteCMD(0X50);
  EPD_W21_WriteDATA(0xf7);
  return true;
}
  
bool EPD_DeepSleep(void)
{
  EPD_W21_WriteCMD(0X50); // VCOM AND DATA INTERVAL SETTING     
  EPD_W21_WriteDATA(0xf7); // WBmode:VBDF 17|D7 VBDW 97 VBDB 57    WBRmode:VBDF F7 VBDW 77 VBDB 37  VBDR B7  

  EPD_W21_WriteCMD(0X02); // power off
  if (!lcd_chkstatus("power-off")) {
    EPD_ForceSleep();
    return false;
  }
  delay(100);
  EPD_W21_WriteCMD(0X07); // deep sleep
  EPD_W21_WriteDATA(0xA5);
  return true;
}

void EPD_ForceSleep(void)
{
  // Best-effort shutdown after a BUSY timeout. Waiting again here would keep
  // the ESP awake when the BUSY connection is missing or stuck.
  EPD_W21_WriteCMD(0X02);
  delay(100);
  EPD_W21_WriteCMD(0X07);
  EPD_W21_WriteDATA(0xA5);
}


//Full screen refresh update function
bool EPD_Update(void)
{   
  //Refresh
  EPD_W21_WriteCMD(0x12); // DISPLAY REFRESH   
  delay(1); // !!!The delay here is necessary, 200uS at least!!!     
  return lcd_chkstatus("full refresh"); // waiting for the electronic paper IC to release the idle signal
}


bool EPD_SetRAMValue_BaseMap( const unsigned char * datas)
{
  unsigned int i; 
  EPD_W21_WriteCMD(0x10); // write old data 
  for(i=0;i<EPD_ARRAY;i++)
  {               
    EPD_W21_WriteDATA(0xFF);
  }
  EPD_W21_WriteCMD(0x13); // write new data 
  for(i=0;i<EPD_ARRAY;i++)
    EPD_W21_WriteDATA(datas[i]);
  return EPD_Update();     
}

bool EPD_SetRAMValue_Partial(const unsigned char *old_datas, const unsigned char *new_datas)
{
  const unsigned long partial_started_at = millis();
  if (!EPD_Init_Part())
    return false;
  ESP_LOGD(EPD_TAG, "Partial initialization completed after %lu ms", millis() - partial_started_at);

  EPD_W21_WriteCMD(0x91); // Enter partial mode
  EPD_W21_WriteCMD(0x90);

  // The entire 88x184 panel is one partial window. Supplying both frames makes
  // the update independent from controller RAM, which is lost in deep sleep.
  EPD_W21_WriteCMD(0x14); // Write old image
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(EPD_WIDTH);
  EPD_W21_WriteDATA(EPD_HEIGHT / 256);
  EPD_W21_WriteDATA(EPD_HEIGHT % 256);
  for (unsigned int i = 0; i < EPD_ARRAY; i++)
    EPD_W21_WriteDATA(old_datas[i]);

  EPD_W21_WriteCMD(0x15); // Write new image
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(EPD_WIDTH);
  EPD_W21_WriteDATA(EPD_HEIGHT / 256);
  EPD_W21_WriteDATA(EPD_HEIGHT % 256);
  for (unsigned int i = 0; i < EPD_ARRAY; i++)
    EPD_W21_WriteDATA(new_datas[i]);
  ESP_LOGD(EPD_TAG, "Partial old/new frames transferred after %lu ms", millis() - partial_started_at);

  EPD_W21_WriteCMD(0x16); // Refresh the same window
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(EPD_WIDTH);
  EPD_W21_WriteDATA(EPD_HEIGHT / 256);
  EPD_W21_WriteDATA(EPD_HEIGHT % 256);
  delay(1);
  if (!lcd_chkstatus("partial refresh"))
    return false;
  ESP_LOGI(EPD_TAG, "Full-screen partial refresh completed in %lu ms", millis() - partial_started_at);
  return true;
}



/*
    That's all folks!
*/
