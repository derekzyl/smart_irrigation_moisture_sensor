#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>



Preferences pref;

// Pin Definitions
#define MOISTURE_SENSOR_PIN 34
#define RELAY_PIN 13
#define BUZZER_PIN 25

#define MENU_BUTTON_PIN 32
#define PLUS_BUTTON_PIN 33
#define MINUS_BUTTON_PIN 35


#define RW_MODE false
#define RO_MODE true 

const char* thresh = "threshold";

// WiFi Hotspot Configuration
const char* ssid = "SmartIrrigation";
const char* password = "IrrigationSystem2024!";

// Static IP for Access Point
IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// Web Server
WebServer server(80);

//define functions
void setupServer();
void handleMenu(int moisturePercentage);
void processIrrigation(int moisturePercentage);



// LCD Configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Global Variables
int moistureThreshold =pref.isKey(thresh)?pref.getInt(thresh): 40;  // Default threshold
int currentMoisture = 0;
bool menuActive = false;
bool systemMode = false;  // false = manual, true = WiFi

// Button State Tracking
int lastMenuButtonState = HIGH;
int lastPlusButtonState = HIGH;
int lastMinusButtonState = HIGH;


// Timing Variables
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
unsigned long lastMoistureCheckTime = 0;
const unsigned long moistureCheckInterval = 1000;
unsigned long lastThresholdAdjustTime = 0;
const unsigned long thresholdAdjustInterval = 200;

// HTML Page
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Smart Irrigation System</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            margin: 0;
            padding: 0;
            background-color: #f0f4f8;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
        }
        .container {
            background-color: white;
            border-radius: 15px;
            box-shadow: 0 10px 25px rgba(0,0,0,0.1);
            padding: 30px;
            width: 90%;
            max-width: 500px;
            text-align: center;
        }
        h1 {
            color: #2c3e50;
            margin-bottom: 20px;
        }
        .status-card {
            background-color: #ecf0f1;
            border-radius: 10px;
            padding: 15px;
            margin-bottom: 20px;
        }
        .status-label {
            font-weight: bold;
            color: #34495e;
        }
        .threshold-control {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 20px;
        }
        input[type="range"] {
            flex-grow: 1;
            margin: 0 15px;
        }
        .btn {
            background-color: #3498db;
            color: white;
            border: none;
            padding: 10px 20px;
            border-radius: 5px;
            cursor: pointer;
            transition: background-color 0.3s ease;
        }
        .btn:hover {
            background-color: #2980b9;
        }
        #modeToggle {
            background-color: #2ecc71;
        }
        #modeToggle:hover {
            background-color: #27ae60;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Smart Irrigation System</h1>
        <div class="status-card">
            <p><span class="status-label">Soil Moisture:</span> <span id="moisture">0%</span></p>
            <p><span class="status-label">System Status:</span> <span id="systemStatus">Idle</span></p>
        </div>
        <div class="threshold-control">
            <span>Moisture Threshold:</span>
            <span id="threshold">40</span>%
            <input type="range" id="thresholdSlider" min="0" max="100" value="40" onchange="updateThreshold(this.value)">
        </div>
        <div>
            <button class="btn" onclick="changeThreshold('decrease')">-</button>
            <button class="btn" onclick="changeThreshold('increase')">+</button>
            <button id="modeToggle" class="btn" onclick="toggleMode()">Toggle Mode</button>
        </div>
    </div>

    <script>
        async function updatePage() {
            const response = await fetch('/status');
            const data = await response.json();
            document.getElementById('moisture').textContent = data.moisture + '%';
            document.getElementById('threshold').textContent = data.threshold + '%';
            document.getElementById('thresholdSlider').value = data.threshold;
            document.getElementById('systemStatus').textContent = data.status;
        }

        async function changeThreshold(action) {
            await fetch('/threshold?action=' + action);
            updatePage();
        }

        async function updateThreshold(value) {
            await fetch('/threshold?value=' + value);
            updatePage();
        }

        async function toggleMode() {
            await fetch('/toggle-mode');
            updatePage();
        }

        setInterval(updatePage, 2000);
        updatePage();
    </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);

  // Initialize Pins
  pinMode(MOISTURE_SENSOR_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MENU_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PLUS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MINUS_BUTTON_PIN, INPUT_PULLUP);

  // init pref

  pref.begin("pref", false);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.print("Smart Irrigation");
  delay(2000);
  lcd.clear();

  // Setup WiFi Access Point
  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(local_IP, gateway, subnet);

  Serial.println("Access Point Started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  setupServer();
  Serial.println("HTTP server started");
}


void setupServer (){
  // Web Server Routes
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", htmlPage);
  });

  server.on("/status", HTTP_GET, []() {
    currentMoisture = analogRead(MOISTURE_SENSOR_PIN);
    int moisturePercentage =100- map(currentMoisture, 0, 4095, 0, 100);
    
    String status;
    if (systemMode) {
      // WiFi mode
      status = (moisturePercentage < moistureThreshold) ? "Irrigating (WiFi)" : "Idle (WiFi)";
      digitalWrite(RELAY_PIN, (moisturePercentage < moistureThreshold) ? HIGH : LOW);
    } else {
      // Manual mode
      status = (moisturePercentage < moistureThreshold) ? "Irrigating (Manual)" : "Idle (Manual)";
      // Manual mode control stays in the loop() function
    }

    // Critical moisture alert
    digitalWrite(BUZZER_PIN, (moisturePercentage < 20) ? HIGH : LOW);

    String json = "{";
    json += "\"moisture\":" + String(moisturePercentage) + ",";
    json += "\"threshold\":" + String(moistureThreshold) + ",";
    json += "\"status\":\"" + status + "\"";
    json += "}";

    server.send(200, "application/json", json);
  });

  server.on("/threshold", HTTP_GET, []() {
    if (server.hasArg("action")) {
      String action = server.arg("action");
      if (action == "increase") {
        moistureThreshold = min(moistureThreshold + 1, 100);

      } else if (action == "decrease") {
        moistureThreshold = max(moistureThreshold - 1, 0);
      }
    } else if (server.hasArg("value")) {
      moistureThreshold = server.arg("value").toInt();

    }
    pref.putInt(thresh, moistureThreshold);
    pref.end();
    server.send(200, "text/plain", "Threshold updated");
  });

  server.on("/toggle-mode", HTTP_GET, []() {
    systemMode = !systemMode;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(systemMode ? "WiFi Mode" : "Manual Mode");
    delay(1500);
    server.send(200, "text/plain", "Mode toggled");
  });

  // Start Server
  server.begin();

}

void loop() {
  // Handle web server requests
  server.handleClient();

  unsigned long currentTime = millis();

  if (currentTime - lastMoistureCheckTime >= moistureCheckInterval) {
    currentMoisture = analogRead(MOISTURE_SENSOR_PIN);
    int moisturePercentage =100- map(currentMoisture, 0, 4095, 0, 100);
    
    // Handle menu first
    handleMenu(moisturePercentage);

    // Only process moisture if not in menu mode
    if (!menuActive) {
      processIrrigation(moisturePercentage);
    }

    lastMoistureCheckTime = currentTime;
  }
  pref.end();
}
void processIrrigation(int moisturePercentage) {
  // Update LCD with current status
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Moisture: ");
  lcd.print(moisturePercentage);
  lcd.print("%");

  // Control relay based on moisture and system mode
  if (!systemMode) {
    lcd.setCursor(0, 0);
    lcd.print("Status:");
    if (moisturePercentage < moistureThreshold) {
      digitalWrite(RELAY_PIN, HIGH); // Turn on 
      
      lcd.setCursor(0, 1);
      lcd.print("Irrigating");
    } else {
      digitalWrite(RELAY_PIN, LOW); // Turn off relay
      lcd.setCursor(0, 1);
      lcd.print("Idle");
    }
  }

  // Critical moisture alert
  if (moisturePercentage < 20) {
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void handleMenu(int moisturePercentage) {
  int menuButtonState = digitalRead(MENU_BUTTON_PIN);
  int plusButtonState = digitalRead(PLUS_BUTTON_PIN);
  int minusButtonState = digitalRead(MINUS_BUTTON_PIN);
  unsigned long currentTime = millis();

  // Check for menu button press with debounce
  if (menuButtonState == LOW && lastMenuButtonState == HIGH && 
      (currentTime - lastDebounceTime) > debounceDelay) {
    menuActive = !menuActive;
    lcd.clear();
    lastDebounceTime = currentTime;
  }
  lastMenuButtonState = menuButtonState;

  // Menu active - allow threshold adjustment
  if (menuActive) {
    lcd.setCursor(0, 0);
    lcd.print("Set Threshold:");
    lcd.setCursor(0, 1);
    lcd.print(moistureThreshold);
    lcd.print("%");

    // Non-blocking threshold adjustment
    if (plusButtonState == LOW && 
        (currentTime - lastThresholdAdjustTime) >= thresholdAdjustInterval) {
      moistureThreshold = min(moistureThreshold + 1, 100);
      lastThresholdAdjustTime = currentTime;
    }

    if (minusButtonState == LOW && 
        (currentTime - lastThresholdAdjustTime) >= thresholdAdjustInterval) {
      moistureThreshold = max(moistureThreshold - 1, 0);
      lastThresholdAdjustTime = currentTime;
    }
    pref.putInt(thresh, moistureThreshold);

  }

  // Update button states
  lastPlusButtonState = plusButtonState;
  lastMinusButtonState = minusButtonState;
}