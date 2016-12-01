/*
Arduino Ethernet Script Server

Created Mars 4, 2014
Mikael Kindborg, Evothings AB

TCP socket server that accept commands for basic scripting
of the Arduino board.

This example is written for use with an Ethernet shield.

The API consists of the requests listed below.

Requests and responses end with a new line.

The input parameter n is a pin number ranging from 2 to 9.

The response is always a 4-character string with a
hex encoded number ranging from 0 to FFFF.

Possible response string values:

H (result from digital read)
L (result from digital read)
0 to 1023 - Analog value (result from analog read)

Set pin mode to OUTPUT for pin n: On
Response: None
Example: O5
Note: O is upper case letter o, not digit zero (0).

Set pin mode to INPUT for pin n: In
Response: None
Example: I5

Write LOW to pin n: Ln
Response: None
Example: L5

Write HIGH to pin n: Hn
Response: None
Example: H5

READ pin n: Rn
Response: "H" (HIGH) or "L" (LOW)
Example: R5 -> H

ANALOG read pin n: An
Response: int value as string (range "0" to "1023")
Example: A5 -> 42
*/

/*
 * For current settings, see connections.txt
 * 
 */

// comment it to avoid syslog usage
#define DOSYSLOG 1

// Include files.
#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h> // per scrivere il Board ID
// disabled: lock digital pins 10, 11, 12, and 13
//#include <SD.h> // to read and write content to SD card
#include "TrueRandom.h"
#ifdef DOSYSLOG
#include "Syslog.h"
#endif
// after all of the other libs
#include "printfall.h"



// Index to be readed on the EEPROM to get the unique board ID.
// This is the index in which the information are stored
#define BOARD_ID_INDEX 0
#define BOARD_ID_MIN 0
#define BOARD_ID_MAX 30
// read the information
byte board_id_code = EEPROM.read(BOARD_ID_INDEX);

// Enter a MAC address for your controller below, usually found on a sticker
// on the back of your Ethernet shield.
//byte mac[] = { 0x90, 0xA2, 0xDA, 0x0E, 0xD0, 0x93 };
byte mac[] = { 0x90, 0xA2, 0xDA, 0x0E, 0xD0, byte(board_id_code) };

// The IP address will be dependent on your local network.
// If you have IP network info, uncomment the lines starting
// with IPAddress and enter relevant data for your network.
// If you don't know, you probably have dynamically allocated IP adresses, then
// you don't need to do anything, move along.
// IPAddress ip(192,168,1, 177);
// IPAddress gateway(192,168,1, 1);
// IPAddress subnet(255, 255, 255, 0);

// Current IP status 
byte ipObtained = 0;
byte counter = 0;

// Create a server listening on the given port.
EthernetServer server(3300);

// TODO generalizzare
//char syslogServerName[] = "syslog";
//char syslogServerName[] = "syslog.gonzaga.retaggio.net";
//static const uint8_t syslogServerName[] = "syslog.gonzaga.retaggio.net";
byte syslogServerName[] = { 172,16,8,1 };


// do a temporary button pressure. debounce is the minimum time in order to do it
// the follow variables are long's because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
unsigned long uptime = millis();  // the last time the output pin was toggled
unsigned long debounceupper = 200;   // the debounce time in milliseconds, increase if the output flickers
//unsigned long debouncelower = 500;   // the debounce time in milliseconds, increase if the output flickers
unsigned long debouncelower = 50;   // the debounce time in milliseconds, increase if the output flickers
unsigned long manualdebounceupper = 900;   // the debounce time in milliseconds, it is the "input switch" time, se premo il tasto a mano servono N secondi
//#define MARK 5000
#define MARK 20000
unsigned long lastmark = uptime; 
unsigned int cyclesonmarktime = 0;
/*
 * Usable inputs:
 * 0, 1: needed to use Serial.begin()
 * From https://www.arduino.cc/en/Main/ArduinoBoardEthernet: NB: Pins 10, 11, 12 and 13 are reserved for interfacing with the Ethernet 
 * module and should not be used otherwise. This reduces the number of available pins to 9, with 4 available as PWM outputs.
 * Ho 7 pin usabili, quindi ne manca uno. devo usare quelli analogici.
 * https://www.arduino.cc/en/Tutorial/AnalogInputPins
 * Per semplicita', li uso come input. la digitalRead funziona ugualmente.
 * 
 *  digitalRead() works on all pins. It will just round the analog value received and present it to you. If analogRead(A0) is greater than or equal to 512, digitalRead(A0) will be 1, else 0.
 *  digitalWrite() works on all pins, with allowed parameter 0 or 1. digitalWrite(A0,0) is the same as analogWrite(A0,0), and digitalWrite(A0,1) is the same as analogWrite(A0,255)
 *  analogRead() works only on analog pins. It can take any value between 0 and 1023.
 *  analogWrite() works on all analog pins and all digital PWM pins. You can supply it any value between 0 and 255.
 */

#define RELEON LOW
#define RELEOFF HIGH
#define RELESWITCH 2

static const uint8_t inputPin[] = { A0, A1, A2, A3, A4 };
// inputPower false: it is a sensor. inputPower true: it is a button to turn on/off
static const bool inputPower[] = { false, false, false, true, true };
//static const unsigned long inputDebounce[] = { debounce, debounce, debounce, manualdebounce, manualdebounce };

static const uint8_t outputPin[] = { 2, 3, 4, 5, 6 };
// imposta per ognuno dei valori dell'array se lo switch e' temporaneo o meno
static const bool temporarySwitch[] = { true, true, true, false, false };

// outputPower imposta il rele come da valore dell'indice. 99 e' valido, per dire "no match"
// in sostanza, se outputPower[0] = 5, se l'input 0 e' alto, allora accende il rele 5. se e' spento, lo spegne
// utile in questo caso perche' l'input e' temporaneo
// 
static const uint8_t outputPower[] = { 4, 99, 99, 99, 99  };

// handle 5 input. Needed to know if the remote rele' is currently HIGH or LOW
// lastInputChange is a timer
unsigned long lastInputChange[] = { 0, 0, 0, 0, 0 };
// lastInputValues contains the previous values
// set to 2 in order to have the first read
//unsigned int lastInputValues[] = { 2, 2, 2, 2, 2 };
// lastInputValues contains the current values
// set to 2 in order to have the current read
unsigned int currentInputValues[] = { 2, 2, 2, 2, 2 };
unsigned int newInputValues[] = { 2, 2, 2, 2, 2 };

// handle 4 output. Needed to turn ON or OFF the rele'. Currently change status only through network.
unsigned long lastOutputChange[] = { 0, 0, 0, 0, 0 };
// sembra non usasto
//unsigned int lastOutputValues[] = { RELEOFF, RELEOFF, RELEOFF, RELEOFF, RELEOFF };
unsigned int currentOutputValues[] = { RELEOFF, RELEOFF, RELEOFF, RELEOFF, RELEOFF };






  /*for (counter = 0; counter < sizeof(inputPin) - 1; counter++) {
    Serial.print("Input");
    Serial.print(counter+1);
    Serial.print(": '");
    Serial.print(digitalRead(inputPin[counter]));
    Serial.print("', ");
  }
  Serial.println(uptime);
  delay(500);*/
// read all inputs needed

/*
 * Take in input also the new state to be assigned
 */
// funzione generica per cambiare parametro, supporta sia i permanent switch che i temporary switch
void switchValue(byte arrayIndex, int newOutputValue) {

  // print the current input values
  /*Serial.print("switchValue: called with array index: '");
  Serial.print(arrayIndex);
  Serial.print("', new value to be set: '");
  Serial.print(newOutputValue);
  Serial.println("'");*/
  printfall(F("switchValue: called with array index: '%d', new value to be set: '%d'\r\n"), arrayIndex, newOutputValue);


  // if new value must be RELEOFF OR currently ON and new value must be RELESWITCH
  if ( newOutputValue == RELEOFF || ( newOutputValue == RELESWITCH && currentOutputValues[arrayIndex] == RELEON ) ) {
    // il valore corrente e' alto, quindi deve diventare basso. in questo caso, temporary non esiste (non auto-accendo)
    // impostando lastOutputChange a 0, non verra' piu' fatto parsing (prossimo giro)
    lastOutputChange[arrayIndex] = 0;
    // imposto il valore basso, "pulsante rilasciato"
    //setOutputPin(arrayIndex, LOW);
    newOutputValue = RELEOFF;
    //Serial.print("switchValue: turning off (HIGH) output: ");
    printfall(F("switchValue: turning off (HIGH) output: '%d'\r\n"), outputPin[arrayIndex]);
  
  // if new value must be RELEON OR currently OFF and new value must be RELESWITCH
  } else if ( newOutputValue == RELEON || ( newOutputValue == RELESWITCH && currentOutputValues[arrayIndex] == RELEOFF ) ) {
    // controllo se l'output e' temporaneo (pulsante) o permanente (interruttore)
    // se il pulsante e' temporaneo, lastOutputChange[arrayIndex] fara' in modo che si auto-resetti
    if ( temporarySwitch[arrayIndex] == true ) {
      // impostando lastOutputChange ad uptime, il check verifichera' ad ogni giro se e' passato il tempo e resetta.
      lastOutputChange[arrayIndex] = uptime;
    }
    // imposto il valore alto, "pulsante premuto"
    //setOutputPin(arrayIndex, HIGH);
    newOutputValue = RELEON;
    //Serial.print("switchValue: turning on (LOW) output: ");
    printfall(F("switchValue: turning on (LOW) output: '%d'\r\n"),outputPin[arrayIndex]);

  // not handled case
  } else {
    //Serial.println("switchValue: wrong input. doing nothing.");
    printfall(F("switchValue: wrong input. doing nothing.\r\n"));
    return;
  }
  //Serial.println(outputPin[arrayIndex]);
  // copio il precedente valore (storico) // attualmente ignorato (sara' sempre l'opposto, inutile)
  //lastOutputValues[arrayIndex] = currentOutputValues[arrayIndex];
  // imposto il valore corrente (per coerenza)
  currentOutputValues[arrayIndex] = newOutputValue;
  // scrivo il pin di output
  digitalWrite(outputPin[arrayIndex], newOutputValue);
}


void parseInputValues() {
  unsigned int tempread = 99;
  bool inputchanged = false;
  
  // cycle over the array, looking at the global array to see for input names
  for (counter = 0; counter < sizeof(inputPin); counter++) {
    // read the current value, but we have not yet decided if we will store it
    tempread = digitalRead(inputPin[counter]);

    // debug
    // considera currentInputValues per verificare la differenza con l'input attuale
    if ( tempread != currentInputValues[counter] ) {
      printfall(F("DEBUG uptime: %lu Input number: %d pin number: %d old value: %d new value: %d\r\n"), uptime, counter, inputPin[counter], currentInputValues[counter], tempread);
      inputchanged = true;
    }

    // questo ad ogni cambio di input resetta il timer per l'input
    // considera newInputValues per verificare la differenza con l'input futuro
    if ( tempread != currentInputValues[counter] ) {
      if ( tempread != newInputValues[counter] ) {
        newInputValues[counter] = tempread;
        lastInputChange[counter] = uptime;
      }
    }
 
    // questa funzione viene eseguita solo dopo N millisecondi
    if ( uptime - lastInputChange[counter] > debouncelower && newInputValues[counter] != currentInputValues[counter]) {
      // time has been passed AND the input is different from the previous one.
        // copy the value to the current array
      // moved here in order to let inputPower and outputPower work
      currentInputValues[counter] = newInputValues[counter];

      // set the last change time for this input
      //lastInputChange[counter] = uptime;
      // se sono nelle condizioni di inputPower, cioe' di un interruttore e non un sensore, allora devo fare lo switch sul relativo output
      if ( inputPower[counter] == true ) {
        
        //Serial.print("Power switch triggered. ");
        printfall(F("Power switch triggered. "));

        // devo eseguirlo solo alla pression del tasto, non anche al rilascio.
        //if ( uptime - lastInputChange[counter] < manualdebounceupper ) {
        if ( newInputValues[counter] == 1 ) {
          //Serial.println(" Changing current value.");
          printfall(F(" Changing current value.\r\n"));
          // non deve riflettere il valore in input, deve solo fare lo switch. non e' un sensore
          // switch on e off, gestiti da web.
          // TODO: su pressione lunga, spegne
          switchValue(counter, RELESWITCH);
          
          // set the last change time of the input
          //lastInputChange[counter] = uptime;
        } else {
          //Serial.println("Manual debounce time not elapsed, doing nothing");
          //printfall(F("Manual debounce time not elapsed, doing nothing\r\n"));
          //printfall(F("Manual debounce %lu, uptime %lu, lastInputChange %lu\r\n"), manualdebounceupper, uptime, lastInputChange[counter]);
          printfall(F("Input value = 0 , inputPower = true. must work only on button press, not also on button release\r\n"));
        }
      }

      // moved after the if, otherwise manualdebounce will never work
      // copy the old value to the array of old values
      //lastInputValues[counter] = currentInputValues[counter];
 
      // now handle the outputPower values
      if ( outputPower[counter] != 99 ) {
        /*Serial.print(F("Changing value for index: '"));
        Serial.print(counter);
        Serial.print(F("' changing the output number: '"));
        Serial.print(outputPower[counter]);
        Serial.print(F("' to: "));
        Serial.println(currentInputValues[counter]);*/
        printfall(F("Changing value for index: '%d' changing the output number: '%d' to: '%d'\r\n"), counter, outputPower[counter], currentInputValues[counter]);
        if ( currentInputValues[counter] == 0 ) {
          switchValue(outputPower[counter], RELEOFF);
        } else {
          switchValue(outputPower[counter], RELEON);
        }
      }
      // copy the value to the current array
      // moved here in order to let inputPower and outputPower work
      //currentInputValues[counter] = newInputValues[counter];
    }
  }
  // DEBUG
  // TODO, fix con printfall
  if ( inputchanged == true ) {
    Serial.println(F("Current Inputs."));
    for (counter = 0; counter < sizeof(inputPin); counter++) {
      Serial.print(F("Input '"));
      Serial.print(counter);
      Serial.print(F("' Value: '"));
      Serial.print(currentInputValues[counter]);
    Serial.print(F("' "));
    }
    Serial.println(F(""));
  }
}

/*
 * Verifico se uno degli attuali valori di output deve essere variato.
 * Serve per "rilasciare il pulsante", cosa che non puo' essere fatta nello stesso loop()
 * non serve la funzione di "pressione", che al momento esiste solo via network.
 * da creare anche input diretti su arduino (luce sopra la tv)
 */
void timeoutButtonParsing() {
  // cycle over the array, looking at the global array to see for output names
  for (counter = 0; counter < sizeof(outputPin); counter++) {
    // verifico se e' passato il timer di modifica // no, mi basta verificare se e' temporaneo
    // se lo switch e' permanente, lastOutputChange sara' sempre 0, quindi il totale < uptime
    // di conseguenza lo switch e' temporaneo

    // verifico il valore di debounce, e' inutile chiamare la funzione infinite volte
    // viene chiaamta solo nel caso in cui sia passato almeno debounceupper tempo
    if ( uptime > lastOutputChange[counter] + debounceupper) {
      // verifico se lo switch e' temporaneo. Lo e' sicuramenete, perche' altrimenti lastOutputChange e' uguale a 0.
      if ( temporarySwitch[counter] == true ) {      
        // controllo se l'output e' attualmente LOW
        if ( currentOutputValues[counter] == RELEON ) {
          // se e' acceso, lo spengo. dentro switchValue controllo se e' da fare o meno (debounce)
          //Serial.print("Simulating temporary button pressure: releasing the button: ");
          //Serial.println(outputPin[counter]);
          printfall(F("Simulating temporary button pressure: releasing the button: %d\r\n"), outputPin[counter]);
          switchValue(counter, RELESWITCH);
        }
      }
    }
  }
}

/*
 * Spengo tutti i rele, a prescindere dallo stato iniziale
 */
void shutdownAllOutput() {
  // cycle over the array, looking at the global array to see for output names
  for (counter = 0; counter < sizeof(outputPin); counter++) {
    
    // verifico se lo switch e' temporaneo. 
    // se lo switch e' temporaneo, non importa lo stato in uscita attuale ma quello in ingresso
    // TODO direi da correggere, in questo momento il codice controlla lo stato in uscita e non in ingreso
    if ( temporarySwitch[counter] == true ) {      
      // controllo se l'output e' attualmente LOW
      if ( currentOutputValues[counter] == RELEON ) {
        // se e' acceso, lo spengo. dentro switchValue controllo se e' da fare o meno (debounce)
        //Serial.print("Simulating temporary button pressure: releasing the button: ");
        //Serial.println(outputPin[counter]);
        printfall(F("Simulating temporary button pressure: releasing the button: %d\r\n"), outputPin[counter]);
        switchValue(counter, RELEOFF);
      }
    } else {
      // piu semplice, basta spegnere il rele'
      //Serial.print("switchValue: turning off (HIGH) output: ");
      //Serial.println(outputPin[counter]);
      printfall(F("shutdownAllOutput: turning off (HIGH) output: %d\r\n"), outputPin[counter]);
      currentOutputValues[counter] = RELEOFF;
      digitalWrite(outputPin[counter], RELEOFF);
    }
  }
}

/*
// funzione generica per cambiare parametro, supporta sia i permanent switch che i temporary switch
void switchValue(byte arrayIndex) {
  // old reference, if called with just 1 argument, it suppose to change the current rele status
  int newOutputValue = RELESWITCH;
  
  switchValue(arrayIndex, newOutputValue);
}
*/


// Read the request line,
String readRequest(EthernetClient* client)
{
  String request = "";

  // Loop while the client is connected.
  while (client->connected())
  {
  	// Read available bytes.
  	while (client->available())
  	{
  		// Read a byte.
  		char c = client->read();

  		// Print the value (for debugging).
  		Serial.write(c);

  		// Exit loop if end of line.
  		if ('\n' == c)
  		{
  			return request;
  		}

  		// Add byte to request line.
  		request += c;
  	}
  }
  return request;
}

// Read the command from the request string.
char readCommand(String* request)
{
  String commandString = request->substring(0, 1);
  return commandString.charAt(0);
}

// Read the parameter from the request string.
int readParam(String* request)
{
  // This handles a hex digit 0 to F (0 to 15).
  char buffer[2];
  buffer[0] = request->charAt(1);
  buffer[1] = 0;
  return (int) strtol(buffer, NULL, 16);
}

void sendResponse(EthernetClient* client, String response)
{
  // Send response to client.
  client->println(response);

  // Debug print.
  Serial.println(F("sendResponse:"));
  Serial.println(response);
}

void printServerStatus()
{
  Serial.print(F("Server address:"));
  Serial.println(Ethernet.localIP());
}

void executeRequest(EthernetClient* client, String* request)
{
  char command = readCommand(request);
  int n = readParam(request);
  if ('O' == command)
  {
  	pinMode(n, OUTPUT);
  }
  else if ('I' == command)
  {
  	pinMode(n, INPUT);
  }
  else if ('L' == command)
  {
  	digitalWrite(n, LOW);
  }
  else if ('H' == command)
  {
  	digitalWrite(n, HIGH);
  }
  else if ('R' == command)
  {
  	sendResponse(client, String(digitalRead(n)));
  }
  else if ('A' == command)
  {
  	sendResponse(client, String(analogRead(n)));
  }
  else if ('S' == command)
  {
    // qui mi riferisco all'indice dell'array
    switchValue(n, RELESWITCH);
  }
  else if ('D' == command)
  {
    shutdownAllOutput();
  }
}


boolean checkDhcp()
{
    /*
     * 0: nothing happened
     * 1: renew failed
     * 2: renew success
     * 3: rebind fail
     * 4: rebind success
     */
    ipObtained = Ethernet.maintain();
    switch( ipObtained ){
    case 0:
      return true;
      break;
    case 1:
      return false;
      break;
    case 2:
      return true;
      break;
    case 3:
      return false;
      break;
    case 4:
      return true;
      break;
    default:
      return false;
      break;
    }
}







void setup()
{
  // Start serial communication with the given baud rate.
  // NOTE: Remember to set the baud rate in the Serial
  // monitor to the same value.
  Serial.begin(9600);

  // Wait for serial port to connect. Needed for Leonardo only.
  while (!Serial) { ; }

  
  
  
  // Get the current ID of the board
  // se non e' numerico, allora imposto 0
  if ( board_id_code <= BOARD_ID_MIN || board_id_code >= BOARD_ID_MAX )
  {
    // byte can handle from 0 to 254. currently handling 0-30
    board_id_code = TrueRandom.random(BOARD_ID_MIN,BOARD_ID_MAX);
    Serial.print(F("board id not valid, setting to '"));
    Serial.print(board_id_code);
    Serial.println(F("'"));
    // An EEPROM write takes 3.3 ms to complete. The EEPROM memory has a specified life of 100,000 write/erase cycles, 
    // so you may need to be careful about how often you write to it. 
    EEPROM.write(BOARD_ID_INDEX, board_id_code);

    // Changing mac address before starting network
    mac[5] = board_id_code;
  } else {
    Serial.print(F("Board ID: '"));
    Serial.print(board_id_code);
    Serial.println(F("'"));
    /*String logmessage = "Board ID: '" + String(board_id_code) + "'";
    Serial.println(logmessage.c_str());*/
    //Syslog.logger(1,6,"Arduino", logmessage.c_str());
  }
  
  // If it works to get a dynamic IP from a DHCP server, use this
  // code to test if you're getting a dynamic adress. If this
  // does not work, use the above method of specifying an IP-address.
  // dhcp test starts here
  Serial.println(F("Trying to configure Ethernet using DHCP"));
  if (Ethernet.begin(mac) == 0)
  {
  	Serial.println(F("Failed to configure Ethernet using DHCP"));
  	// No point in carrying on, stop here forever.
  	// DEBUG: no network, just for input
  	while(true) ;
  }
  // assign value to the global value that give me current status
  ipObtained = checkDhcp();
  // dhcp test end

  /*
   * Setup syslog messages
   */
  #ifdef DOSYSLOG
  Serial.println(F("Setting up Syslog Server"));
  Syslog.setLoghost(syslogServerName);
  //Serial.println(F("Syslog Server setup completed"));
  printfall(F("Syslog Server setup completed\r\n"));
  // 5 argomenti, il 4 deve essere il timestamp
  // NILVALUE puo essere usato al posto del timestamp
  // NILVALUE == "-"
  //Syslog.logger(1,5,"Arduino","-","setup started");
  #endif

  // La parte rete non e' ancora stata inizializata, uso Serial.print.
  Serial.print(F("Numbers of input configured: "));
  Serial.println(sizeof(inputPin));
  // initialize the pins, just the first real thing
  for (counter = 0; counter < sizeof(inputPin); counter++) {
    //Serial.println(counter);
    Serial.print(F("Set INPUT for pin "));
    Serial.print(inputPin[counter]);
    pinMode(inputPin[counter], INPUT);
    currentInputValues[counter]=digitalRead(inputPin[counter]);
    newInputValues[counter]=currentInputValues[counter];
    Serial.print(F(" Current value: "));
    Serial.println(currentInputValues[counter]);
  }

  Serial.print(F("Numbers of output configured: "));
  Serial.println(sizeof(outputPin));
  for (counter = 0; counter < sizeof(outputPin); counter++) {
    Serial.print(F("Set OUTPUT for pin "));
    Serial.println(outputPin[counter]);
    pinMode(outputPin[counter], OUTPUT);
    // TODO with function on boot, shut down all the lights
    digitalWrite(outputPin[counter], RELEOFF);
  }
 
  // verifico gli attuali input
  parseInputValues();
  // spegno tutto
  shutdownAllOutput();
 
  // Start the server.
  server.begin();

  // Print status.
  printServerStatus();

  
  // Initialized SD card. 
  /* Initializes the SD library and card. This begins use of the SPI bus 
   * (digital pins 11, 12, and 13 on most Arduino boards; 50, 51, and 52 on the Mega) 
   * and the chip select pin, which defaults to the hardware SS pin (pin 10 on most Arduino 
   * boards, 53 on the Mega). Note that even if you use a different chip select pin, 
   * the hardware SS pin must be kept as an output or the SD library functions will not work. 
   */
  //if (!SD.begin(4)) { return; }


  // TCP test
  /*EthernetClient client;

  if (client.connect(serverName, 80)) {
    Serial.println("connected");
    // Make a HTTP request:
    client.println("GET /search?q=arduino HTTP/1.0");
    client.println();
  } 
  else {
    // kf you didn't get a connection to the server:
    Serial.println("connection failed");
  }*/
  
}



void loop()
{
  // uptime is global. needed to parse objects
  // TODO: what if uptime reset?
  // idea: controllo se millis() < uptime, ed in tal caso reset dei parametri di lastChange
  uptime = millis();

  if ( lastmark + MARK <= uptime ) {
    lastmark = uptime;
    //Serial.print("MARK uptime ");
    //Serial.println(uptime);
    printfall(F("MARK uptime %lu, %u cycles on %d seconds\r\n"), uptime, cyclesonmarktime, MARK/1000);
    cyclesonmarktime=0;
  } else {
    cyclesonmarktime = cyclesonmarktime+1;
  }
  
  // Renew lease if needed
  // DEBUG
  while ( ! checkDhcp() ) {
    Serial.print(F("DHCP problem. Retrying. Current status: "));
    Serial.println(ipObtained);
  }

  // Verifico se ci sono degli output temporanei da abbassare (pulsante)
  timeoutButtonParsing();

  // get current input values and compare with previous one
  parseInputValues();
  
  // set now current output value and compare with previous one
  //Serial.println("Write the output values");
  //setCurrentOutputValues();
  
  // Listen for incoming client requests.
  EthernetClient client = server.available();
  if (!client) {
  	return;
  }

  //Serial.println("Client connected");
  printfall(F("Client connected%s"), "\r\n");

  String request = readRequest(&client);
  executeRequest(&client, &request);

  // Close the connection.
  //client.stop();

  //Serial.println("Client disonnected");
  printfall(F("Client disconnected%s"), "\r\n");
}


