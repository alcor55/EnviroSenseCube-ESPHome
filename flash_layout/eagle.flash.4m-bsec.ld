/* 4 MiB flash: same firmware/OTA and preference addresses as eagle.flash.4m.ld.
 * Reserve 32 KiB from the previously unused region for a LittleFS backup.
 */
MEMORY
{
  dport0_0_seg : org = 0x3FF00000, len = 0x10
  dram0_0_seg : org = 0x3FFE8000, len = 0x14000
  irom0_0_seg : org = 0x40201010, len = 0xfeff0
}
PROVIDE ( _FS_start = 0x405F3000 );
PROVIDE ( _FS_end = 0x405FB000 );
PROVIDE ( _FS_page = 0x100 );
PROVIDE ( _FS_block = 0x2000 );
PROVIDE ( _EEPROM_start = 0x405FB000 );
PROVIDE ( _SPIFFS_start = 0x405F3000 );
PROVIDE ( _SPIFFS_end = 0x405FB000 );
PROVIDE ( _SPIFFS_page = 0x100 );
PROVIDE ( _SPIFFS_block = 0x2000 );
INCLUDE "local.eagle.app.v6.common.ld"
