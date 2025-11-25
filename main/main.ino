#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <SHA256.h>
#include "sha1.h"
#include "TOTP.h"
#include <ArduinoBLE.h>
#include <RTC.h>

//RFID and LCD
#define RST_PIN 9
#define SS_PIN 10

MFRC522 mfrc522(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 20, 4);

//Keypad setup
const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {2, 3, 4, 5};
byte colPins[COLS] = {6, 7, 8};
Keypad keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

//Time
long timeq = 1737753600;

//Menu items
const int menuItemsCount = 5;
const char* menuItems[] = {
  "Add Card",
  "Cards List",
  "Config 2FA",
  "Set PIN",
  "Save And Exit"
};

//System variables
int cards = 0;
bool logged = false;
bool config = true;
int step = 0;

//Menu navigation
int displayStartMenu = 0;
int displayStartMenuCards = 0;
int cardRemoving = 0;
int cardSelecting = 0;

//PIN management
char pin[7];
int pinLength = 0;
byte pinHash[32];
bool pinSet = false;

char enteredPin[7];
int enteredLength = 0;
bool checkingPin = false;

//Card management
byte storedCards[9][4];
int cardCount = 0;

//2FA
char* cardsSecrets[9] = {0};
TOTP* totps[9];
int foundId = 0;
char code2FA[7];
int code2FALength = 0;

//Animation
int dotAnim = 0;

//SETUP
void setup() {
  Serial.begin(9600);
  SPI.begin();
  mfrc522.PCD_Init();

  RTC.begin();
  
  lcd.init();
  lcd.backlight();
}

//MAIN LOOP
void loop() {
  char key = keypad.getKey();

  // Initial setup if no cards exist yet
  if (!logged && cardCount == 0 && config) {
    start_configuration(key);
  } 
  // Main menu when configuration mode is active
  else if (!logged && config) {
    main_menu(key);
  } 
  // If PIN mode active
  else if (key || checkingPin) {
    check_pin(key);
  } 
  // Default card scanning mode
  else if (!logged) {
    check_card(key);
  }
}

//CONFIGURATION MENU (FIRST MENU)
void start_configuration(char key) {
  switch (step) {
    case 0:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("1. Start config");
      step = 1;
      break;

    case 1:
      if (key == '1') {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("1. Add Card");
        step = 2;
      }
      break;

    case 2:
      if (key == '1') {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Scan card...");
        step = 3;
      }
      break;

    case 3:
      if(key == '*'){
          lcd.clear();
          step = 0;
          return;
      }
      if (check_new_card()) {
        add_new_card();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Card added!");
        delay(1000);
        step = 0;
      }
      break;
  }
}

//MAIN MENU
void main_menu(char key) {
  switch (step) {
    // Display menu page
    case 0:
      lcd.clear();
      lcd.setCursor(0, 0);
      displayStartMenu = 0;
      show_page(displayStartMenu);
      step = 1;
      break;

    case 1:
      if (key == '1' && displayStartMenu==0) { // Add new card
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Scan card...");
        step = 2;
      } 
      else if (key == '2' && displayStartMenu==0) { // Show all cards
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("All cards:");
        delay(1500);
        show_page_cards(displayStartMenuCards);
        step = 3;
      }
      else if (key == '3' && displayStartMenu==2) { // 2FA CONFIG
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Select card for 2FA");
        delay(1500);
        show_page_cards(displayStartMenuCards);
        step = 6;
      }  
      else if (key == '4' && displayStartMenu==2) { // Set PIN
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Set PIN");
        pinLength = 0;
        step = 5;
      } 
      else if (key == '5' && displayStartMenu==4) { // Save configuration
        lcd.clear();
        if (!pinSet) {
          lcd.setCursor(0, 0);
          lcd.print("Please set PIN!");
          delay(1000);
          step = 0;
        } else {
          lcd.setCursor(0, 0);
          lcd.print("Locked");
          delay(3000);
          lcd.clear();
          config = false;
        }
      } 
      else if (key == '#') { // Scroll through menu
        displayStartMenu += 2;
        if (displayStartMenu >= menuItemsCount) displayStartMenu = 0;
        show_page(displayStartMenu);
      }
      break;

    case 2: // Adding new card
      if(key == '*'){
        lcd.clear();
        step = 0;
        return;
      }
      if (check_new_card()) {
        add_new_card();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Card added!");
        delay(1000);
        step = 0;
      }
      break;

    case 3: // Cards list
      if (key == '#') {
        displayStartMenuCards += 2;
        if (displayStartMenuCards >= cardCount) displayStartMenuCards = 0;
        show_page_cards(displayStartMenuCards);
      } 
      else if (key == '*') {
        displayStartMenuCards = 0;
        step = 0;
      } 
      else if (key && key != '0') { // Remove card
        int index = key - '0';
        if (index <= cardCount) {
          char uidStr[16];
          format_UID(uidStr, storedCards[index - 1], 4);
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Remove? N-* Y-#");
          lcd.setCursor(0, 1);
          lcd.print(uidStr);
          step = 4;
          cardRemoving = index - 1;
        }
      }
      break;

    case 4: // Confirm card removal
      if (key == '#') {
        displayStartMenuCards = 0;
        remove_card(cardRemoving);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Removed");
        delay(1000);
        step = 0;
      } 
      else if (key == '*') {
        show_page_cards(displayStartMenuCards);
        step = 3;
      }
      break;

    case 5: // Set new PIN
      if (key) {
        if (key >= '0' && key <= '9' && pinLength < 6) {
          pin[pinLength++] = key;
          pin[pinLength] = '\0';
        } 
        else if (key == '*') { // Backspace or exit
          if (pinLength > 0) {
            pinLength--;
            pin[pinLength] = '\0';
          } else step = 0;
        } 
        else if (key == '#' && pinLength >= 4 && pinLength <= 6) { // Save PIN
          hash_pin(pin, pinHash);
          memset(pin, 0, sizeof(pin));
          pinLength = 0;
          pinSet = true;

          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("PIN saved");
          delay(1000);
          step = 0;
        }

        // Display PIN as stars
        lcd.setCursor(0, 1);
        lcd.print("      ");
        for (int i = 0; i < pinLength; i++) {
          lcd.setCursor(i, 1);
          lcd.print("*");
        }
      }
      break;
    case 6:
      if (key == '#') {
        displayStartMenuCards += 2;
        if (displayStartMenuCards >= cardCount) displayStartMenuCards = 0;
        show_page_cards(displayStartMenuCards);
      } 
      else if (key == '*') {
        displayStartMenuCards = 0;
        step = 0;
      } 
      else if (key && key != '0') { // Select card
        int index = key - '0';
        if (index <= cardCount) {
          char uidStr[16];
          format_UID(uidStr, storedCards[index - 1], 4);
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Selected card:");
          lcd.setCursor(0, 1);
          lcd.print(uidStr);
          delay(2000);
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Waiting for pair...");
          step = 7;
          cardSelecting = index - 1;
        }
      }
    case 7:
      if(configureTotp()){ //true -> if connected
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Confirm *-No #-Yes");
        step = 8;
      }
      if (key == '*') {
        show_page_cards(displayStartMenuCards);
        step = 6;
      }
    case 8:
      if (key == '#') {
        int keyLen = 0;
        cardsSecrets[cardSelecting] = "SECRET"; //SECRET GET SECRET
        uint8_t* hmacKey = convertCharToKey(cardsSecrets[cardSelecting], &keyLen);
        totps[cardSelecting] = new TOTP(hmacKey, keyLen);
        //timeDif = time.time() - bf.time;
        //getTime -> time.time() + timeDif;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("All set!");
        delay(2000);
        step = 0;
      } 
      else if (key == '*') {
        show_page_cards(displayStartMenuCards);
        step = 6;
      } 
  }
}


//CARD MANAGEMENT

void add_new_card() {
  for (byte i = 0; i < 4; i++) {
    storedCards[cardCount][i] = mfrc522.uid.uidByte[i];
  }
  cardCount++;
  Serial.print("Card added: ");
  dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

bool check_new_card() {
  if(!mfrc522.PICC_IsNewCardPresent()){
    //Serial.println("PICC_IsNewCardPresent");
    return false;
  } 
  if(!mfrc522.PICC_ReadCardSerial()){
    //Serial.println("PICC_ReadCardSerial");
    return false;
  }
  dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  return true;
}

void remove_card(int index) {
  for (int i = index; i < cardCount - 1; i++) {
    memcpy(storedCards[i], storedCards[i + 1], 4);
  }
  cardCount--;
  Serial.print("Removed card #");
  Serial.println(index + 1);
}


//CARD READING
void check_card(char key) {
  if(step == 1){ //2FA CODE
    if(key){
      if (key >= '0' && key <= '9' && code2FALength < 6) {
          code2FA[code2FALength++] = key;
          code2FA[code2FALength] = '\0';
        } 
        else if (key == '*') { // Backspace or exit
          if (code2FALength > 0) {
            code2FALength--;
            code2FA[code2FALength] = '\0';
          } else step = 0;
        } 
        else if (key == '#' && pinLength == 6) {
          char* newCode = totps[foundId]->getCode(timeq); //GET CODE FROM TOTPS
          if(strcmp(code2FA, newCode) != 0) {
            code2FALength = 0;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("OK");
            delay(3000);
            step = 0;
          }else{
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Wrong code!");
            delay(3000);
          }
        }

        // Display PIN as stars
        lcd.setCursor(0, 1);
        lcd.print(String(code2FA) + "      ");
    }
  }
  static unsigned long lastAnimTime = 0;
  lcd.setCursor(0, 0);
  lcd.print("Locked");
  lcd.setCursor(0, 1);
  lcd.print("Scan card.");

  if (millis() - lastAnimTime >= 400) {
    lastAnimTime = millis();

    lcd.setCursor(0, 1);
    lcd.print("Scan card");

    dotAnim = (dotAnim + 1) % 3;
    lcd.setCursor(9, 1);
    lcd.print("   ");
    lcd.setCursor(9, 1);
    for (int i = 0; i <= dotAnim; i++) lcd.print(".");
  }

  if(!mfrc522.PICC_IsNewCardPresent()){
    return;
  } 
  if(!mfrc522.PICC_ReadCardSerial()){
    return;
  }

  // Compare UID with stored cards
  bool found = false;
  for (int i = 0; i < cardCount; i++) {
    if (memcmp(storedCards[i], mfrc522.uid.uidByte, 4) == 0) {
      found = true;
      foundId = i;
      break;
    }
  }

  // Display result
  lcd.clear();
  lcd.setCursor(0, 0);
  if (found) {
    if(cardsSecrets[foundId] != 0){
        lcd.print("Enter 2FA code:");
        step = 1;
        delay(2000);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Enter code");
        pinLength = 0;
        return;
    }else{
      lcd.print("Card authorized");
      Serial.println("Card authorized");
    }
    
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Unauthorized");
    lcd.setCursor(0, 1);
    lcd.print("card");
    Serial.println("Unauthorized card");
  }

  delay(3000);
  lcd.clear();
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

//PIN
void check_pin(char key) {
  if (!checkingPin) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Enter PIN:");
    enteredLength = 0;
    checkingPin = true;
  }

  if (key) {
    if (key >= '0' && key <= '9' && enteredLength < 6) {
      enteredPin[enteredLength++] = key;
      enteredPin[enteredLength] = '\0';
    } 
    else if (key == '*') {
      if (enteredLength > 0) enteredLength--;
      else checkingPin = false;
      enteredPin[enteredLength] = '\0';
    } 
    else if (key == '#' && enteredLength >= 4) { // Confirm PIN
      if (pinSet) {
        byte enteredHash[32];
        hash_pin(enteredPin, enteredHash);

        if (compare_hashes(enteredHash, pinHash, 32)) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("PIN OK");
          config = true;
          checkingPin = false;
          step = 0;
          enteredLength = 0;
          memset(enteredPin, 0, sizeof(enteredPin)); // clear input
          delay(1000);
          lcd.setCursor(0, 0);
          lcd.print("UNLOCKED");
          delay(2000);
        } else {
          // Wrong PIN animation
          for (int i = 0; i < 2; i++) {
            lcd.setCursor(10, 0); lcd.print(".");
            delay(300);
            lcd.print(".."); delay(300);
            lcd.print("..."); delay(300);
            lcd.print("   "); delay(300);
          }
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Wrong PIN");
          enteredLength = 0;
          memset(enteredPin, 0, sizeof(enteredPin));
          delay(3000);
          lcd.clear();
        }
        checkingPin = false;
      }
    }

    // Display typed PIN as stars
    lcd.setCursor(0, 1);
    lcd.print("      ");
    for (int i = 0; i < enteredLength; i++) {
      lcd.setCursor(i, 1);
      lcd.print("*");
    }
  }
}

void hash_pin(const char *input, byte *output) {
  SHA256 sha;
  sha.reset();
  sha.update((const byte*)input, strlen(input));
  sha.finalize(output, 32);
}

// Hash comparison
bool compare_hashes(const byte *a, const byte *b, size_t len) {
  byte diff = 0;
  for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
  return (diff == 0);
}

//FUNCTIONS
void dump_byte_array(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
  Serial.println();
}

void format_UID(char *out, byte *uid, byte len) {
  char tmp[6];
  out[0] = 0;
  for (byte i = 0; i < len; i++) {
    if (i > 0) strcat(out, ":");
    snprintf(tmp, sizeof(tmp), "%02X", uid[i]);
    strcat(out, tmp);
  }
}

// Show menu items
void show_page(int startIndex) {
  lcd.clear();
  for (int row = 0; row < 2; row++) {
    int idx = startIndex + row;
    lcd.setCursor(0, row);
    if (idx < menuItemsCount) {
      lcd.print(idx + 1);
      lcd.print(". ");
      lcd.print(menuItems[idx]);
    } else {
      lcd.print(" ");
    }
  }
}

// Show stored cards
void show_page_cards(int startIndex) {
  lcd.clear();
  for (int row = 0; row < 2; row++) {
    int idx = startIndex + row;
    lcd.setCursor(0, row);
    if (idx < cardCount) {
      lcd.print(idx + 1);
      lcd.print(". ");
      char uidStr[16];
      format_UID(uidStr, storedCards[idx], 4);
      lcd.print(uidStr);
    } else {
      lcd.print(" ");
    }
  }
}

uint8_t* convertCharToKey(const char* str, int* keyLength) {
    int len = 0;

    while (str[len] != '\0') {
        len++;
    }

    *keyLength = len;

    uint8_t* key = (uint8_t*)malloc(len);

    for (int i = 0; i < len; i++) {
        key[i] = (uint8_t)str[i];
    }

    return key;
}

bool configureTotp() {
  BLEService loginService("f47ac10b-58cc-4372-a567-0e02b2c3d479");

  BLECharacteristic pinCharacteristic("9c858901-8a57-4791-81fe-4c455b099bc9", BLEWrite, 20);
  BLECharacteristic timestampCharacteristic("3f2504e0-4f89-11d3-9a0c-0305e82c3301", BLEWrite, 20);
  BLECharacteristic keyCharacteristic("6ba7b810-9dad-11d1-80b4-00c04fd430c8", BLEWrite, 20);
  BLECharacteristic statusCharacteristic("123e4567-e89b-12d3-a456-426614174000", BLENotify | BLEIndicate | BLERead, 20);

  char* deviceName = "SafeVault";
  char* totpPin;
  generateRandomPin(totpPin);

  BLE.setLocalName(deviceName);
  BLE.setAdvertisedService(loginService);

  loginService.addCharacteristic(keyCharacteristic);
  loginService.addCharacteristic(pinCharacteristic);
  loginService.addCharacteristic(timestampCharacteristic);
  loginService.addCharacteristic(statusCharacteristic);
  BLE.addService(loginService);

  BLE.advertise();

  Serial.println("BLE Peripheral with PIN started");
  Serial.print("Device name: "); Serial.println(deviceName);
  Serial.print("Safety PIN: "); Serial.println(pin);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Name: ");
  lcd.print(deviceName);
  lcd.setCursor(1,0);
  lcd.print("PIN: ");
  lcd.print(totpPin);

  unsigned long startTime = millis();
  while (millis() - startTime < 180000) {
    BLEDevice central = BLE.central();

    if (central) {
      Serial.print("Connected to central: ");
      Serial.println(central.address());

      while (central.connected()) {

        if (timestampCharacteristic.written()) {
          int len = timestampCharacteristic.valueLength();
          char buf[21];
          memcpy(buf, timestampCharacteristic.value(), len);
          buf[len] = '\0';
          String timestampString = String(buf);

          uint32_t epochSeconds = timestampString.toInt();

          RTCTime currentTime = epochSecToRTCTime(epochSeconds);

          RTC.setTime(currentTime);

          Serial.print("Time set to: "); Serial.println(currentTime);
        }

        if (keyCharacteristic.written()) {
          int length = pinCharacteristic.valueLength();
          const uint8_t* data = pinCharacteristic.value();

          char receivedPin[length + 1];
          memcpy(receivedPin, data, length);
          receivedPin[length] = '\0';

          Serial.print("Received PIN: ");
          Serial.println(receivedPin);

          if (strcmp(receivedPin, totpPin) == 0) {
            int length = keyCharacteristic.valueLength();
            const uint8_t* data = keyCharacteristic.value();

            char receivedKey[length + 1];
            memcpy(receivedKey, data, length);
            receivedKey[length] = '\0';

            Serial.print("Received key: ");
            Serial.println(receivedKey);

            cardsSecrets[cardSelecting] = receivedKey;

            statusCharacteristic.writeValue("success");
            central.disconnect();
            BLE.end();

            return true;
          }
          else {
            Serial.println("Wrong PIN");
            statusCharacteristic.writeValue("failure");
            central.disconnect();
          }
        }
      }

      Serial.print("Disconnected from central: ");
      Serial.println(central.address());
    }
  }
  return false;
}

RTCTime epochSecToRTCTime(uint32_t epochSeconds) {
  time_t rawTime = epochSeconds;
  struct tm * timeinfo = gmtime(&rawTime);

  RTCTime rtcTime(
    timeinfo->tm_mday,
    Month(timeinfo->tm_mon),
    timeinfo->tm_year + 1900,
    timeinfo->tm_hour,
    timeinfo->tm_min,
    timeinfo->tm_sec,
    DayOfWeek((timeinfo->tm_wday == 0) ? 7 : timeinfo->tm_wday),
    SaveLight::SAVING_TIME_INACTIVE
  );

  return rtcTime;
}

void generateRandomPin(char* buffer) {
  for (int i = 0; i < 4; i++) {
    buffer[i] = '0' + random(0, 10);
  }
  buffer[4] = '\0'; 
}


