// Armina Grow - Prototipo IoT para Cultivo Indoor con ESP32
// Versión 0.1.0

// =============================================================================
// INCLUSIÓN DE LIBRERÍAS
// =============================================================================
#include <WiFi.h>
#include <WebServer.h> // Para el servidor web HTTP
#include <DNSServer.h> // Para el portal cautivo (usado por WiFiManager)
#include <WiFiManager.h> // Para la gestión de conexión WiFi y portal cautivo
#include <LiquidCrystal_I2C.h> // Para el display LCD I2C
#include <DHT.h> // Para el sensor DHT11/22
#include <Preferences.h> // Para guardar configuraciones en memoria no volátil
#include <LittleFS.h> // Para el sistema de archivos (logs, página web)
#include <NTPClient.h> // Para obtener la hora de internet
#include <WiFiUdp.h> // Requerido por NTPClient
// Para el Encoder Rotativo, usaremos una implementación simple sin librería externa por ahora
// o puedes integrar una como ESP32Encoder.h o RotaryEncoder.h

// =============================================================================
// DEFINICIONES Y CONSTANTES GLOBALES
// =============================================================================
#define FIRMWARE_VERSION "0.1.0"

// --- Pines del Hardware ---
// Display LCD I2C
const int LCD_SDA_PIN = 21; // Ya definido por la librería, pero bueno tenerlo
const int LCD_SCL_PIN = 22; // Ya definido por la librería, pero bueno tenerlo
const int LCD_ADDRESS = 0x27; // Dirección I2C común para LCDs 1602. Verifica la tuya.
const int LCD_COLS = 16;
const int LCD_ROWS = 2;

// Módulo de 4 Relés
const int RELAY1_PIN = 18; // Luces
const int RELAY2_PIN = 19; // Ventilación
const int RELAY3_PIN = 23; // Extracción
const int RELAY4_PIN = 25; // Riego
const int NUM_RELAYS = 4;
const int relayPins[NUM_RELAYS] = {RELAY1_PIN, RELAY2_PIN, RELAY3_PIN, RELAY4_PIN};
const char* relayNames[NUM_RELAYS] = {"Luces", "Ventilacion", "Extraccion", "Riego"};
bool relayStates[NUM_RELAYS] = {false, false, false, false}; // Estado actual de los relés

// Sensor DHT11
const int DHT_PIN = 26;
#define DHT_TYPE DHT11 // o DHT22 si usas ese
float currentTemperature = -99.0;
float currentHumidity = -99.0;

// Encoder Rotativo
const int ENCODER_CLK_PIN = 13;
const int ENCODER_DT_PIN = 14;
const int ENCODER_SW_PIN = 27;
volatile int encoderPos = 0;
volatile int lastEncoderPos = 0;
volatile bool encoderSWPressed = false;
unsigned long lastEncoderActivityTime = 0;
const unsigned long MENU_TIMEOUT_MS = 10000; // 10 segundos

// LED Integrado
const int LED_BUILTIN_PIN = 2; // Generalmente el GPIO2 en ESP32 DevKits

// --- Nombres de Archivos y Directorios ---
const char* PREFERENCES_NAMESPACE = "armina-grow";
const char* PREFERENCES_FILE_DEPRECATED = "/armina-grow-pref.json"; // Ya no se usa directamente, Preferences lo maneja
const char* LOG_FILE = "/armina-grow-log.csv";
const char* WEB_MONITOR_HTML_FILE = "/armina-grow-monitor.html"; // Nombre de archivo actualizado

// --- Configuración WiFi y Servidor Web ---
const char* AP_SSID_PREFIX = "ArminaGrow-Setup";
WebServer server(80);
DNSServer dnsServer;
WiFiManager wifiManager;

// --- Configuración NTP ---
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); // Offset UTC 0, update cada 60s

// =============================================================================
// ESTRUCTURAS DE DATOS Y ENUMS
// =============================================================================

// --- Estados de la Interfaz de Usuario (UI) ---
enum UIState {
    UI_HOME,
    UI_MENU_MAIN,
    UI_MENU_SUB, // Para submenús como el de Sistema->WiFi, etc.
    UI_EDIT_VALUE,
    UI_CONFIRM_ACTION,
    UI_TEMP_MESSAGE
};
UIState currentUIState = UI_HOME;
UIState previousUIState = UI_HOME; // Para volver después de un mensaje temporal

// --- Definición del Menú (Ejemplo básico, expandir según necesidad) ---
// Esta es una simplificación. Una estructura más robusta usaría structs/clases anidadas.
const int MAIN_MENU_ITEMS = 7;
const char* mainMenu[MAIN_MENU_ITEMS] = {
    "1. Temp/Hum",
    "2. Luces",
    "3. Ventilacion",
    "4. Extraccion",
    "5. Riego",
    "6. Sistema",
    "7. Salir"
};
int currentMenuItem = 0;
int currentSubMenuItem = 0; // Para submenús
int editingParamIndex = 0; // Para saber qué parámetro de un ítem se está editando

// --- Estructura para Preferencias (Valores por defecto y claves) ---
struct Config {
    // Temp y Humedad
    int thFrecuenciaMuestreo; // 1-60 min
    bool thModoTest;

    // Luces
    int lucesHoraON; // 0-23
    int lucesHoraOFF; // 0-23
    bool lucesModoTest;

    // Ventilación
    int ventHoraON; // 0-23
    int ventHoraOFF; // 0-23
    bool ventModoTest;

    // Extracción
    int extrFrecuenciaHoras; // 0-24 (0=off)
    int extrDuracionMinutos; // 1-60
    bool extrModoTest;

    // Riego
    int riegoFrecuenciaDias; // 0-30 (0=off)
    int riegoDuracionMinutos; // 1-30
    int riegoHoraDisparo; // 0-23
    bool riegoModoTest;

    // Registro
    int registroFrecuenciaHoras; // 0-24 (0=off)
    int registroTamMax; // 1000-9999
    bool registroModoTest;
};
Config currentConfig;

// Claves para Preferences
const char* KEY_TH_FREQ = "thFreq";
const char* KEY_TH_TEST = "thTest";
const char* KEY_LUCES_ON = "luzOn";
const char* KEY_LUCES_OFF = "luzOff";
const char* KEY_LUCES_TEST = "luzTest";
const char* KEY_VENT_ON = "ventOn";
const char* KEY_VENT_OFF = "ventOff";
const char* KEY_VENT_TEST = "ventTest";
const char* KEY_EXTR_FREQ = "extrFreq";
const char* KEY_EXTR_DUR = "extrDur";
const char* KEY_EXTR_TEST = "extrTest";
const char* KEY_RIEGO_FREQ = "riegoFreq";
const char* KEY_RIEGO_DUR = "riegoDur";
const char* KEY_RIEGO_HORA = "riegoHora";
const char* KEY_RIEGO_TEST = "riegoTest";
const char* KEY_REG_FREQ = "regFreq";
const char* KEY_REG_MAX = "regMax";
const char* KEY_REG_TEST = "regTest";


// =============================================================================
// OBJETOS GLOBALES
// =============================================================================
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);
DHT dht(DHT_PIN, DHT_TYPE);
Preferences preferences;

// =============================================================================
// VARIABLES GLOBALES DE ESTADO Y TEMPORIZADORES
// =============================================================================
unsigned long lastDHTReadTime = 0;
unsigned long lastLogTime = 0;
unsigned long lastLEDToggleTime = 0;
unsigned long tempMessageStartTime = 0;
unsigned long tempMessageDuration = 0;

bool wifiConnected = false;
String currentSSID = "";
String currentIP = "";

// Para el parpadeo del LED integrado
int ledBlinkState = LOW;
int ledBlinkCount = 0;
const int NORMAL_MODE_BLINK_COUNT = 2;
unsigned long ledBlinkIntervalNormal = 150; // ms entre parpadeos rápidos
unsigned long ledBlinkPauseNormal = 3000 - (NORMAL_MODE_BLINK_COUNT * 2 * ledBlinkIntervalNormal); // Pausa para completar 3s
unsigned long ledBlinkIntervalConfig = 1000; // 1s ON, 1s OFF

// Para la lógica de control de relés basada en tiempo
unsigned long lastLucesCheck = 0;
unsigned long lastVentCheck = 0;
unsigned long lastExtraccionActivationTime = 0; // Para frecuencia de extracción
unsigned long lastRiegoActivationTime = 0; // Para frecuencia de riego
bool extraccionActive = false; // Para saber si la extracción está en su ciclo de duración
unsigned long extraccionStartTime = 0;
bool riegoActive = false; // Para saber si el riego está en su ciclo de duración
unsigned long riegoStartTime = 0;


// =============================================================================
// DECLARACIONES DE FUNCIONES (PROTOTIPOS)
// =============================================================================
// --- Setup y Loop ---
void setup();
void loop();

// --- Sensores y Actuadores ---
void readDHTSensor();
void controlRelay(int relayIndex, bool state, bool manualOverride = false);
void applyScheduledControls();
void applyLucesControl();
void applyVentilacionControl();
void applyExtraccionControl();
void applyRiegoControl();
void checkTestModes(); // Para ejecutar lógica de modo test de relés

// --- Interfaz de Usuario (LCD y Encoder) ---
void IRAM_ATTR handleEncoderInterrupt();
void IRAM_ATTR handleEncoderButtonInterrupt();
void processEncoderChanges();
void updateDisplay();
void displayHomeScreen();
void displayMenuScreen();
void displayEditValueScreen(); // Simplificado, necesitará mucha lógica aquí
void displayConfirmScreen(); // Simplificado
void displayTemporaryMessage(String line1, String line2, int durationMs);
void resetMenuTimeout();
void checkMenuTimeout();

// --- Gestión WiFi y NTP ---
void setupWiFi();
bool connectToWiFi();
void startAPMode();
void updateNTP();
String getFormattedTime(bool withSeconds = false);
String getFormattedDateTime();
String getLocalIP();

// --- Gestión de Preferencias ---
void loadDefaultPreferences();
void loadPreferences();
void savePreferences();
// void saveSinglePreference(String key, ...); // Implementar según necesidad para cada tipo
void resetWiFiCredentialsAndRestart();

// --- Gestión de Archivos (Log y Web) ---
void initLittleFS();
void logData();
int getLogEntryCount();
void rotateLogIfNeeded();
void serveFile(String path, String contentType);
void uploadFileToLittleFS(String localPath, String fsPath); // Utilidad para desarrollo

// --- Servidor Web ---
void setupWebServer();
void handleRoot();
void handleData();
void handleControl();
void handleRiegoManual();
void handleDownloadLog();
void handleLastLogEntries();
void handleNotFound();
void handleWiFiSaveAndRestart(); // Para el portal cautivo si se usa config manual

// --- Utilidades ---
void blinkLED();
void printSerialDebug(String message);

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    printSerialDebug("Armina Grow Iniciando...");
    printSerialDebug("Version Firmware: " + String(FIRMWARE_VERSION));

    pinMode(LED_BUILTIN_PIN, OUTPUT);
    digitalWrite(LED_BUILTIN_PIN, LOW);

    // Inicializar Encoder
    pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);
    pinMode(ENCODER_DT_PIN, INPUT_PULLUP);
    pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ENCODER_CLK_PIN), handleEncoderInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_DT_PIN), handleEncoderInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_SW_PIN), handleEncoderButtonInterrupt, FALLING); // Pulsador activo bajo

    // Inicializar LCD
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Armina Grow");
    lcd.setCursor(0, 1);
    lcd.print("Iniciando...");
    delay(2000);

    // Inicializar LittleFS
    initLittleFS();
    // Aquí podrías tener una función para subir el armina-grow-monitor.html si no existe
    // uploadFileToLittleFS("path/to/your/local/armina-grow-monitor.html", WEB_MONITOR_HTML_FILE);

    // Cargar Preferencias
    preferences.begin(PREFERENCES_NAMESPACE, false); // false = R/W mode
    loadPreferences(); // Carga desde NVS o usa defaults si no existen

    // Inicializar DHT
    dht.begin();

    // Configurar pines de Relés
    for (int i = 0; i < NUM_RELAYS; i++) {
        pinMode(relayPins[i], OUTPUT);
        digitalWrite(relayPins[i], LOW); // Asegurar que todos los relés estén apagados al inicio
        relayStates[i] = false;
    }

    // Configurar WiFi
    setupWiFi(); // Esto manejará si entra en modo AP o se conecta a una red existente

    if (wifiConnected) {
        printSerialDebug("Conectado a WiFi: " + WiFi.SSID());
        printSerialDebug("IP: " + WiFi.localIP().toString());
        currentSSID = WiFi.SSID();
        currentIP = WiFi.localIP().toString();
        
        timeClient.begin();
        updateNTP(); // Primera sincronización

        setupWebServer();
        server.begin();
        printSerialDebug("Servidor Web HTTP iniciado.");
        displayHomeScreen();
    } else {
        printSerialDebug("Modo Configuracion (AP). SSID: " + String(AP_SSID_PREFIX) + WiFi.macAddress().substring(12));
        // El LCD ya debería mostrar "Conectar a:" y el SSID del AP desde setupWiFi()
    }
    
    lastEncoderActivityTime = millis(); // Iniciar timeout del menú
    currentUIState = UI_HOME;
    printSerialDebug("Setup completado.");
}

// =============================================================================
// LOOP PRINCIPAL
// =============================================================================
void loop() {
    if (wifiConnected) {
        server.handleClient(); // Atender peticiones HTTP
        updateNTP(); // Actualizar hora periódicamente
    } else {
        // Si no estamos conectados y WiFiManager está en modo AP, él maneja el loop.
        // Si usamos lógica manual de AP, aquí iría dnsServer.processNextRequest();
        // y el manejo del servidor web de configuración.
        // Con WiFiManager, si no hay conexión, intentará reconectar o entrará en portal.
        // Aquí podríamos verificar si WiFiManager ha terminado y necesitamos reiniciar o algo.
    }

    processEncoderChanges(); // Leer y procesar entradas del encoder
    updateDisplay(); // Actualizar el LCD según el estado de la UI
    checkMenuTimeout(); // Volver a HOME si hay inactividad en el menú

    // Tareas periódicas
    unsigned long currentTime = millis();

    // Leer sensores
    unsigned long thInterval = (currentConfig.thModoTest ? 5 : currentConfig.thFrecuenciaMuestreo * 60) * 1000UL;
    if (thInterval > 0 && (currentTime - lastDHTReadTime >= thInterval || lastDHTReadTime == 0)) {
        readDHTSensor();
        lastDHTReadTime = currentTime;
    }

    // Controlar relés según configuración (si no están en modo test manual desde web)
    if (wifiConnected) { // Solo si hay hora y lógica de control activa
         applyScheduledControls();
    }
    checkTestModes(); // Lógica de parpadeo para modos test activados desde el menú

    // Registrar datos
    unsigned long logInterval = (currentConfig.registroModoTest ? 10 : currentConfig.registroFrecuenciaHoras * 3600) * 1000UL;
    if (currentConfig.registroFrecuenciaHoras > 0 && wifiConnected && (currentTime - lastLogTime >= logInterval || lastLogTime == 0)) {
        if (timeClient.isTimeSet()) { // Solo registrar si tenemos hora válida
            logData();
            lastLogTime = currentTime;
        }
    }
    
    // Parpadeo del LED integrado
    blinkLED();

    // Verificar estado de conexión WiFi (si no estamos en modo AP por WiFiManager)
    if (!wifiManager.getConfigPortalActive()) { // No verificar si el portal está activo
        if (WiFi.status() != WL_CONNECTED && wifiConnected) {
            printSerialDebug("Conexion WiFi perdida.");
            wifiConnected = false;
            currentSSID = "";
            currentIP = "";
            // No reiniciar el servidor web aquí, podría estar en proceso de reconexión.
            // La pantalla HOME se actualizará a "Offline"
            displayHomeScreen(); // Forzar actualización para mostrar Offline
        } else if (WiFi.status() == WL_CONNECTED && !wifiConnected) {
            // Se reconectó (o conectó después de un intento fallido inicial no manejado por WiFiManager)
            printSerialDebug("Conexion WiFi restablecida/establecida.");
            wifiConnected = true;
            currentSSID = WiFi.SSID();
            currentIP = WiFi.localIP().toString();
            timeClient.begin(); // Reiniciar NTP si es necesario
            updateNTP();
            // Si el servidor no estaba corriendo (ej. después de un fallo total), iniciarlo.
            // server.begin(); // Esto podría ser problemático si ya estaba iniciado.
            // Mejor asumir que WiFiManager o la lógica de setupWiFi lo maneja.
            displayHomeScreen(); // Forzar actualización para mostrar Online
        }
    }
}

// =============================================================================
// IMPLEMENTACIÓN DE FUNCIONES
// =============================================================================

// --- Sensores y Actuadores ---
void readDHTSensor() {
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {
        printSerialDebug("Error al leer DHT11/22");
        // Mantener valores anteriores o marcarlos como inválidos
        // currentTemperature = -99.0; // Opcional: marcar como inválido
        // currentHumidity = -99.0;
    } else {
        currentTemperature = t;
        currentHumidity = h;
        printSerialDebug("Temp: " + String(t) + "C, Hum: " + String(h) + "%");
    }
    // Si estamos en la pantalla HOME, actualizarla inmediatamente
    if (currentUIState == UI_HOME) {
        displayHomeScreen();
    }
}

void controlRelay(int relayIndex, bool state, bool manualOverride) {
    if (relayIndex < 0 || relayIndex >= NUM_RELAYS) return;

    // Solo cambiar y notificar si el estado es diferente o es un override manual que queremos forzar
    if (relayStates[relayIndex] != state || manualOverride) {
        relayStates[relayIndex] = state;
        digitalWrite(relayPins[relayIndex], state ? HIGH : LOW); // Asumir relés activos en ALTO
        printSerialDebug(String(relayNames[relayIndex]) + (state ? " ENCENDIDO" : " APAGADO"));
        
        // Mostrar mensaje temporal en LCD
        // Solo si no estamos ya mostrando otro mensaje temporal o en medio de una edición
        if (currentUIState != UI_TEMP_MESSAGE && currentUIState != UI_EDIT_VALUE && currentUIState != UI_CONFIRM_ACTION) {
            displayTemporaryMessage(relayNames[relayIndex], (state ? "Encendido" : "Apagado"), 2000);
        }
    }
}

void applyScheduledControls() {
    if (!timeClient.isTimeSet()) {
        // printSerialDebug("Hora no sincronizada, controles programados en espera.");
        return; // No aplicar controles si no hay hora
    }
    applyLucesControl();
    applyVentilacionControl();
    applyExtraccionControl();
    applyRiegoControl();
}

void applyLucesControl() {
    if (currentConfig.lucesModoTest) return; // Modo test manual tiene prioridad

    int currentHour = timeClient.getHours();
    // Lógica simple: si HoraON <= HoraOFF, es un ciclo dentro del mismo día.
    // Si HoraON > HoraOFF, es un ciclo que cruza la medianoche.
    bool shouldBeOn = false;
    if (currentConfig.lucesHoraON <= currentConfig.lucesHoraOFF) { // Ciclo diurno
        if (currentHour >= currentConfig.lucesHoraON && currentHour < currentConfig.lucesHoraOFF) {
            shouldBeOn = true;
        }
    } else { // Ciclo nocturno (cruza medianoche)
        if (currentHour >= currentConfig.lucesHoraON || currentHour < currentConfig.lucesHoraOFF) {
            shouldBeOn = true;
        }
    }
    controlRelay(0, shouldBeOn); // Relay 0 = Luces
}

void applyVentilacionControl() {
    if (currentConfig.ventModoTest) return;

    int currentHour = timeClient.getHours();
    bool shouldBeOn = false;
    if (currentConfig.ventHoraON <= currentConfig.ventHoraOFF) {
        if (currentHour >= currentConfig.ventHoraON && currentHour < currentConfig.ventHoraOFF) {
            shouldBeOn = true;
        }
    } else {
        if (currentHour >= currentConfig.ventHoraON || currentHour < currentConfig.ventHoraOFF) {
            shouldBeOn = true;
        }
    }
    controlRelay(1, shouldBeOn); // Relay 1 = Ventilación
}

void applyExtraccionControl() {
    if (currentConfig.extrModoTest) return;
    if (currentConfig.extrFrecuenciaHoras == 0) { // 0 = apagado
        controlRelay(2, false);
        extraccionActive = false;
        return;
    }

    unsigned long currentTime = millis();
    if (extraccionActive) {
        // Verificar si el tiempo de duración ha pasado
        if (currentTime - extraccionStartTime >= (unsigned long)currentConfig.extrDuracionMinutos * 60 * 1000UL) {
            controlRelay(2, false); // Apagar extracción
            extraccionActive = false;
            printSerialDebug("Extraccion: Ciclo completado.");
        }
    } else {
        // Verificar si es hora de activar según frecuencia
        if (currentTime - lastExtraccionActivationTime >= (unsigned long)currentConfig.extrFrecuenciaHoras * 3600 * 1000UL) {
            controlRelay(2, true); // Encender extracción
            extraccionActive = true;
            extraccionStartTime = currentTime;
            lastExtraccionActivationTime = currentTime;
            printSerialDebug("Extraccion: Iniciando ciclo.");
        }
    }
}

void applyRiegoControl() {
    if (currentConfig.riegoModoTest) return;
    if (currentConfig.riegoFrecuenciaDias == 0) { // 0 = apagado
        controlRelay(3, false);
        riegoActive = false;
        return;
    }

    unsigned long currentTime = millis();
    int currentHour = timeClient.getHours();
    // Para la frecuencia en días, necesitamos una forma de saber si ya regamos "hoy" si la frecuencia es 1
    // o si ya pasaron N días desde el último riego.
    // Esto es más complejo y requiere guardar la fecha/hora del último riego en Preferences.
    // Simplificación por ahora: se activa a la hora de disparo si ha pasado el intervalo de días.
    // Una mejor implementación usaría el número de día del año o similar.

    if (riegoActive) {
        if (currentTime - riegoStartTime >= (unsigned long)currentConfig.riegoDuracionMinutos * 60 * 1000UL) {
            controlRelay(3, false);
            riegoActive = false;
            printSerialDebug("Riego: Ciclo completado.");
        }
    } else {
        // Chequear si es la hora de disparo y si ha pasado el tiempo de frecuencia
        // (currentTime - lastRiegoActivationTime) debería compararse con currentConfig.riegoFrecuenciaDias en milisegundos
        // Y también currentHour == currentConfig.riegoHoraDisparo
        // Esta es una simplificación y necesita una lógica de fecha más robusta.
        // Por ahora, asumimos que lastRiegoActivationTime se actualiza correctamente.
        unsigned long freqMillis = (unsigned long)currentConfig.riegoFrecuenciaDias * 24 * 3600 * 1000UL;
        if (currentHour == currentConfig.riegoHoraDisparo && (currentTime - lastRiegoActivationTime >= freqMillis || lastRiegoActivationTime == 0 /*primer riego*/)) {
            // Para evitar múltiples disparos en la misma hora, necesitamos un flag de "ya regué hoy a esta hora"
            // Esta es una simplificación:
            static int lastRiegoDay = -1; // Día del año del último riego
            if (timeClient.getDay() != lastRiegoDay || freqMillis == 0 /* si es el primer riego */) {
                 controlRelay(3, true);
                 riegoActive = true;
                 riegoStartTime = currentTime;
                 lastRiegoActivationTime = currentTime; // Actualizar para la próxima frecuencia
                 lastRiegoDay = timeClient.getDay();
                 printSerialDebug("Riego: Iniciando ciclo.");
            }
        }
    }
}

void checkTestModes() {
    unsigned long currentTime = millis();
    static unsigned long lastTestBlinkTime[NUM_RELAYS] = {0};
    static bool testBlinkState[NUM_RELAYS] = {false};

    bool modes[] = {currentConfig.lucesModoTest, currentConfig.ventModoTest, currentConfig.extrModoTest, currentConfig.riegoModoTest};

    for (int i = 0; i < NUM_RELAYS; ++i) {
        if (modes[i]) { // Si el modo test está activo para este relé
            if (currentTime - lastTestBlinkTime[i] >= 5000) { // Cada 5 segundos
                testBlinkState[i] = true; // Encender
                controlRelay(i, true, true); // Forzar encendido
                lastTestBlinkTime[i] = currentTime;
                printSerialDebug(String(relayNames[i]) + " Modo Test: ON (1s)");
            } else if (testBlinkState[i] && (currentTime - lastTestBlinkTime[i] >= 1000)) { // 1 segundo después
                testBlinkState[i] = false; // Apagar
                controlRelay(i, false, true); // Forzar apagado
                // No actualizar lastTestBlinkTime[i] aquí para que el próximo ciclo de 5s comience desde el ON
            }
        } else {
            // Si el modo test se acaba de desactivar, asegurar que el relé vuelva a su estado programado
            if (testBlinkState[i]) { // Estaba en test y se apagó el test
                 testBlinkState[i] = false; // Resetear estado de test
                 // La lógica de applyScheduledControls se encargará de poner el estado correcto.
                 // O podríamos forzar una reevaluación aquí.
            }
        }
    }
}


// --- Interfaz de Usuario (LCD y Encoder) ---
// ISR para el encoder (CLK y DT)
void IRAM_ATTR handleEncoderInterrupt() {
    // Implementación básica de lectura de encoder.
    // Una librería dedicada como ESP32Encoder es más robusta.
    static int8_t lastCLK = LOW;
    static int8_t lastDT = LOW;
    int8_t clkState = digitalRead(ENCODER_CLK_PIN);
    int8_t dtState = digitalRead(ENCODER_DT_PIN);

    if (clkState != lastCLK) {
        if (clkState == LOW) { // Flanco descendente de CLK
            if (dtState == HIGH) { // Si DT es HIGH, giro antihorario
                encoderPos--;
            } else { // Si DT es LOW, giro horario
                encoderPos++;
            }
        }
        lastCLK = clkState;
    }
    // Podrías añadir lógica similar para DT si quieres doble resolución,
    // pero usualmente una es suficiente y más estable.
}

// ISR para el botón del encoder
void IRAM_ATTR handleEncoderButtonInterrupt() {
    // Debounce simple, en una ISR no se puede usar delay.
    static unsigned long lastInterruptTime = 0;
    unsigned long interruptTime = millis();
    if (interruptTime - lastInterruptTime > 200) { // 200ms debounce
        encoderSWPressed = true;
        lastInterruptTime = interruptTime;
    }
}

void processEncoderChanges() {
    if (encoderSWPressed) {
        printSerialDebug("Encoder SW presionado.");
        // Lógica de click del encoder
        handleEncoderClick();
        encoderSWPressed = false; // Resetear flag
        resetMenuTimeout();
    }

    if (encoderPos != lastEncoderPos) {
        int diff = encoderPos - lastEncoderPos;
        printSerialDebug("Encoder girado. Pos: " + String(encoderPos) + ", Diff: " + String(diff));
        // Lógica de rotación del encoder
        handleEncoderRotation(diff);
        lastEncoderPos = encoderPos;
        resetMenuTimeout();
    }
}

void updateDisplay() {
    if (currentUIState == UI_TEMP_MESSAGE) {
        if (millis() - tempMessageStartTime >= tempMessageDuration) {
            currentUIState = previousUIState; // Volver al estado anterior
            // Forzar redibujo de la pantalla anterior
            if (currentUIState == UI_HOME) displayHomeScreen();
            else if (currentUIState == UI_MENU_MAIN || currentUIState == UI_MENU_SUB) displayMenuScreen();
            // Añadir otros estados si es necesario
        }
        return; // No hacer nada más si estamos mostrando un mensaje temporal
    }

    // Lógica para dibujar la pantalla según currentUIState
    switch (currentUIState) {
        case UI_HOME:
            // La pantalla HOME se actualiza por eventos (DHT, NTP, WiFi status)
            // No necesita redibujo constante aquí a menos que algo cambie.
            break;
        case UI_MENU_MAIN:
        case UI_MENU_SUB:
            displayMenuScreen(); // Se redibuja si currentMenuItem cambia
            break;
        case UI_EDIT_VALUE:
            displayEditValueScreen();
            break;
        case UI_CONFIRM_ACTION:
            displayConfirmScreen();
            break;
        default:
            break;
    }
}

void displayHomeScreen() {
    lcd.clear();
    // Línea 1: Temperatura y Humedad
    String tempStr = (currentTemperature == -99.0) ? "--.-" : String(currentTemperature, 1);
    String humStr = (currentHumidity == -99.0) ? "--.-" : String(currentHumidity, 1);
    lcd.setCursor(0, 0);
    lcd.print("T:" + tempStr + "C H:" + humStr + "%");

    // Línea 2: Estado WiFi y Hora
    lcd.setCursor(0, 1);
    if (wifiConnected && timeClient.isTimeSet()) {
        lcd.print("Online  " + getFormattedTime());
    } else if (wifiConnected && !timeClient.isTimeSet()) {
        lcd.print("Online  --:--"); // Conectado pero sin hora
    } else if (!wifiConnected && !wifiManager.getConfigPortalActive()) { // No conectado y no en portal
         lcd.print("Offline --:--");
    } else if (wifiManager.getConfigPortalActive()) { // En modo portal de configuración
        lcd.setCursor(0,0);
        lcd.print("Conectar a:");
        lcd.setCursor(0,1);
        String apName = String(AP_SSID_PREFIX) + WiFi.macAddress().substring(12);
        apName.replace(":", ""); // WiFiManager puede quitar los dos puntos
        lcd.print(apName.substring(0,16)); // Mostrar solo los primeros 16 caracteres
    } else { // Caso por defecto si estamos en modo AP manual (no WiFiManager)
        lcd.print("Modo AP"); 
        // Aquí mostrarías el SSID del AP que creaste manualmente
    }
}

void displayMenuScreen() {
    lcd.clear();
    // Esta es una implementación muy básica. Un menú real necesita scrolling si hay más ítems que filas.
    // Y manejar submenús.
    // Por ahora, solo muestra el ítem actual y el siguiente si cabe.
    if (currentUIState == UI_MENU_MAIN) {
        lcd.setCursor(0, 0);
        lcd.print(">" + String(mainMenu[currentMenuItem]));
        if (currentMenuItem + 1 < MAIN_MENU_ITEMS) {
            lcd.setCursor(1, 1); // Sin el ">"
            lcd.print(String(mainMenu[currentMenuItem + 1]));
        }
    } else if (currentUIState == UI_MENU_SUB) {
        // Lógica para mostrar submenús
        // Ejemplo: Si mainMenu[currentMenuItem] es "Sistema"
        if (strcmp(mainMenu[currentMenuItem], "6. Sistema") == 0) {
            // Aquí iría la lógica para mostrar los ítems del submenú de Sistema
            // const char* sistemaSubMenu[] = {"WiFi", "IP", "Olvidar Red", ...};
            // lcd.print(">" + String(sistemaSubMenu[currentSubMenuItem]));
            lcd.print("Submenu Sistema"); // Placeholder
        } else {
             lcd.print("Submenu Items"); // Placeholder
        }
    }
}

void displayEditValueScreen() {
    lcd.clear();
    lcd.setCursor(0,0);
    // Aquí mostrarías el nombre del parámetro y su valor actual.
    // Ejemplo: Si editamos currentConfig.thFrecuenciaMuestreo
    // lcd.print("TH Freq: " + String(currentConfig.thFrecuenciaMuestreo) + "m");
    lcd.print("Editando Valor:"); // Placeholder
    lcd.setCursor(0,1);
    // lcd.print("Girar: Cambiar"); // Placeholder
    // lcd.print("Clic: Guardar"); // Placeholder
    // Dependiendo del parámetro, el valor se mostraría y actualizaría aquí.
    // Por ejemplo, si currentMenuItem es 0 (Temp/Hum) y editingParamIndex es 0 (Frecuencia):
    if (currentMenuItem == 0 && editingParamIndex == 0) {
        lcd.print("TH Freq: " + String(currentConfig.thFrecuenciaMuestreo) + " min");
    } else {
        lcd.print("Valor: XX"); // Placeholder
    }
}

void displayConfirmScreen() {
    lcd.clear();
    lcd.setCursor(0,0);
    // Ejemplo: Si se va a "Olvidar Red WiFi"
    // lcd.print("Olvidar Red?");
    lcd.print("Confirmar Accion?"); // Placeholder
    lcd.setCursor(0,1);
    lcd.print(">No     Si"); // Placeholder para selección Sí/No
}

void displayTemporaryMessage(String line1, String line2, int durationMs) {
    if (currentUIState == UI_TEMP_MESSAGE) { // Si ya hay un mensaje, no interrumpir
        // Podríamos encolar mensajes si fuera necesario
        return;
    }
    previousUIState = currentUIState; // Guardar estado actual para volver
    currentUIState = UI_TEMP_MESSAGE;
    tempMessageStartTime = millis();
    tempMessageDuration = durationMs;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1.substring(0, LCD_COLS));
    lcd.setCursor(0, 1);
    lcd.print(line2.substring(0, LCD_COLS));
    printSerialDebug("LCD Temp Msg: " + line1 + " / " + line2);
}

void handleEncoderRotation(int diff) {
    // diff > 0 es horario, diff < 0 es antihorario
    switch (currentUIState) {
        case UI_HOME:
            // Girar desde HOME entra al menú
            currentUIState = UI_MENU_MAIN;
            currentMenuItem = 0; // Empezar en el primer ítem
            printSerialDebug("UI: HOME -> MENU_MAIN");
            displayMenuScreen(); // Actualizar display inmediatamente
            break;
        case UI_MENU_MAIN:
            currentMenuItem += (diff > 0 ? 1 : -1);
            if (currentMenuItem < 0) currentMenuItem = MAIN_MENU_ITEMS - 1;
            if (currentMenuItem >= MAIN_MENU_ITEMS) currentMenuItem = 0;
            displayMenuScreen();
            break;
        case UI_MENU_SUB:
            // Lógica para navegar submenús
            // currentSubMenuItem += (diff > 0 ? 1 : -1);
            // ... (manejar límites del submenú)
            displayMenuScreen();
            break;
        case UI_EDIT_VALUE:
            // Lógica para cambiar el valor que se está editando
            // Esto es muy dependiente del parámetro específico
            // Ejemplo para currentConfig.thFrecuenciaMuestreo (min 1, max 60)
            if (currentMenuItem == 0 && editingParamIndex == 0) { // Temp/Hum -> Frecuencia
                currentConfig.thFrecuenciaMuestreo += (diff > 0 ? 1 : -1);
                if (currentConfig.thFrecuenciaMuestreo < 1) currentConfig.thFrecuenciaMuestreo = 1;
                if (currentConfig.thFrecuenciaMuestreo > 60) currentConfig.thFrecuenciaMuestreo = 60;
            }
            // Añadir más casos para otros parámetros (lucesHoraON, etc.)
            // ...
            displayEditValueScreen();
            break;
        case UI_CONFIRM_ACTION:
            // Lógica para cambiar entre "Sí" y "No"
            // Ejemplo: si currentSubMenuItem es 0 para "No", 1 para "Sí"
            // currentSubMenuItem = (currentSubMenuItem == 0 ? 1 : 0);
            displayConfirmScreen();
            break;
        default:
            break;
    }
}

void handleEncoderClick() {
    switch (currentUIState) {
        case UI_HOME:
            // Clic desde HOME también podría entrar al menú (opcional)
            currentUIState = UI_MENU_MAIN;
            currentMenuItem = 0;
            printSerialDebug("UI: HOME -> MENU_MAIN (por click)");
            displayMenuScreen();
            break;
        case UI_MENU_MAIN:
            printSerialDebug("UI: Click en MENU_MAIN, item: " + String(mainMenu[currentMenuItem]));
            // Lógica para entrar a submenú, editar valor o ejecutar acción
            if (strcmp(mainMenu[currentMenuItem], "7. Salir") == 0) {
                currentUIState = UI_HOME;
                displayHomeScreen();
            } else if (strcmp(mainMenu[currentMenuItem], "1. Temp/Hum") == 0) {
                // Este ítem tiene sub-parámetros para editar
                // currentUIState = UI_MENU_SUB; // O directamente a editar el primer param
                currentUIState = UI_EDIT_VALUE;
                editingParamIndex = 0; // Empezar editando Frecuencia de TH
                // Cargar el valor actual para edición (ya está en currentConfig)
                displayEditValueScreen();
            } else if (strcmp(mainMenu[currentMenuItem], "6. Sistema") == 0) {
                currentUIState = UI_MENU_SUB;
                currentSubMenuItem = 0; // Ir al primer ítem del submenú de Sistema
                // Aquí necesitarías definir la estructura del submenú de Sistema
                displayMenuScreen(); // Mostrar el submenú
            }
            // ... más casos para otros ítems del menú principal
            break;
        case UI_MENU_SUB:
            // Lógica para click en un ítem de submenú
            // Puede ser entrar a editar, ejecutar acción, o ir a pantalla de confirmación
            // Ejemplo: si en Sistema->Olvidar Red
            // currentUIState = UI_CONFIRM_ACTION;
            // displayConfirmScreen();
            break;
        case UI_EDIT_VALUE:
            // Guardar el valor editado y volver al menú anterior
            printSerialDebug("UI: Valor confirmado/guardado.");
            savePreferences(); // Guardar todas las preferencias (o solo la modificada)
            // Volver al menú principal o submenú desde donde se entró a editar
            // Esto necesita una mejor gestión de "de dónde vengo"
            currentUIState = UI_MENU_MAIN; // Simplificación
            displayMenuScreen();
            break;
        case UI_CONFIRM_ACTION:
            // Lógica para ejecutar la acción si se confirmó "Sí"
            // Ejemplo: si se confirmó "Olvidar Red WiFi"
            // if (currentSubMenuItem == 1 /* "Sí" */) {
            //    resetWiFiCredentialsAndRestart();
            // } else {
            //    currentUIState = UI_MENU_SUB; // Volver al submenú
            //    displayMenuScreen();
            // }
            currentUIState = UI_MENU_SUB; // Simplificación
            displayMenuScreen();
            break;
        default:
            break;
    }
}

void resetMenuTimeout() {
    lastEncoderActivityTime = millis();
}

void checkMenuTimeout() {
    if (currentUIState != UI_HOME && currentUIState != UI_TEMP_MESSAGE) {
        if (millis() - lastEncoderActivityTime >= MENU_TIMEOUT_MS) {
            printSerialDebug("Timeout de menu, volviendo a HOME.");
            currentUIState = UI_HOME;
            displayHomeScreen();
        }
    }
}


// --- Gestión WiFi y NTP ---
void setupWiFi() {
    WiFi.mode(WIFI_STA); // Empezar en modo Station
    wifiManager.setDebugOutput(true); // O false para menos output serial
    wifiManager.setMinimumSignalQuality(20); // Calidad mínima de señal para mostrar redes
    // wifiManager.setConfigPortalTimeout(180); // Timeout para el portal en segundos (0 = sin timeout)
    
    // Nombre del AP para el portal cautivo
    String apName = String(AP_SSID_PREFIX) + WiFi.macAddress().substring(12);
    apName.replace(":", ""); // WiFiManager puede quitar los dos puntos

    // Intentar conectar con credenciales guardadas o iniciar portal
    // El true final significa que si la conexión falla, iniciará el portal AP
    // El false significaría que solo intenta conectar y luego tú manejas el AP.
    // Usaremos autoConnect que es más simple.
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Conectando WiFi");
    printSerialDebug("Intentando conectar a WiFi o iniciar portal...");

    // Callback para cuando entra en modo configuración
    wifiManager.setAPCallback([](WiFiManager *myWiFiManager) {
        printSerialDebug("Entrando en modo Portal de Configuracion AP.");
        String currentApName = myWiFiManager->getConfigPortalSSID();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Conectar a:");
        lcd.setCursor(0, 1);
        lcd.print(currentApName.substring(0,16));
        // El parpadeo del LED se manejará en blinkLED() basado en wifiManager.getConfigPortalActive()
    });

    // Callback para cuando se guardan nuevas credenciales (opcional)
    wifiManager.setSaveConfigCallback([]() {
        printSerialDebug("Nuevas credenciales WiFi guardadas. Reiniciando...");
        // WiFiManager usualmente reinicia solo, pero podemos forzarlo.
        // ESP.restart(); // Descomentar si es necesario.
    });
    
    // Si no se conecta en X segundos, inicia el portal.
    // El timeout es para el intento de conexión, no para el portal en sí.
    wifiManager.setConfigPortalTimeout(120); // Espera 2 minutos para conectar antes de abrir portal

    if (wifiManager.autoConnect(apName.c_str())) {
        printSerialDebug("WiFi conectado! SSID: " + WiFi.SSID());
        printSerialDebug("IP: " + WiFi.localIP().toString());
        wifiConnected = true;
    } else {
        printSerialDebug("Fallo al conectar WiFi y el portal de configuracion se cerro o tuvo timeout.");
        printSerialDebug("El dispositivo puede necesitar reinicio o no tendra conectividad.");
        wifiConnected = false;
        // Aquí podrías decidir reiniciar o entrar en un modo offline limitado.
        // Por ahora, se quedará offline y el LCD lo indicará.
        // La función displayHomeScreen se encargará de mostrar el estado del AP si WiFiManager lo dejó activo.
    }
    displayHomeScreen(); // Actualizar LCD con el estado final
}


void updateNTP() {
    if (wifiConnected && timeClient.isTimeSet()) { // Solo actualizar si ya tenemos una hora válida
      if (timeClient.update()) {
        // printSerialDebug("NTP actualizado: " + timeClient.getFormattedTime());
      } else {
        // printSerialDebug("Fallo al actualizar NTP.");
      }
    } else if (wifiConnected && !timeClient.isTimeSet()) { // Si conectado pero sin hora, intentar obtenerla
        if (timeClient.forceUpdate()) { // Forzar primer update
            printSerialDebug("NTP obtenido por primera vez: " + timeClient.getFormattedTime());
            displayHomeScreen(); // Actualizar LCD con la hora
        } else {
            printSerialDebug("Fallo al obtener NTP inicial.");
        }
    }
}

String getFormattedTime(bool withSeconds) {
    if (!wifiConnected || !timeClient.isTimeSet()) {
        return withSeconds ? "--:--:--" : "--:--";
    }
    String timeStr = timeClient.getFormattedTime(); // HH:MM:SS
    if (withSeconds) {
        return timeStr;
    } else {
        return timeStr.substring(0, 5); // HH:MM
    }
}

String getFormattedDateTime() {
    if (!wifiConnected || !timeClient.isTimeSet()) {
        return "---- -- --:--:--"; // Placeholder si no hay hora
    }
    time_t epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime(&epochTime); // O localtime() si quieres hora local con zona horaria configurada
                                         // pero NTPClient ya suele dar la hora con offset.
                                         // Para gmtime, necesitarías añadir el offset manualmente si es UTC 0.
                                         // Como usamos pool.ntp.org y offset 0, esto es UTC.
                                         // Para hora local Argentina (-3), el offset en NTPClient debería ser -3*3600.
                                         // O usar funciones de manejo de zona horaria de C++.

    // Formato AAAA-MM-DD-HH-MM-SS
    char buffer[20];
    // strftime(buffer, sizeof(buffer), "%Y-%m-%d-%H-%M-%S", localtime(&epochTime)); // Para hora local con TZ del sistema
    // Para UTC directo de timeClient con offset 0:
    int year = ptm->tm_year + 1900;
    int month = ptm->tm_mon + 1;
    int day = ptm->tm_mday;
    // Los H,M,S ya los da bien timeClient.getFormattedTime()
    String formattedTime = timeClient.getFormattedTime(); // HH:MM:SS
    
    sprintf(buffer, "%04d-%02d-%02d-%s", year, month, day, formattedTime.c_str());
    return String(buffer);
}

String getLocalIP() {
    if (wifiConnected) {
        return WiFi.localIP().toString();
    } else if (wifiManager.getConfigPortalActive()) {
        return WiFi.softAPIP().toString(); // IP del portal
    }
    return "N/A";
}

// --- Gestión de Preferencias ---
void loadDefaultPreferences() {
    printSerialDebug("Cargando preferencias por defecto.");
    currentConfig.thFrecuenciaMuestreo = 60;
    currentConfig.thModoTest = false;
    currentConfig.lucesHoraON = 6;
    currentConfig.lucesHoraOFF = 0;
    currentConfig.lucesModoTest = false;
    currentConfig.ventHoraON = 6;
    currentConfig.ventHoraOFF = 0;
    currentConfig.ventModoTest = false;
    currentConfig.extrFrecuenciaHoras = 0;
    currentConfig.extrDuracionMinutos = 1;
    currentConfig.extrModoTest = false;
    currentConfig.riegoFrecuenciaDias = 0;
    currentConfig.riegoDuracionMinutos = 1;
    currentConfig.riegoHoraDisparo = 21;
    currentConfig.riegoModoTest = false;
    currentConfig.registroFrecuenciaHoras = 1;
    currentConfig.registroTamMax = 4380;
    currentConfig.registroModoTest = false;
    // Guardar los defaults para que existan en la próxima carga
    savePreferences();
}

void loadPreferences() {
    // preferences.begin(PREFERENCES_NAMESPACE, false) ya se llamó en setup
    // Si una clave no existe, getInt/getBool devuelve el segundo argumento (default)
    printSerialDebug("Cargando preferencias...");
    bool defaultsLoaded = false;
    if (!preferences.isKey(KEY_TH_FREQ)) { // Si una clave principal no existe, asumimos que son defaults
        loadDefaultPreferences();
        defaultsLoaded = true;
    }

    if (!defaultsLoaded) { // Solo cargar si no se usaron defaults recién
        currentConfig.thFrecuenciaMuestreo = preferences.getInt(KEY_TH_FREQ, 60);
        currentConfig.thModoTest = preferences.getBool(KEY_TH_TEST, false);
        currentConfig.lucesHoraON = preferences.getInt(KEY_LUCES_ON, 6);
        currentConfig.lucesHoraOFF = preferences.getInt(KEY_LUCES_OFF, 0);
        currentConfig.lucesModoTest = preferences.getBool(KEY_LUCES_TEST, false);
        currentConfig.ventHoraON = preferences.getInt(KEY_VENT_ON, 6);
        currentConfig.ventHoraOFF = preferences.getInt(KEY_VENT_OFF, 0);
        currentConfig.ventModoTest = preferences.getBool(KEY_VENT_TEST, false);
        currentConfig.extrFrecuenciaHoras = preferences.getInt(KEY_EXTR_FREQ, 0);
        currentConfig.extrDuracionMinutos = preferences.getInt(KEY_EXTR_DUR, 1);
        currentConfig.extrModoTest = preferences.getBool(KEY_EXTR_TEST, false);
        currentConfig.riegoFrecuenciaDias = preferences.getInt(KEY_RIEGO_FREQ, 0);
        currentConfig.riegoDuracionMinutos = preferences.getInt(KEY_RIEGO_DUR, 1);
        currentConfig.riegoHoraDisparo = preferences.getInt(KEY_RIEGO_HORA, 21);
        currentConfig.riegoModoTest = preferences.getBool(KEY_RIEGO_TEST, false);
        currentConfig.registroFrecuenciaHoras = preferences.getInt(KEY_REG_FREQ, 1);
        currentConfig.registroTamMax = preferences.getInt(KEY_REG_MAX, 4380);
        currentConfig.registroModoTest = preferences.getBool(KEY_REG_TEST, false);
        printSerialDebug("Preferencias cargadas desde NVS.");
    }
}

void savePreferences() {
    printSerialDebug("Guardando preferencias...");
    preferences.putInt(KEY_TH_FREQ, currentConfig.thFrecuenciaMuestreo);
    preferences.putBool(KEY_TH_TEST, currentConfig.thModoTest);
    preferences.putInt(KEY_LUCES_ON, currentConfig.lucesHoraON);
    preferences.putInt(KEY_LUCES_OFF, currentConfig.lucesHoraOFF);
    preferences.putBool(KEY_LUCES_TEST, currentConfig.lucesModoTest);
    preferences.putInt(KEY_VENT_ON, currentConfig.ventHoraON);
    preferences.putInt(KEY_VENT_OFF, currentConfig.ventHoraOFF);
    preferences.putBool(KEY_VENT_TEST, currentConfig.ventModoTest);
    preferences.putInt(KEY_EXTR_FREQ, currentConfig.extrFrecuenciaHoras);
    preferences.putInt(KEY_EXTR_DUR, currentConfig.extrDuracionMinutos);
    preferences.putBool(KEY_EXTR_TEST, currentConfig.extrModoTest);
    preferences.putInt(KEY_RIEGO_FREQ, currentConfig.riegoFrecuenciaDias);
    preferences.putInt(KEY_RIEGO_DUR, currentConfig.riegoDuracionMinutos);
    preferences.putInt(KEY_RIEGO_HORA, currentConfig.riegoHoraDisparo);
    preferences.putBool(KEY_RIEGO_TEST, currentConfig.riegoModoTest);
    preferences.putInt(KEY_REG_FREQ, currentConfig.registroFrecuenciaHoras);
    preferences.putInt(KEY_REG_MAX, currentConfig.registroTamMax);
    preferences.putBool(KEY_REG_TEST, currentConfig.registroModoTest);
    printSerialDebug("Preferencias guardadas en NVS.");
}

void resetWiFiCredentialsAndRestart() {
    printSerialDebug("Borrando credenciales WiFi y reiniciando...");
    wifiManager.resetSettings(); // Borra las credenciales guardadas
    delay(1000);
    ESP.restart();
}


// --- Gestión de Archivos (Log y Web) ---
void initLittleFS() {
    if (!LittleFS.begin(true)) { // true formatea si no se puede montar
        printSerialDebug("Error al montar LittleFS. Se formateara.");
        if (!LittleFS.begin(true)) { // Intentar de nuevo formateando
             printSerialDebug("Fallo critico al montar/formatear LittleFS.");
             // Aquí podrías entrar en un bucle infinito o reiniciar
             return;
        }
    }
    printSerialDebug("LittleFS montado correctamente.");
    // Listar archivos (opcional, para debug)
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while(file){
        printSerialDebug("FS File: " + String(file.name()) + " Size: " + String(file.size()));
        file = root.openNextFile();
    }
    root.close();
    file.close();
}

void logData() {
    if (!timeClient.isTimeSet()) {
        printSerialDebug("No se puede registrar: hora no sincronizada.");
        return;
    }
    rotateLogIfNeeded(); // Asegurar que haya espacio antes de escribir

    File logFile = LittleFS.open(LOG_FILE, FILE_APPEND);
    if (!logFile) {
        printSerialDebug("Error al abrir archivo de log para append.");
        return;
    }

    String dateTime = getFormattedDateTime();
    String logEntry = dateTime + "," +
                      String(currentTemperature) + "," +
                      String(currentHumidity) + "," +
                      (relayStates[0] ? "ON" : "OFF") + "," + // Luces
                      (relayStates[1] ? "ON" : "OFF") + "," + // Ventilación
                      (relayStates[2] ? "ON" : "OFF") + "," + // Extracción
                      (relayStates[3] ? "ON" : "OFF");       // Riego

    if (logFile.println(logEntry)) {
        printSerialDebug("Datos registrados: " + logEntry);
    } else {
        printSerialDebug("Error al escribir en el archivo de log.");
    }
    logFile.close();
}

int getLogEntryCount() {
    File logFile = LittleFS.open(LOG_FILE, FILE_READ);
    if (!logFile) {
        return 0;
    }
    int count = 0;
    while (logFile.available()) {
        logFile.readStringUntil('\n');
        count++;
    }
    logFile.close();
    return count;
}

void rotateLogIfNeeded() {
    int currentEntries = getLogEntryCount();
    if (currentEntries < currentConfig.registroTamMax) {
        return; // No se necesita rotar
    }
    printSerialDebug("Rotando log. Entradas actuales: " + String(currentEntries) + ", Max: " + String(currentConfig.registroTamMax));

    File oldLog = LittleFS.open(LOG_FILE, FILE_READ);
    if (!oldLog) {
        printSerialDebug("Error al abrir log para rotar (lectura).");
        return;
    }

    String tempLogFileName = String(LOG_FILE) + ".tmp";
    File tempLog = LittleFS.open(tempLogFileName.c_str(), FILE_WRITE);
    if (!tempLog) {
        printSerialDebug("Error al crear archivo de log temporal.");
        oldLog.close();
        return;
    }

    // Omitir la primera línea (la más antigua)
    if (oldLog.available()) {
        oldLog.readStringUntil('\n'); 
    }

    // Copiar el resto de las líneas al archivo temporal
    while (oldLog.available()) {
        tempLog.println(oldLog.readStringUntil('\n'));
    }
    oldLog.close();
    tempLog.close();

    // Reemplazar el log antiguo con el temporal
    if (!LittleFS.remove(LOG_FILE)) {
        printSerialDebug("Error al borrar el archivo de log original.");
    } else {
        if (!LittleFS.rename(tempLogFileName.c_str(), LOG_FILE)) {
            printSerialDebug("Error al renombrar el archivo de log temporal.");
        } else {
            printSerialDebug("Log rotado exitosamente.");
        }
    }
}

void serveFile(String path, String contentType) {
    if (LittleFS.exists(path)) {
        File file = LittleFS.open(path, "r");
        server.streamFile(file, contentType);
        file.close();
        printSerialDebug("Sirviendo archivo: " + path);
    } else {
        printSerialDebug("Archivo no encontrado para servir: " + path);
        handleNotFound();
    }
}

// --- Servidor Web ---
void setupWebServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/data", HTTP_GET, handleData);
    server.on("/control", HTTP_GET, handleControl); // Podría ser POST también
    server.on("/riego_manual", HTTP_GET, handleRiegoManual);
    server.on("/descargarLogCsv", HTTP_GET, handleDownloadLog);
    server.on("/ultimasEntradasLog", HTTP_GET, handleLastLogEntries);
    // server.on("/wifisave", HTTP_POST, handleWiFiSaveAndRestart); // Para portal cautivo manual
    server.onNotFound(handleNotFound);
    // No iniciar server.begin() aquí, se hace en setup() después de conectar WiFi
}

void handleRoot() {
    serveFile(WEB_MONITOR_HTML_FILE, "text/html");
}

void handleData() {
    // Crear JSON con datos actuales
    // Ejemplo: {"temperatura": 23.5, "humedad": 58.2, "luces": "ON", ...}
    String json = "{";
    json += "\"temperatura\":" + String(currentTemperature, 1) + ",";
    json += "\"humedad\":" + String(currentHumidity, 1) + ",";
    json += "\"luces\":\"" + String(relayStates[0] ? "ON" : "OFF") + "\",";
    json += "\"ventilacion\":\"" + String(relayStates[1] ? "ON" : "OFF") + "\",";
    json += "\"extraccion\":\"" + String(relayStates[2] ? "ON" : "OFF") + "\",";
    json += "\"riego\":\"" + String(relayStates[3] ? "ON" : "OFF") + "\"";
    // Podrías añadir más datos como hora, IP, SSID, etc.
    json += "}";
    server.send(200, "application/json", json);
    printSerialDebug("Sirviendo /data: " + json);
}

void handleControl() {
    String componente = server.arg("componente");
    String accion = server.arg("accion"); // "on", "off", o "toggle"

    printSerialDebug("Recibido comando /control: componente=" + componente + ", accion=" + accion);

    int relayIndex = -1;
    if (componente == "luces") relayIndex = 0;
    else if (componente == "ventilacion") relayIndex = 1;
    else if (componente == "extraccion") relayIndex = 2;
    else if (componente == "riego") relayIndex = 3;

    if (relayIndex != -1) {
        bool newState;
        if (accion == "on") newState = true;
        else if (accion == "off") newState = false;
        else if (accion == "toggle") newState = !relayStates[relayIndex];
        else {
            server.send(400, "text/plain", "Accion invalida");
            return;
        }
        // Al controlar desde la web, desactivar el modo test de ese relé si estaba activo
        if (relayIndex == 0) currentConfig.lucesModoTest = false;
        else if (relayIndex == 1) currentConfig.ventModoTest = false;
        else if (relayIndex == 2) currentConfig.extrModoTest = false;
        else if (relayIndex == 3) currentConfig.riegoModoTest = false;
        savePreferences(); // Guardar el cambio de modo test

        controlRelay(relayIndex, newState, true); // true para manualOverride
        server.send(200, "text/plain", "OK, " + componente + " " + (newState ? "ON" : "OFF"));
    } else {
        server.send(400, "text/plain", "Componente invalido");
    }
}

void handleRiegoManual() {
    printSerialDebug("Recibido comando /riego_manual");
    // Lógica para activar el riego manualmente por X tiempo
    // Similar a como se activa desde el menú o por horario, pero es un ciclo único.
    // Podrías usar la duración configurada para el riego o una fija.
    if (!riegoActive) { // Solo si no está ya regando
        currentConfig.riegoModoTest = false; // Desactivar modo test si se activa manual
        savePreferences();

        controlRelay(3, true, true); // Encender riego
        riegoActive = true;
        riegoStartTime = millis(); // Usará la duración de currentConfig.riegoDuracionMinutos
        printSerialDebug("Riego manual iniciado.");
        server.send(200, "text/plain", "OK, Riego manual iniciado");
    } else {
        server.send(200, "text/plain", "Riego ya activo");
    }
}

void handleDownloadLog() {
    serveFile(LOG_FILE, "text/csv");
}

void handleLastLogEntries() {
    int linesToShow = 15; // Por defecto
    if (server.hasArg("lineas")) {
        linesToShow = server.arg("lineas").toInt();
        if (linesToShow <= 0 || linesToShow > 100) linesToShow = 15; // Limitar
    }

    File logFile = LittleFS.open(LOG_FILE, FILE_READ);
    if (!logFile) {
        server.send(500, "text/plain", "Error al abrir log");
        return;
    }

    // Contar líneas totales para saber dónde empezar a leer si son muchas
    // O leer todo y tomar las últimas N (más simple para pocas líneas, ineficiente para logs grandes)
    // Para este ejemplo, leemos todo y tomamos las últimas N.
    String logContent = "";
    int lineCount = 0;
    String lines[linesToShow]; // Array para guardar las últimas N líneas
    int currentLineIndex = 0;

    while (logFile.available()) {
        String line = logFile.readStringUntil('\n');
        lines[currentLineIndex % linesToShow] = line;
        currentLineIndex++;
        lineCount++;
    }
    logFile.close();

    String output = "";
    // Reconstruir en el orden correcto
    int startIdx = (lineCount < linesToShow) ? 0 : (currentLineIndex % linesToShow);
    for (int i = 0; i < min(linesToShow, lineCount); ++i) {
        output += lines[startIdx] + "\n";
        startIdx = (startIdx + 1) % linesToShow;
    }
    
    server.send(200, "text/plain", output);
    printSerialDebug("Sirviendo ultimas " + String(linesToShow) + " lineas del log.");
}


void handleNotFound() {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
    printSerialDebug("HTTP 404: " + server.uri());
}

// --- Utilidades ---
void blinkLED() {
    unsigned long currentTime = millis();
    
    if (wifiManager.getConfigPortalActive()) { // Modo Configuración (Portal AP activo)
        if (currentTime - lastLEDToggleTime >= ledBlinkIntervalConfig) {
            ledBlinkState = !ledBlinkState;
            digitalWrite(LED_BUILTIN_PIN, ledBlinkState);
            lastLEDToggleTime = currentTime;
        }
    } else if (wifiConnected) { // Modo Normal (Conectado a WiFi)
        if (ledBlinkCount < NORMAL_MODE_BLINK_COUNT * 2) { // *2 porque es ON y OFF
            if (currentTime - lastLEDToggleTime >= ledBlinkIntervalNormal) {
                ledBlinkState = !ledBlinkState;
                digitalWrite(LED_BUILTIN_PIN, ledBlinkState);
                lastLEDToggleTime = currentTime;
                ledBlinkCount++;
            }
        } else { // Pausa larga
            if (currentTime - lastLEDToggleTime >= ledBlinkPauseNormal) {
                ledBlinkCount = 0; // Reiniciar ciclo de parpadeo
                lastLEDToggleTime = currentTime; // Para que el primer blink del nuevo ciclo sea inmediato
            }
        }
    } else { // Offline o intentando conectar (sin portal activo)
        // Podrías tener otro patrón de parpadeo, por ahora lo dejamos apagado o parpadeo lento
         if (currentTime - lastLEDToggleTime >= 2000) { // Parpadeo muy lento
            ledBlinkState = !ledBlinkState;
            digitalWrite(LED_BUILTIN_PIN, ledBlinkState);
            lastLEDToggleTime = currentTime;
        }
    }
}

void printSerialDebug(String message) {
    Serial.println("[" + String(millis() / 1000.0, 3) + "s] " + message);
}

// Función de utilidad para subir archivos a LittleFS durante el desarrollo.
// No llamar en producción normalmente.
void uploadFileToLittleFS(String localPath, String fsPath) {
    // Esta función es conceptual. Necesitarías una forma de obtener el contenido
    // del localPath (ej. desde una tarjeta SD, o hardcodeado como un string grande,
    // o a través de una carga HTTP desde una herramienta).
    // Por simplicidad, aquí asumimos que tienes el contenido en una variable.
    // Ejemplo: String fileContent = "<!DOCTYPE html><html>...</html>";
    
    // Para el caso real, el archivo armina-grow-monitor.html debe ser subido
    // usando el plugin "ESP32 Sketch Data Upload" del Arduino IDE,
    // o alguna otra herramienta de subida a LittleFS.
    
    // Este es solo un placeholder si quisieras crear un archivo de ejemplo.
    if (!LittleFS.exists(fsPath)) {
        printSerialDebug("Archivo " + fsPath + " no existe. Creando ejemplo...");
        File file = LittleFS.open(fsPath, FILE_WRITE);
        if (file) {
            file.println("<!DOCTYPE html><html><head><title>Armina Grow Monitor</title></head>");
            file.println("<body><h1>Armina Grow</h1><p>Contenido de prueba. Sube el archivo real.</p>");
            file.println("<p>Temperatura: <span id='temperatura'>--</span></p>");
            file.println("<p>Humedad: <span id='humedad'>--</span></p>");
            file.println("<script>");
            file.println("function fetchData() { fetch('/data').then(r=>r.json()).then(d => { document.getElementById('temperatura').innerText = d.temperatura; document.getElementById('humedad').innerText = d.humedad; }); }");
            file.println("setInterval(fetchData, 5000); fetchData();");
            file.println("</script></body></html>");
            file.close();
            printSerialDebug("Archivo de ejemplo " + fsPath + " creado.");
        } else {
            printSerialDebug("Error al crear archivo de ejemplo " + fsPath);
        }
    } else {
        printSerialDebug("Archivo " + fsPath + " ya existe.");
    }
}

