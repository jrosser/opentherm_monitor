#include <opentherm.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

#include <DS1307RTC.h>
#include <TimeLib.h>

// Arduino UNO
#define THERMOSTAT_IN 2
#define THERMOSTAT_OUT 4
#define BOILER_IN 3
#define BOILER_OUT 5

OpenthermData message;

// override the maximum CH setpoint reported by the boiler to the thermostat
#define MAX_CH_SETPOINT_OVERRIDE 70

// override the maximum modulation level written to the boiler from the thermostat
#define MAX_MODULATION_LEVEL_OVERRIDE 75

// Set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x27, 20, 4);

//globals
unsigned char ch_setpoint = 0;
unsigned char max_ch_setpoint = 0;
unsigned char flow_temperature = 0;
unsigned char dhw_setpoint = 0;
unsigned char dhw_temperature = 0;
unsigned char modulation_level = 0;
unsigned char max_modulation_level = 0;

unsigned char ch_active = 0;
unsigned char dhw_active = 0;
unsigned char dhw_time_window = 0;
unsigned char flame_active = 0;

tmElements_t tm;

const char *monthName[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

bool getTime(const char *str)
{
  int Hour, Min, Sec;

  if (sscanf(str, "%d:%d:%d", &Hour, &Min, &Sec) != 3) return false;
  tm.Hour = Hour;
  tm.Minute = Min;
  tm.Second = Sec;
  return true;
}

bool getDate(const char *str)
{
  char Month[12];
  int Day, Year;
  uint8_t monthIndex;

  if (sscanf(str, "%s %d %d", Month, &Day, &Year) != 3) return false;
  for (monthIndex = 0; monthIndex < 12; monthIndex++) {
    if (strcmp(Month, monthName[monthIndex]) == 0) break;
  }
  if (monthIndex >= 12) return false;
  tm.Day = Day;
  tm.Month = monthIndex + 1;
  tm.Year = CalendarYrToTm(Year);
  return true;
}

void print2digits(int number) {
  if (number >= 0 && number < 10) {
    Serial.write('0');
  }
  Serial.print(number);
}

void update_display(void)
{
  lcd.backlight();
  RTC.read(tm);

  lcd.setCursor(0, 0); lcd.print("CH:"); lcd.print(ch_active);
  lcd.setCursor(6, 0); lcd.print("Set:"); lcd.print(ch_setpoint);
  if(ch_setpoint < 10) lcd.print(" "); 
  lcd.setCursor(13,0); lcd.print("Flow:"); lcd.print(flow_temperature);
  if(flow_temperature < 10) lcd.print(" ");

  lcd.setCursor(0, 1); lcd.print("FL:"); lcd.print(flame_active);
  lcd.setCursor(6, 1); lcd.print("Max:"); lcd.print(max_ch_setpoint);
  if(max_ch_setpoint < 10) lcd.print(" ");
  lcd.setCursor(13,1); lcd.print("Modn:"); lcd.print(modulation_level);
  if(modulation_level < 10) lcd.print(" ");
    
  lcd.setCursor(0, 2); lcd.print("HW:"); lcd.print(dhw_active);
  lcd.setCursor(6, 2); lcd.print("Set:"); lcd.print(dhw_setpoint);
  if(dhw_setpoint < 10) lcd.print(" ");
  lcd.setCursor(13,2); lcd.print("Tank:"); lcd.print(dhw_temperature);
  if(dhw_temperature < 10) lcd.print(" ");

  lcd.setCursor(6, 3); lcd.print("Ena:"); lcd.print(dhw_time_window);
  
  lcd.setCursor(12, 3);
  if(tm.Hour < 10)   lcd.print("0"); lcd.print(tm.Hour); lcd.print(":");
  if(tm.Minute < 10) lcd.print("0"); lcd.print(tm.Minute);  lcd.print(":");
  if(tm.Second < 10) lcd.print("0"); lcd.print(tm.Second); 
}

void setup() {
  pinMode(THERMOSTAT_IN, INPUT);
  digitalWrite(THERMOSTAT_IN, HIGH); // pull up
  digitalWrite(THERMOSTAT_OUT, HIGH);
  pinMode(THERMOSTAT_OUT, OUTPUT); // low output = high current, high output = low current
  pinMode(BOILER_IN, INPUT);
  digitalWrite(BOILER_IN, HIGH); // pull up
  digitalWrite(BOILER_OUT, HIGH);
  pinMode(BOILER_OUT, OUTPUT); // low output = high voltage, high output = low voltage

  Serial.begin(115200);

  lcd.init();
  update_display();

  if (RTC.read(tm)) {
    Serial.print("Ok, Time = ");
    print2digits(tm.Hour);
    Serial.write(':');
    print2digits(tm.Minute);
    Serial.write(':');
    print2digits(tm.Second);
    Serial.print(", Date (D/M/Y) = ");
    Serial.print(tm.Day);
    Serial.write('/');
    Serial.print(tm.Month);
    Serial.write('/');
    Serial.print(tmYearToCalendar(tm.Year));
    Serial.println();
  } else {
    if (RTC.chipPresent()) {
      Serial.println("The DS1307 is stopped.");
      Serial.println("Initialising RTC time using compiler timestamp.");
      Serial.println();

      // get the date and time the compiler was run
      if (getDate(__DATE__) && getTime(__TIME__)) {
          // and configure the RTC with this info
          if (RTC.write(tm)) {
        } else {
          Serial.print("Could not parse info from the compiler, Time=\"");
          Serial.print(__TIME__);
          Serial.print("\", Date=\"");
          Serial.print(__DATE__);
          Serial.println("\"");
        }
      }
    } else {
      Serial.println("DS1307 read error!  Please check the circuitry.");
      Serial.println();
    }
  }
}

void increment_hours()
{
  tm.Hour++;
  if(tm.Hour > 23) tm.Hour=0;
  RTC.write(tm);
}

void decrement_hours()
{
  if(tm.Hour == 0) tm.Hour=24;
  tm.Hour--;
  RTC.write(tm);
}

void increment_minutes()
{
  tm.Minute++;
  if(tm.Minute > 59) tm.Minute=0;
  RTC.write(tm);
}

void decrement_minutes()
{
  if(tm.Minute == 0) tm.Minute=60;
  tm.Hour--;
  RTC.write(tm);
}

#define MODE_LISTEN_MASTER 0
#define MODE_LISTEN_SLAVE 1

int mode = 0;

/**
 * Loop will act as man in the middle connected between Opentherm boiler and Opentherm thermostat.
 * It will listen for requests from thermostat, forward them to boiler and then wait for response from boiler and forward it to thermostat.
 * Requests and response are logged to Serial on the way through the gateway.
 */
void loop() {
  if (mode == MODE_LISTEN_MASTER) {
    if (OPENTHERM::isSent() || OPENTHERM::isIdle() || OPENTHERM::isError()) {
      OPENTHERM::listen(THERMOSTAT_IN);
    }
    else if (OPENTHERM::getMessage(message)) {

      /* status request from thermostat to boiler*/
      if(message.type == OT_MSGTYPE_READ_DATA && message.id == OT_MSGID_STATUS) {

        /* run hot water only between hours of 08:00 and 20:00 */
        unsigned char mask;
        if(tm.Hour > 7 and tm.Hour < 20) {
          dhw_time_window = 1;
          mask = 0xFF;
        }
        else {
          //disable DHWenable (bit 1)
          dhw_time_window = 0;
          mask = 0xFD;
        }

        message.valueHB &= mask;
      }

      /* override the maximum modulation level set by the thermostat into the boiler */
      if(message.type == OT_MSGTYPE_WRITE_DATA && message.id == OT_MSGID_MAX_MODULATION_LEVEL) {
        message.valueHB = MAX_MODULATION_LEVEL_OVERRIDE;
      }

      Serial.print(F("-> "));
      OPENTHERM::printToSerial(message);
      Serial.println();
      OPENTHERM::send(BOILER_OUT, message); // forward message to boiler
      mode = MODE_LISTEN_SLAVE;
    }
  }
  else if (mode == MODE_LISTEN_SLAVE) {
    if (OPENTHERM::isSent()) {
      OPENTHERM::listen(BOILER_IN, 800); // response need to be send back by boiler within 800ms
    }
    else if (OPENTHERM::getMessage(message)) {

      /* status read back from boiler */
      if(message.type == OT_MSGTYPE_READ_ACK && message.id == OT_MSGID_STATUS) {
        ch_active = ((message.valueLB & 0x02) == 0x02) ? 1 : 0;
        dhw_active = ((message.valueLB & 0x04) == 0x04) ? 1 : 0;
        flame_active = ((message.valueLB & 0x08) == 0x08) ? 1 : 0;
      }

      /* CH setpoint */
      if(message.type == OT_MSGTYPE_WRITE_ACK && message.id == OT_MSGID_CH_SETPOINT)
        ch_setpoint = message.valueHB;

      /* max modulation level */
      if(message.type == OT_MSGTYPE_WRITE_ACK && message.id == OT_MSGID_MAX_MODULATION_LEVEL)
        max_modulation_level = message.valueHB;

      /* CH max setpoint */
      /* override the max CH setpoint reported by the boiler to the thermostat */
      if(message.type == OT_MSGTYPE_READ_ACK && message.id == OT_MSGID_MAX_CH_SETPOINT) {
        message.valueHB = MAX_CH_SETPOINT_OVERRIDE;
        max_ch_setpoint = message.valueHB;
      }

      /* flow temperature*/
      if(message.type == OT_MSGTYPE_READ_ACK && message.id == OT_MSGID_FEED_TEMP)
        flow_temperature = message.valueHB;

      /* dhw temperature */
      if(message.type == OT_MSGTYPE_READ_ACK && message.id == OT_MSGID_DHW_TEMP)
        dhw_temperature = message.valueHB;

      /* dhw setpoint */
      if(message.type == OT_MSGTYPE_READ_ACK && message.id == OT_MSGID_DHW_SETPOINT)
        dhw_setpoint = message.valueHB;

      /* modulation level */
      if(message.type == OT_MSGTYPE_READ_ACK && message.id == OT_MSGID_MODULATION_LEVEL)
        modulation_level = message.valueHB;
           
      Serial.print(F("<- "));
      OPENTHERM::printToSerial(message);
      Serial.println();
      Serial.println();      
      OPENTHERM::send(THERMOSTAT_OUT, message); // send message back to thermostat
      mode = MODE_LISTEN_MASTER;
    }
    else if (OPENTHERM::isError()) {
      mode = MODE_LISTEN_MASTER;
      Serial.println(F("<- Timeout"));
      Serial.println();
    }
  }

  update_display();

  if(Serial.available()) {
    
    switch(Serial.read()) {
      case 'h':
        increment_hours();
        break;
      case 'H':
        decrement_hours();
        break;
      case 'm':
        increment_minutes();
        break;
      case 'M':
        decrement_minutes();
        break;
      default:
        break;
    }
  }
  
}
