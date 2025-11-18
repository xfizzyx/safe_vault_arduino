#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <SHA256.h>

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

//Animation
int dotAnim = 0;

//SETUP
void setup() {
  Serial.begin(9600);
  SPI.begin();
  mfrc522.PCD_Init();
  
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
    check_card();
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
        delay(1000);
        show_page_cards(displayStartMenuCards);
        step = 3;
      }
      else if (key == '3' && displayStartMenu==2) { // 2FA CONFIG
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Pair BT...");
        pinLength = 0;
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
      if (key) {
        // BLUETOOTH SCRIPT TO PAIR AND CONFIG...
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
void check_card() {
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
      break;
    }
  }

  // Display result
  lcd.clear();
  lcd.setCursor(0, 0);
  if (found) {
    lcd.print("OK");
    Serial.println("Card authorized");

    // ALL OK - LOGGED
    // START CHECK 2FA
    
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
