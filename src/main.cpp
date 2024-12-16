#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal_I2C.h>

// Pin Definitions
#define MOISTURE_SENSOR_PIN 2
#define RELAY_PIN 13
#define BUZZER_PIN 34

#define MENU_BUTTON_PIN 32
#define PLUS_BUTTON_PIN 33
#define MINUS_BUTTON_PIN 35

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
void handleMenu();
void setupServer();

// LCD Configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Global Variables
int moistureThreshold = 40;  // Default threshold
int currentMoisture = 0;
bool menuActive = false;
bool systemMode = false;  // false = manual, true = WiFi

// Button State Tracking
int lastMenuButtonState = HIGH;
int lastPlusButtonState = HIGH;
int lastMinusButtonState = HIGH;

// Debounce Timing
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

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
    int moisturePercentage = map(currentMoisture, 0, 4095, 0, 100);
    
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

  // Read moisture
  currentMoisture = analogRead(MOISTURE_SENSOR_PIN);
  int moisturePercentage = map(currentMoisture, 0, 4095, 0, 100);

  // Menu and manual mode button handling
  handleMenu();

  // Only control relay in manual mode if not in WiFi mode
  if (!systemMode) {
    // Display current moisture and system status
    lcd.setCursor(0, 0);
    lcd.print("Moisture: ");
    lcd.print(moisturePercentage);
    lcd.print("%   ");

    if (moisturePercentage < moistureThreshold) {
      digitalWrite(RELAY_PIN, HIGH); // Turn on relay
      lcd.setCursor(0, 1);
      lcd.print("Status: Irrigate ");
    } else {
      digitalWrite(RELAY_PIN, LOW); // Turn off relay
      lcd.setCursor(0, 1);
      lcd.print("Status: Idle     ");
    }

    // Critical moisture alert
    digitalWrite(BUZZER_PIN, (moisturePercentage < 20) ? HIGH : LOW);
  }

  delay(500);
}

void handleMenu() {
  int menuButtonState = digitalRead(MENU_BUTTON_PIN);
  int plusButtonState = digitalRead(PLUS_BUTTON_PIN);
  int minusButtonState = digitalRead(MINUS_BUTTON_PIN);

  // Check for menu button press
  if (menuButtonState == LOW && lastMenuButtonState == HIGH && (millis() - lastDebounceTime) > debounceDelay) {
    menuActive = !menuActive; // Toggle menu
    lcd.clear();
    lastDebounceTime = millis();
  }
  lastMenuButtonState = menuButtonState;

  if (menuActive) {
    lcd.setCursor(0, 0);
    lcd.print("Set Threshold:");
    lcd.setCursor(0, 1);
    lcd.print(moistureThreshold);
    lcd.print("%   ");

    // Adjust threshold with buttons
    if (plusButtonState == LOW && lastPlusButtonState == HIGH && (millis() - lastDebounceTime) > debounceDelay) {
      moistureThreshold = min(moistureThreshold + 1, 100);
      lastDebounceTime = millis();
      delay(200); // Add a small delay to prevent rapid changes
    }
    if (minusButtonState == LOW && lastMinusButtonState == HIGH && (millis() - lastDebounceTime) > debounceDelay) {
      moistureThreshold = max(moistureThreshold - 1, 0);
      lastDebounceTime = millis();
      delay(200); // Add a small delay to prevent rapid changes
    }
    lastPlusButtonState = plusButtonState;
    lastMinusButtonState = minusButtonState;
  }
}