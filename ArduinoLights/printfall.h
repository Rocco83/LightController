/* 
 * http://playground.arduino.cc/Main/Printf
 * way to handle printf
 * 
 * p("%s", "Hello world");
 * p("%s\n", "Hello world"); // with line break
 * unsigned long a=0xFFFFFFFF;
 * p("Decimal a: %l\nDecimal unsigned a: %lu\n", a, a); 
 * p("Hex a: %x\n", a); 
 * p(F("Best to store long strings to flash to save %s"),"memory");
 */

void printfall(const __FlashStringHelper *fmt, ... ){
  char buf[128]; // resulting string limited to 128 chars
  va_list args;
  va_start (args, fmt);
#ifdef __AVR__
  vsnprintf_P(buf, sizeof(buf), (const char *)fmt, args); // progmem for AVR
#else
  vsnprintf(buf, sizeof(buf), (const char *)fmt, args); // for the rest of the world
#endif
  va_end(args);
  Serial.print(buf);
#ifdef DOSYSLOG
  Syslog.logger(1,5,"Arduino","-",buf);
#endif
}
