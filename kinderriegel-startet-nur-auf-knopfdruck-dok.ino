#include <ESP8266WiFi.h>          // Bibliothek für WiFi-Funktionen
#include <ESP8266WebServer.h>     // Bibliothek für den Webserver
#include <WiFiManager.h>          // Bibliothek für den WiFi Manager (Access Point)
#include <Servo.h>                // Bibliothek für die Servo-Steuerung
#include <PubSubClient.h>         // Bibliothek für MQTT-Kommunikation

// Webserver auf Port 80 initialisieren
ESP8266WebServer server(80);

// WiFi- und MQTT-Client initialisieren
WiFiClient espClient;
PubSubClient client(espClient);

// Servo-Objekt initialisieren
Servo myServo;
const int servoPin = 2;           // Pin, an dem der Servo angeschlossen ist
const int buttonPin = 4;          // Pin, an dem der Taster angeschlossen ist (GPIO4, D2 auf den meisten ESP8266-Boards)

// Variablen für den Automodus
bool autoMode = false;            // Automodus aktiviert/deaktiviert
int autoInterval = 300000;        // Intervall für den Automodus (Standard: 5 Minuten in Millisekunden)
unsigned long lastTrigger = 0;    // Zeitpunkt der letzten Servo-Aktivierung

// MQTT-Einstellungen
const char* mqtt_server = "0.0.0.0"; // MQTT-Server-Adresse (Standard: deaktiviert)
const char* mqtt_user = "your_mqtt_user"; // MQTT-Benutzername
const char* mqtt_pass = "your_mqtt_password"; // MQTT-Passwort
const char* mqtt_topic = "esp/servo"; // MQTT-Topic für Servo-Steuerung
const int mqtt_port = 1883;       // MQTT-Port (Standard: 1883)

// Variablen für Verbindungsversuche und Zustände
int reconnectAttempts = 0;        // Zähler für MQTT-Verbindungsversuche
bool inAccessMode = false;        // Flag, um zu prüfen, ob der ESP im Access Mode ist
unsigned long buttonPressStart = 0; // Zeitpunkt, zu dem der Taster gedrückt wurde
bool buttonActive = false;        // Flag, um zu prüfen, ob der Taster aktiv ist
bool wifiConnected = false;       // Flag, um zu prüfen, ob WiFi verbunden ist

// Funktion zum Bewegen des Servos
void triggerServo() {
    myServo.write(180); // Servo auf 180 Grad bewegen
    delay(500);         // 500 ms warten
    myServo.write(0);   // Servo auf 0 Grad bewegen
    delay(500);         // 500 ms warten
    myServo.write(180); // Servo auf 180 Grad bewegen
    delay(500);         // 500 ms warten
    myServo.write(0);   // Servo auf 0 Grad bewegen
    delay(500);         // 500 ms warten
}

// MQTT-Callback-Funktion (wird aufgerufen, wenn eine Nachricht empfangen wird)
void callback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i]; // Nachricht aus dem Payload extrahieren
    }
    if (message == "trigger") { // Wenn die Nachricht "trigger" ist, Servo bewegen
        triggerServo();
    }
}

// Funktion zum Verbinden mit dem MQTT-Server
void reconnect() {
    if (mqtt_server != "0.0.0.0" && !client.connected() && reconnectAttempts < 5) {
        while (!client.connected() && reconnectAttempts < 5) {
            Serial.print("Verbindung zum MQTT-Server... Versuch " + String(reconnectAttempts + 1) + "/5 ");
            if (client.connect("ESP8266Client", mqtt_user, mqtt_pass)) { // Verbindungsversuch
                Serial.println("verbunden!");
                client.subscribe(mqtt_topic); // Topic abonnieren
                reconnectAttempts = 0; // Zähler zurücksetzen
            } else {
                reconnectAttempts++;
                Serial.print("Fehler, rc=");
                Serial.print(client.state());
                Serial.println(" Versuch in 5 Sekunden...");
                delay(5000); // 5 Sekunden warten vor dem nächsten Versuch
            }
        }
        if (reconnectAttempts >= 5) {
            Serial.println("Maximale Verbindungsversuche erreicht. Keine weitere Verbindung.");
        }
    }
}

// Funktion zum Starten des Webservers
void startWebServer() {
    // Route für die Homepage
    server.on("/", HTTP_GET, []() {
        unsigned long timeLeft = (autoMode && autoInterval > 0) ? (autoInterval - (millis() - lastTrigger)) / 1000 : 0;
        server.send(200, "text/html", "<html><body>"
                                      "<h1>Kinderriegel by Det Eu</h1>"
                                      "<form action='/toggle' method='GET'>"
                                      "<button type='submit'>Servo bewegen</button>"
                                      "</form>"
                                      "<form action='/auto' method='GET'>"
                                      "<button type='submit'>Auto-Modus umschalten</button>"
                                      "</form>"
                                      "<form action='/setinterval' method='GET'>"
                                      "<input type='number' name='interval' placeholder='Intervall in Minuten'>"
                                      "<button type='submit'>Setze Intervall</button>"
                                      "</form>"
                                      "<p>Automatik: " + String(autoMode ? "AN" : "AUS") + "</p>"
                                      "<p>Intervall: " + String(autoInterval / 60000) + " Minuten</p>"
                                      "<p>Nächste Aktivierung in: <span id='countdown'>" + String(timeLeft) + "</span> Sekunden</p>"
                                      "<script>"
                                      "function updateCountdown() {"
                                      "  function tick() {"
                                      "    let countElement = document.getElementById('countdown');"
                                      "    if (countElement) {"
                                      "      let count = parseInt(countElement.innerText);"
                                      "      if (count > 0) {"
                                      "        countElement.innerText = count - 1;"
                                      "      }"
                                      "    }"
                                      "    setTimeout(tick, 1000);"
                                      "  }"
                                      "  tick();"
                                      "}"
                                      "window.onload = updateCountdown;"
                                      "</script>"
                                      "</body></html>");
    });

    // Route zum Bewegen des Servos
    server.on("/toggle", HTTP_GET, []() {
        triggerServo();
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "Servo bewegt");
    });

    // Route zum Umschalten des Automodus
    server.on("/auto", HTTP_GET, []() {
        autoMode = !autoMode;
        lastTrigger = millis();
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "Auto-Modus umgeschaltet");
    });

    // Route zum Setzen des Intervalls
    server.on("/setinterval", HTTP_GET, []() {
        if (server.hasArg("interval")) {
            autoInterval = server.arg("interval").toInt() * 60000; // Minuten in Millisekunden umwandeln
            lastTrigger = millis();
        }
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "Intervall gesetzt");
    });

    // Webserver starten
    server.begin();
    Serial.println("Webserver gestartet");
}

// Setup-Funktion (wird einmal beim Start ausgeführt)
void setup() {
    Serial.begin(115200); // Serielle Kommunikation starten
    myServo.attach(servoPin); // Servo an den definierten Pin anhängen
    myServo.write(0); // Servo auf 0 Grad setzen

    // Taster-Pin als Eingang mit Pull-up-Widerstand konfigurieren
    pinMode(buttonPin, INPUT_PULLUP);

    // Überprüfen, ob der Taster beim Start gedrückt wird
    if (digitalRead(buttonPin) == LOW) {
        Serial.println("Taster gedrückt beim Start! Servo bewegt.");
        triggerServo();
    }
}

// Loop-Funktion (wird kontinuierlich ausgeführt)
void loop() {
    // Tasterzustand überprüfen
    if (digitalRead(buttonPin) == LOW) { // Taster gedrückt (LOW wegen Pull-up)
        if (!buttonActive) {
            buttonActive = true;
            buttonPressStart = millis(); // Startzeit speichern
        } else if (millis() - buttonPressStart >= 15000) { // 15 Sekunden gedrückt
            Serial.println("Taster 15 Sekunden gedrückt! Starte Access Mode...");
            WiFiManager wifiManager;
            wifiManager.autoConnect("Kinderriegel"); // Access Point erstellen
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("ESP verbunden mit WiFi");
                Serial.print("IP-Adresse: ");
                Serial.println(WiFi.localIP());
                startWebServer(); // Webserver starten
                inAccessMode = true; // Access Mode aktivieren
                wifiConnected = true; // WiFi-Verbindungs-Flag setzen

                // MQTT-Client initialisieren, wenn WiFi verbunden ist
                if (mqtt_server != "0.0.0.0") {
                    client.setServer(mqtt_server, mqtt_port);
                    client.setCallback(callback);
                }
            } else {
                Serial.println("ESP im Access Mode");
            }
            buttonActive = false; // Taster-Flag zurücksetzen
        }
    } else {
        if (buttonActive) {
            // Taster wurde losgelassen
            if (millis() - buttonPressStart < 15000) {
                // Kurzer Tastendruck: Servo bewegen
                Serial.println("Taster gedrückt! Servo bewegt.");
                triggerServo();
            }
            buttonActive = false; // Taster-Flag zurücksetzen
        }
    }

    // MQTT-Verbindung verwalten (nur wenn WiFi verbunden ist)
    if (wifiConnected && mqtt_server != "0.0.0.0" && !client.connected()) {
        reconnect();
    }
    client.loop();

    // Automodus
    if (autoMode && millis() - lastTrigger >= autoInterval) {
        lastTrigger = millis();
        triggerServo();
    }

    // Webserver-Anfragen bearbeiten
    if (inAccessMode) {
        server.handleClient();
    }
}