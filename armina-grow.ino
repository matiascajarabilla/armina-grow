// Armina Grow - Prototipo IoT para Cultivo Indoor con ESP32
// Versión 0.2.2 (Correcciones de compilación y comentarios mejorados)

// =============================================================================
// INCLUSIÓN DE LIBRERÍAS
// =============================================================================
#include <WiFi.h>              // Para la conectividad WiFi
#include <WebServer.h>         // Para crear el servidor web HTTP
#include <DNSServer.h>         // Para el portal cautivo (usado por WiFiManager)
#include <WiFiManager.h>       // Para la gestión de conexión WiFi y portal cautivo
#include <LiquidCrystal_I2C.h> // Para el display LCD I2C
#include <DHT.h>               // Para el sensor de Temperatura y Humedad DHT11/22
#include <Preferences.h>       // Para guardar configuraciones en memoria no volátil (NVS)
#include <LittleFS.h>          // Para el sistema de archivos (logs, página web)
#include <NTPClient.h>         // Para obtener la hora de internet (Network Time Protocol)
#include <WiFiUdp.h>           // Requerido por NTPClient para la comunicación UDP

// =============================================================================
// DEFINICIONES Y CONSTANTES GLOBALES
// =============================================================================
#define FIRMWARE_VERSION "0.2.2" // Versión actual del firmware

// --- Pines del Hardware ---
// Display LCD I2C
const int LCD_SDA_PIN = 21; // Pin SDA para I2C
const int LCD_SCL_PIN = 22; // Pin SCL para I2C
const int LCD_ADDRESS = 0x27; // Dirección I2C del módulo LCD
const int LCD_COLS = 16;    // Columnas del LCD
const int LCD_ROWS = 2;     // Filas del LCD

// Módulo de 4 Relés
const int RELAY1_PIN = 18; // Pin para el Relé 1 (Luces)
const int RELAY2_PIN = 19; // Pin para el Relé 2 (Ventilación)
const int RELAY3_PIN = 23; // Pin para el Relé 3 (Extracción)
const int RELAY4_PIN = 25; // Pin para el Relé 4 (Riego)
const int NUM_RELAYS = 4;  // Número total de relés
const int relayPins[NUM_RELAYS] = {RELAY1_PIN, RELAY2_PIN, RELAY3_PIN, RELAY4_PIN};
const char* relayNames[NUM_RELAYS] = {"Luces", "Ventilacion", "Extraccion", "Riego"};
bool relayStates[NUM_RELAYS] = {false, false, false, false}; // Estado actual (ON/OFF) de cada relé

// Sensor DHT11 de Temperatura y Humedad
const int DHT_PIN = 26;      // Pin de datos del sensor DHT
#define DHT_TYPE DHT11       // Tipo de sensor DHT (DHT11 o DHT22)
float currentTemperature = -99.0; // Valor inicial inválido para la temperatura
float currentHumidity = -99.0;  // Valor inicial inválido para la humedad

// Encoder Rotativo
const int ENCODER_CLK_PIN = 13; // Pin CLK del encoder
const int ENCODER_DT_PIN = 14;  // Pin DT del encoder
const int ENCODER_SW_PIN = 27;  // Pin SW (botón) del encoder
volatile long encoderPos = 0;   // Posición actual del encoder (modificada por ISR)
volatile long lastReportedEncoderPos = 0; // Última posición del encoder reportada al loop principal
volatile bool encoderSWStatusChanged = false; // Flag para indicar cambio en el botón del encoder (manejado por ISR)
unsigned long lastEncoderActivityTime = 0;    // Timestamp de la última actividad del encoder (para timeout del menú)
const unsigned long MENU_TIMEOUT_MS = 10000;  // 10 segundos de inactividad para salir del menú

// LED Integrado en la placa ESP32
const int LED_BUILTIN_PIN = 2; // Pin del LED integrado (GPIO2 en muchos ESP32)

// --- Nombres de Archivos y Directorios en LittleFS ---
const char* PREFERENCES_NAMESPACE = "armina-grow"; // Espacio de nombres para Preferences
const char* LOG_FILE = "/armina-grow-log.csv";     // Nombre del archivo de registro de datos
const char* WEB_MONITOR_HTML_FILE = "/armina-grow-monitor.html"; // Nombre del archivo HTML para el monitor web

// --- Configuración WiFi y Servidor Web ---
const char* AP_SSID_PREFIX = "ArminaGrow-Setup"; // Prefijo para el SSID del AP en modo configuración
WebServer server(80);          // Objeto para el servidor web en el puerto 80
DNSServer dnsServer;           // Servidor DNS para el portal cautivo
WiFiManager wifiManager;       // Objeto para gestionar la conexión WiFi y el portal

// --- Configuración NTP (Protocolo de Tiempo de Red) ---
WiFiUDP ntpUDP; // Objeto UDP necesario para NTP
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600, 60000); // Cliente NTP: servidor, offset UTC-3 (Argentina) en segundos, intervalo de actualización 60s

// =============================================================================
// OBJETOS GLOBALES DE LIBRERÍAS
// =============================================================================
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS); // Objeto para el display LCD
DHT dht(DHT_PIN, DHT_TYPE);                             // Objeto para el sensor DHT
Preferences preferences;                                // Objeto para manejar las preferencias guardadas

// =============================================================================
// ESTRUCTURAS DE DATOS Y ENUMS PARA EL MENÚ
// =============================================================================
// Tipos de ítems que puede haber en el menú
enum MenuItemType {
    MENU_ITEM_SUBMENU,         // Ítem que abre otro menú (un submenú)
    MENU_ITEM_INT_VALUE,       // Ítem para editar un valor entero
    MENU_ITEM_BOOL_SI_NO,    // Ítem para editar un valor booleano (representado como Sí/No)
    MENU_ITEM_ACTION,          // Ítem que ejecuta una función (acción)
    MENU_ITEM_INFO_STRING,     // Ítem que muestra una cadena de texto fija
    MENU_ITEM_INFO_FUNCTION,   // Ítem que muestra información obtenida de una función
    MENU_ITEM_BACK             // Ítem para retroceder al menú anterior o salir
};

struct MenuItem; // Pre-declaración de la estructura MenuItem

// Definición de tipos para punteros a funciones usadas en el menú
typedef void (*MenuActionFunction)(); // Puntero a función para acciones del menú
typedef String (*MenuInfoFunction)();   // Puntero a función para obtener información a mostrar

// Estructura que define un ítem individual del menú
struct MenuItem {
    const char* name;           // Nombre del ítem que se muestra en el LCD
    MenuItemType type;          // Tipo de ítem
    union {                     // Unión para almacenar diferentes tipos de "objetivos"
        const MenuItem* subMenu;
        int* intValueTarget;
        bool* boolValueTarget;
        MenuActionFunction actionFunc;
        const char* infoString;
        MenuInfoFunction infoFunc;
    } target;                   // Nombre de la unión

    // Parámetros adicionales para los ítems
    int minValue;
    int maxValue;
    int stepValue;              // Incremento/decremento para edición de valores INT
    const char* unit;           // Unidad del valor (ej. "C", "min", "hs")
    const char* prefKey;        // Clave para guardar/cargar esta configuración en Preferences
    byte subMenuSize;           // Para SUBMENU: número de ítems que contiene el submenú
};

// =============================================================================
// VARIABLES GLOBALES DE ESTADO Y TEMPORIZADORES
// =============================================================================
// Timers para tareas periódicas
unsigned long lastDHTReadTime = 0;
unsigned long lastLogTime = 0;
unsigned long lastLEDToggleTime = 0;
unsigned long tempMessageStartTime = 0;
unsigned long tempMessageDuration = 0;

// Estado de la conexión WiFi
bool wifiConnected = false;
String currentSSID = "";
String currentIP = "";

// Variables para el control del parpadeo del LED integrado
int ledBlinkState = LOW;
int ledBlinkCount = 0;
const int NORMAL_MODE_BLINK_COUNT = 2;
unsigned long ledBlinkIntervalNormal = 150;
unsigned long ledBlinkPauseNormal = 3000 - (NORMAL_MODE_BLINK_COUNT * 2 * ledBlinkIntervalNormal);
unsigned long ledBlinkIntervalConfig = 1000;

// Timers y estados para la lógica de control de relés programada
unsigned long lastLucesCheck = 0;
unsigned long lastVentCheck = 0;
unsigned long lastExtraccionActivationTime = 0;
unsigned long lastRiegoActivationTime = 0;
bool extraccionActive = false;
unsigned long extraccionStartTime = 0;
bool riegoActive = false;
unsigned long riegoStartTime = 0;

// --- Estados de la Interfaz de Usuario (UI) ---
enum UIState {
    UI_HOME,                // Pantalla principal
    UI_MENU_NAVIGATE,       // Navegando por un menú/submenú
    UI_EDIT_INT_VALUE,      // Editando un valor entero
    UI_EDIT_BOOL_VALUE,     // Editando un valor booleano
    UI_SHOW_INFO,           // Mostrando una pantalla de información
    UI_TEMP_MESSAGE         // Mostrando un mensaje temporal
};
UIState currentUIState = UI_HOME;
UIState previousUIState = UI_HOME; // Para volver después de un mensaje temporal

// --- Variables para la Navegación del Menú ---
const MenuItem* currentActiveMenu = nullptr; // Menú actualmente visible
byte currentActiveMenuSize = 0;        // Número de ítems en el menú activo
byte selectedMenuItemIndex = 0;      // Índice del ítem resaltado en PANTALLA (0 o 1 si LCD es de 2 filas)
byte topMenuItemIndex = 0;           // Índice (del array `currentActiveMenu`) del ítem en la primera fila del LCD

const byte MAX_MENU_DEPTH = 5;         // Máxima profundidad de submenús
struct MenuNavigationState {           // Estructura para guardar el estado de un nivel de menú
    const MenuItem* menuItems;
    byte menuSize;
    byte selectedIndexOnScreen;
    byte topItemIndexInArray;
};
MenuNavigationState menuNavigationStack[MAX_MENU_DEPTH]; // Pila para navegación "Atrás"
byte currentMenuDepth = 0;             // Nivel de profundidad actual en el menú

// Buffers para la edición de valores en el menú
int    editingIntValueBuffer;
bool   editingBoolValueBuffer;
const MenuItem* itemBeingEditedOrShown = nullptr; // Ítem que se está editando o cuya info se muestra
String infoStringToDisplay = "";       // String para mostrar en UI_SHOW_INFO

bool forceLcdUpdate = true;

// --- Estructura para almacenar todas las configuraciones del dispositivo ---
struct Config {
    int thFrecuenciaMuestreo; bool thModoTest;
    int lucesHoraON; int lucesHoraOFF; bool lucesModoTest;
    int ventHoraON; int ventHoraOFF; bool ventModoTest;
    int extrFrecuenciaHoras; int extrDuracionMinutos; bool extrModoTest;
    int riegoFrecuenciaDias; int riegoDuracionMinutos; int riegoHoraDisparo; bool riegoModoTest;
    int registroFrecuenciaHoras; int registroTamMax; bool registroModoTest;
};
Config currentConfig; // Variable global que almacena la configuración actual

// Claves para la persistencia de datos en Preferences
const char* KEY_TH_FREQ = "thFreq"; const char* KEY_TH_TEST = "thTest";
const char* KEY_LUCES_ON = "luzOn"; const char* KEY_LUCES_OFF = "luzOff"; const char* KEY_LUCES_TEST = "luzTest";
const char* KEY_VENT_ON = "ventOn"; const char* KEY_VENT_OFF = "ventOff"; const char* KEY_VENT_TEST = "ventTest";
const char* KEY_EXTR_FREQ = "extrFreq"; const char* KEY_EXTR_DUR = "extrDur"; const char* KEY_EXTR_TEST = "extrTest";
const char* KEY_RIEGO_FREQ = "riegoFreq"; const char* KEY_RIEGO_DUR = "riegoDur"; const char* KEY_RIEGO_HORA = "riegoHora"; const char* KEY_RIEGO_TEST = "riegoTest";
const char* KEY_REG_FREQ = "regFreq"; const char* KEY_REG_MAX = "regMax"; const char* KEY_REG_TEST = "regTest";

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
void checkTestModes();

// --- Interfaz de Usuario (LCD y Encoder) ---
void IRAM_ATTR readEncoderISR();        // ISR para rotación del encoder
void IRAM_ATTR encoderButtonISR();      // ISR para botón del encoder
void processEncoderInput();             // Procesa entradas del encoder en el loop principal
void handleEncoderRotation(long rotation); // Maneja la lógica de rotación
void handleEncoderClick();              // Maneja la lógica de click
void updateDisplay();                   // Actualiza el LCD según el estado actual
void displayHomeScreen();               // Muestra la pantalla principal
void displayMenuScreen();               // Muestra el menú/submenú actual
void displayEditIntScreen();            // Muestra la pantalla de edición de enteros
void displayEditBoolScreen();           // Muestra la pantalla de edición de Sí/No
void displayInfoScreen();               // Muestra una pantalla de información
void displayTemporaryMessage(String line1, String line2, int durationMs); // Muestra mensaje temporal
void resetMenuTimeout();                // Reinicia el temporizador de inactividad del menú
void checkMenuTimeout();                // Verifica si se debe salir del menú por inactividad

void enterMenu();                       // Entra al sistema de menú
void navigateBack();                    // Navega hacia atrás en el menú
void saveEditedValue();                 // Guarda un valor editado

// --- Gestión WiFi y NTP ---
void setupWiFi();
void updateNTP();
String getFormattedTime(bool withSeconds = false);
String getFormattedDateTime();
String getLocalIP();

// --- Gestión de Preferencias ---
void loadDefaultPreferences();
void loadPreferences();
void savePreferences();
void resetWiFiCredentialsAndRestart();

// --- Gestión de Archivos (Log y Web) ---
void initLittleFS();
void logData();
int getLogEntryCount();
void rotateLogIfNeeded();
void serveFile(String path, String contentType, bool download = false);

// --- Servidor Web ---
void setupWebServer();
void handleRoot();
void handleData();
void handleControl();
void handleRiegoManual();
void handleDownloadLog();
void handleLastLogEntries();
void handleNotFound();
// void handleWiFiSaveAndRestart(); // No es necesaria con WiFiManager, él lo maneja

// --- Utilidades ---
void printSerialDebug(String message);
void blinkLED();

// =============================================================================
// DEFINICIÓN DE FUNCIONES DE ACCIÓN E INFORMACIÓN DEL MENÚ
// =============================================================================
// Funciones que se ejecutan al seleccionar ciertos ítems del menú
void action_SalirDelMenu() { currentUIState = UI_HOME; printSerialDebug("Accion: Salir del Menu -> UI_HOME"); forceLcdUpdate = true; }
void action_ConfirmarReinicio() { printSerialDebug("Accion: Reiniciando..."); displayTemporaryMessage("Sistema", "Reiniciando...", 1800); delay(2000); ESP.restart(); }
void action_ConfirmarOlvidoRed() { printSerialDebug("Accion: Olvidando Red..."); displayTemporaryMessage("WiFi", "Borrando Red...", 1800); delay(2000); resetWiFiCredentialsAndRestart(); }
void action_ConfirmarBorrarLog() {
    if (LittleFS.exists(LOG_FILE)) { LittleFS.remove(LOG_FILE); printSerialDebug("Accion: Registro borrado."); displayTemporaryMessage("Registro", "Borrado!", 2000); }
    else { printSerialDebug("Accion: Log no existe."); displayTemporaryMessage("Registro", "Ya vacio", 2000); }
    // El mensaje temporal se encargará de volver al estado anterior (menú)
}
// Funciones que devuelven strings para mostrar en el menú
String info_GetWiFiSSID() { return wifiConnected ? currentSSID : "N/A"; }
String info_GetDeviceIP() { return getLocalIP(); }
String info_GetFirmwareVersion() { return String(FIRMWARE_VERSION); }

// =============================================================================
// ESTRUCTURAS DE MENÚ DETALLADAS (CON INICIALIZADORES DESIGNADOS CORREGIDOS)
// =============================================================================
// Nivel 3: Menús de Confirmación (usados como submenús)
const MenuItem menuReinicio_ConfirmSubMenu[] = {
    {.name = "Si, reiniciar", .type = MENU_ITEM_ACTION, .target = {.actionFunc = action_ConfirmarReinicio}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0},
    {.name = "No, Atras", .type = MENU_ITEM_BACK, .target = {.actionFunc = nullptr}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0}
};
const MenuItem menuOlvidoRed_ConfirmSubMenu[] = {
    {.name = "Si, olvidar", .type = MENU_ITEM_ACTION, .target = {.actionFunc = action_ConfirmarOlvidoRed}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0},
    {.name = "No, Atras", .type = MENU_ITEM_BACK, .target = {.actionFunc = nullptr}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0}
};
const MenuItem menuBorrarLog_ConfirmSubMenu[] = {
    {.name = "Si, borrar", .type = MENU_ITEM_ACTION, .target = {.actionFunc = action_ConfirmarBorrarLog}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0},
    {.name = "No, Atras", .type = MENU_ITEM_BACK, .target = {.actionFunc = nullptr}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0}
};

// Nivel 2: Submenú "Registro" (dentro de "Sistema")
const MenuItem menuSistema_RegistroSubMenu[] = {
    {.name = "Frecuencia", .type = MENU_ITEM_INT_VALUE, .target = {.intValueTarget = &currentConfig.registroFrecuenciaHoras}, .minValue = 0, .maxValue = 24, .stepValue = 1, .unit = "hs", .prefKey = KEY_REG_FREQ, .subMenuSize = 0},
    {.name = "Tamano max.", .type = MENU_ITEM_INT_VALUE, .target = {.intValueTarget = &currentConfig.registroTamMax}, .minValue = 1000, .maxValue = 9999, .stepValue = 100, .unit = "regs", .prefKey = KEY_REG_MAX, .subMenuSize = 0},
    {.name = "Borrar Logs", .type = MENU_ITEM_SUBMENU, .target = {.subMenu = menuBorrarLog_ConfirmSubMenu}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = sizeof(menuBorrarLog_ConfirmSubMenu)/sizeof(MenuItem)},
    {.name = "Modo Test", .type = MENU_ITEM_BOOL_SI_NO, .target = {.boolValueTarget = &currentConfig.registroModoTest}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = KEY_REG_TEST, .subMenuSize = 0},
    {.name = "Atras", .type = MENU_ITEM_BACK, .target = {.actionFunc = nullptr}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0}
};

// Nivel 2: Submenú "Sistema"
const MenuItem menuSistema_SubMenu[] = {
    {.name = "WiFi SSID", .type = MENU_ITEM_INFO_FUNCTION, .target = {.infoFunc = info_GetWiFiSSID}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0},
    {.name = "Direccion IP", .type = MENU_ITEM_INFO_FUNCTION, .target = {.infoFunc = info_GetDeviceIP}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0},
    {.name = "Olvido Red", .type = MENU_ITEM_SUBMENU, .target = {.subMenu = menuOlvidoRed_ConfirmSubMenu}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = sizeof(menuOlvidoRed_ConfirmSubMenu)/sizeof(MenuItem)},
    {.name = "Reiniciar Disp.", .type = MENU_ITEM_SUBMENU, .target = {.subMenu = menuReinicio_ConfirmSubMenu}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = sizeof(menuReinicio_ConfirmSubMenu)/sizeof(MenuItem)},
    {.name = "Firmware Ver.", .type = MENU_ITEM_INFO_FUNCTION, .target = {.infoFunc = info_GetFirmwareVersion}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0},
    {.name = "Registro Datos", .type = MENU_ITEM_SUBMENU, .target = {.subMenu = menuSistema_RegistroSubMenu}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = sizeof(menuSistema_RegistroSubMenu)/sizeof(MenuItem)},
    {.name = "Atras", .type = MENU_ITEM_BACK, .target = {.actionFunc = nullptr}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0}
};

// Nivel 2: Submenú "Riego"
const MenuItem menuRiego_SubMenu[] = {
    {.name = "Frecuencia", .type = MENU_ITEM_INT_VALUE, .target = {.intValueTarget = &currentConfig.riegoFrecuenciaDias}, .minValue = 0, .maxValue = 30, .stepValue = 1, .unit = "dias", .prefKey = KEY_RIEGO_FREQ, .subMenuSize = 0},
    {.name = "Duracion", .type = MENU_ITEM_INT_VALUE, .target = {.intValueTarget = &currentConfig.riegoDuracionMinutos}, .minValue = 1, .maxValue = 30, .stepValue = 1, .unit = "min", .prefKey = KEY_RIEGO_DUR, .subMenuSize = 0},
    {.name = "Hora Disparo", .type = MENU_ITEM_INT_VALUE, .target = {.intValueTarget = &currentConfig.riegoHoraDisparo}, .minValue = 0, .maxValue = 23, .stepValue = 1, .unit = "hs", .prefKey = KEY_RIEGO_HORA, .subMenuSize = 0},
    {.name = "Modo Test", .type = MENU_ITEM_BOOL_SI_NO, .target = {.boolValueTarget = &currentConfig.riegoModoTest}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = KEY_RIEGO_TEST, .subMenuSize = 0},
    {.name = "Atras", .type = MENU_ITEM_BACK, .target = {.actionFunc = nullptr}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0}
};

// Nivel 2: Submenú "Extraccion"
const MenuItem menuExtraccion_SubMenu[] = {
    {.name = "Frecuencia", .type = MENU_ITEM_INT_VALUE, .target = {.intValueTarget = &currentConfig.extrFrecuenciaHoras}, .minValue = 0, .maxValue = 24, .stepValue = 1, .unit = "hs", .prefKey = KEY_EXTR_FREQ, .subMenuSize = 0},
    {.name = "Duracion", .type = MENU_ITEM_INT_VALUE, .target = {.intValueTarget = &currentConfig.extrDuracionMinutos}, .minValue = 1, .maxValue = 60, .stepValue = 1, .unit = "min", .prefKey = KEY_EXTR_DUR, .subMenuSize = 0},
    {.name = "Modo Test", .type = MENU_ITEM_BOOL_SI_NO, .target = {.boolValueTarget = &currentConfig.extrModoTest}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = KEY_EXTR_TEST, .subMenuSize = 0},
    {.name = "Atras", .type = MENU_ITEM_BACK, .target = {.actionFunc = nullptr}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0}
};

// Nivel 2: Submenú "Ventilacion"
const MenuItem menuVentilacion_SubMenu[] = {
    {.name = "Hora ON", .type = MENU_ITEM_INT_VALUE, .target = {.intValueTarget = &currentConfig.ventHoraON}, .minValue = 0, .maxValue = 23, .stepValue = 1, .unit = "hs", .prefKey = KEY_VENT_ON, .subMenuSize = 0},
    {.name = "Hora OFF", .type = MENU_ITEM_INT_VALUE, .target = {.intValueTarget = &currentConfig.ventHoraOFF}, .minValue = 0, .maxValue = 23, .stepValue = 1, .unit = "hs", .prefKey = KEY_VENT_OFF, .subMenuSize = 0},
    {.name = "Modo Test", .type = MENU_ITEM_BOOL_SI_NO, .target = {.boolValueTarget = &currentConfig.ventModoTest}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = KEY_VENT_TEST, .subMenuSize = 0},
    {.name = "Atras", .type = MENU_ITEM_BACK, .target = {.actionFunc = nullptr}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0}
};

// Nivel 2: Submenú "Luces"
const MenuItem menuLuces_SubMenu[] = {
    {.name = "Hora ON", .type = MENU_ITEM_INT_VALUE, .target = {.intValueTarget = &currentConfig.lucesHoraON}, .minValue = 0, .maxValue = 23, .stepValue = 1, .unit = "hs", .prefKey = KEY_LUCES_ON, .subMenuSize = 0},
    {.name = "Hora OFF", .type = MENU_ITEM_INT_VALUE, .target = {.intValueTarget = &currentConfig.lucesHoraOFF}, .minValue = 0, .maxValue = 23, .stepValue = 1, .unit = "hs", .prefKey = KEY_LUCES_OFF, .subMenuSize = 0},
    {.name = "Modo Test", .type = MENU_ITEM_BOOL_SI_NO, .target = {.boolValueTarget = &currentConfig.lucesModoTest}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = KEY_LUCES_TEST, .subMenuSize = 0},
    {.name = "Atras", .type = MENU_ITEM_BACK, .target = {.actionFunc = nullptr}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0}
};

// Nivel 2: Submenú "Temp. y Humedad"
const MenuItem menuTempHum_SubMenu[] = {
    {.name = "Frec. Muestra", .type = MENU_ITEM_INT_VALUE, .target = {.intValueTarget = &currentConfig.thFrecuenciaMuestreo}, .minValue = 1, .maxValue = 60, .stepValue = 1, .unit = "min", .prefKey = KEY_TH_FREQ, .subMenuSize = 0},
    {.name = "Modo Test", .type = MENU_ITEM_BOOL_SI_NO, .target = {.boolValueTarget = &currentConfig.thModoTest}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = KEY_TH_TEST, .subMenuSize = 0},
    {.name = "Atras", .type = MENU_ITEM_BACK, .target = {.actionFunc = nullptr}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0}
};

// Nivel 1: Menú Principal
const MenuItem menuPrincipal_MainMenu[] = {
    {.name = "Temp/Humedad", .type = MENU_ITEM_SUBMENU, .target = {.subMenu = menuTempHum_SubMenu}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = sizeof(menuTempHum_SubMenu)/sizeof(MenuItem)},
    {.name = "Luces", .type = MENU_ITEM_SUBMENU, .target = {.subMenu = menuLuces_SubMenu}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = sizeof(menuLuces_SubMenu)/sizeof(MenuItem)},
    {.name = "Ventilacion", .type = MENU_ITEM_SUBMENU, .target = {.subMenu = menuVentilacion_SubMenu}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = sizeof(menuVentilacion_SubMenu)/sizeof(MenuItem)},
    {.name = "Extraccion", .type = MENU_ITEM_SUBMENU, .target = {.subMenu = menuExtraccion_SubMenu}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = sizeof(menuExtraccion_SubMenu)/sizeof(MenuItem)},
    {.name = "Riego", .type = MENU_ITEM_SUBMENU, .target = {.subMenu = menuRiego_SubMenu}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = sizeof(menuRiego_SubMenu)/sizeof(MenuItem)},
    {.name = "Sistema", .type = MENU_ITEM_SUBMENU, .target = {.subMenu = menuSistema_SubMenu}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = sizeof(menuSistema_SubMenu)/sizeof(MenuItem)},
    {.name = "Salir del Menu", .type = MENU_ITEM_ACTION, .target = {.actionFunc = action_SalirDelMenu}, .minValue = 0, .maxValue = 0, .stepValue = 0, .unit = nullptr, .prefKey = nullptr, .subMenuSize = 0}
};


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
    // CORREGIDO: Usar las ISRs implementadas
    attachInterrupt(digitalPinToInterrupt(ENCODER_CLK_PIN), readEncoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_DT_PIN), readEncoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_SW_PIN), encoderButtonISR, FALLING); // Pulsador activo bajo

    // Inicializar LCD
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Armina Grow");
    lcd.setCursor(0, 1);
    lcd.print("Iniciando...");
    delay(1500);

    // Inicializar LittleFS
    initLittleFS();
    // Cargar Preferencias
    preferences.begin(PREFERENCES_NAMESPACE, false);
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
    setupWiFi();

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
    } else {
        printSerialDebug("Modo Configuracion (AP). SSID: " + String(AP_SSID_PREFIX));
    }
    
    currentUIState = UI_HOME;
    forceLcdUpdate = true; // Forzar el primer dibujo del LCD
    displayHomeScreen(); // Se llama desde updateDisplay() en el primer loop si forceLcdUpdate es true
    lastEncoderActivityTime = millis(); // Iniciar timeout del menú
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
        if (wifiManager.getConfigPortalActive()){
             // WiFiManager maneja su propio loop para el portal
        }
    }

    processEncoderInput(); // Procesa entradas del encoder y actualiza flags/estados
    
    if (forceLcdUpdate) { // Llama a updateDisplay solo si es necesario
        updateDisplay();    // Actualiza el LCD según el estado actual de la UI
    }
    
    checkMenuTimeout(); // Volver a HOME si hay inactividad en el menú

    // Tareas periódicas
    unsigned long currentTime = millis();

    // Leer sensores
    unsigned long thInterval = (currentConfig.thModoTest ? 5 : currentConfig.thFrecuenciaMuestreo * 60) * 1000UL;
    if (thInterval > 0 && (currentTime - lastDHTReadTime >= thInterval || lastDHTReadTime == 0)) {
        readDHTSensor();
        lastDHTReadTime = currentTime;
    }

    // Controlar relés según configuración
    if (wifiConnected && timeClient.isTimeSet()) {
         applyScheduledControls();
    }
    checkTestModes(); // Lógica de parpadeo para modos test activados desde el menú

    // Registrar datos
    unsigned long logInterval = (currentConfig.registroModoTest ? 10 : currentConfig.registroFrecuenciaHoras * 3600) * 1000UL;
    if (currentConfig.registroFrecuenciaHoras > 0 && wifiConnected && timeClient.isTimeSet() && (currentTime - lastLogTime >= logInterval || lastLogTime == 0)) {
        logData();
        lastLogTime = currentTime;
    }
    
    // Parpadeo del LED integrado
    blinkLED();

    // Verificar estado de conexión WiFi (si no estamos en modo AP por WiFiManager)
    if (!wifiManager.getConfigPortalActive()) {
        bool prevWifiConnected = wifiConnected;
        wifiConnected = (WiFi.status() == WL_CONNECTED);
        if (prevWifiConnected && !wifiConnected) {
            printSerialDebug("Conexion WiFi perdida.");
            currentSSID = "";
            currentIP = "";
            forceLcdUpdate = true; // Forzar actualización para mostrar Offline
        } else if (!prevWifiConnected && wifiConnected) {
            printSerialDebug("Conexion WiFi restablecida/establecida.");
            currentSSID = WiFi.SSID();
            currentIP = WiFi.localIP().toString();
            timeClient.begin(); // Reiniciar NTP si es necesario
            updateNTP();
            forceLcdUpdate = true; // Forzar actualización para mostrar Online
        }
    }
}

// =============================================================================
// IMPLEMENTACIÓN DE FUNCIONES DE SENSORES Y ACTUADORES
// =============================================================================
void readDHTSensor() {
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {
        printSerialDebug("Error al leer DHT11/22");
        // Opcional: mantener valores anteriores o marcarlos explícitamente como inválidos si es necesario
    } else {
        currentTemperature = t;
        currentHumidity = h;
        printSerialDebug("Temp: " + String(t) + "C, Hum: " + String(h) + "%");
    }
    if (currentUIState == UI_HOME) {
        forceLcdUpdate = true; // Indicar que la pantalla HOME necesita redibujarse
    }
}

void controlRelay(int relayIndex, bool state, bool manualOverride) {
    if (relayIndex < 0 || relayIndex >= NUM_RELAYS) return;
    // Cambiar y notificar solo si el estado es diferente o es un override manual
    if (relayStates[relayIndex] != state || manualOverride) {
        relayStates[relayIndex] = state;
        digitalWrite(relayPins[relayIndex], state ? HIGH : LOW); // Asumir relés activos en ALTO
        printSerialDebug(String(relayNames[relayIndex]) + (state ? " ENCENDIDO" : " APAGADO"));
        
        // CORREGIDO: Usar los nuevos estados de UI para evitar mostrar mensaje durante edición
        if (currentUIState != UI_TEMP_MESSAGE &&
            currentUIState != UI_EDIT_INT_VALUE &&
            currentUIState != UI_EDIT_BOOL_VALUE &&
            currentUIState != UI_SHOW_INFO) {
            displayTemporaryMessage(relayNames[relayIndex], (state ? "Encendido" : "Apagado"), 2000);
        }
    }
}

void applyScheduledControls() {
    if (!timeClient.isTimeSet()) {
        return; // No aplicar controles si no hay hora sincronizada
    }
    applyLucesControl();
    applyVentilacionControl();
    applyExtraccionControl();
    applyRiegoControl();
}

void applyLucesControl() {
    if (currentConfig.lucesModoTest) return; // Si está en modo test, la lógica programada no aplica

    int currentHour = timeClient.getHours();
    bool shouldBeOn = false;
    if (currentConfig.lucesHoraON <= currentConfig.lucesHoraOFF) { // Ciclo dentro del mismo día
        if (currentHour >= currentConfig.lucesHoraON && currentHour < currentConfig.lucesHoraOFF) {
            shouldBeOn = true;
        }
    } else { // Ciclo que cruza la medianoche
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
    if (currentConfig.extrFrecuenciaHoras == 0) { // 0 = Extracción desactivada
        controlRelay(2, false);
        extraccionActive = false;
        return;
    }

    unsigned long currentTime = millis();
    if (extraccionActive) { // Si ya está en un ciclo de extracción
        // Verificar si el tiempo de duración ha pasado
        if (currentTime - extraccionStartTime >= (unsigned long)currentConfig.extrDuracionMinutos * 60 * 1000UL) {
            controlRelay(2, false); // Apagar extracción
            extraccionActive = false;
            printSerialDebug("Extraccion: Ciclo completado.");
        }
    } else { // Si no está activa, verificar si es hora de activar según frecuencia
        if (currentTime - lastExtraccionActivationTime >= (unsigned long)currentConfig.extrFrecuenciaHoras * 3600 * 1000UL) {
            controlRelay(2, true); // Encender extracción
            extraccionActive = true;
            extraccionStartTime = currentTime;
            lastExtraccionActivationTime = currentTime; // Actualizar para el próximo ciclo
            printSerialDebug("Extraccion: Iniciando ciclo.");
        }
    }
}

void applyRiegoControl() {
    if (currentConfig.riegoModoTest) return;
    if (currentConfig.riegoFrecuenciaDias == 0) { // 0 = Riego desactivado
        controlRelay(3, false);
        riegoActive = false;
        return;
    }

    unsigned long currentTime = millis();
    int currentHour = timeClient.getHours();
    
    // Esta lógica de frecuencia en días necesita ser robusta. Guardar la fecha del último riego sería ideal.
    // Por ahora, es una simplificación basada en el tiempo transcurrido.
    if (riegoActive) { // Si ya está en un ciclo de riego
        if (currentTime - riegoStartTime >= (unsigned long)currentConfig.riegoDuracionMinutos * 60 * 1000UL) {
            controlRelay(3, false); // Apagar riego
            riegoActive = false;
            printSerialDebug("Riego: Ciclo completado.");
        }
    } else { // Si no está activo, verificar si es hora de regar
        unsigned long freqMillis = (unsigned long)currentConfig.riegoFrecuenciaDias * 24 * 3600 * 1000UL;
        static int lastRiegoControlDay = -1; // Para evitar múltiples activaciones en la misma hora del mismo día

        if (currentHour == currentConfig.riegoHoraDisparo && 
            (currentTime - lastRiegoActivationTime >= freqMillis || lastRiegoActivationTime == 0 /*para el primer riego*/)) {
             
             // Comprobar si ya se regó hoy para esta hora de disparo (simplificado)
             // Lo ideal sería comparar fechas completas (YYYY-MM-DD) del último riego.
             if (timeClient.getDay() != lastRiegoControlDay || freqMillis == 0) { 
                 controlRelay(3, true); // Encender riego
                 riegoActive = true;
                 riegoStartTime = currentTime;
                 lastRiegoActivationTime = currentTime; // Actualizar para la próxima frecuencia
                 lastRiegoControlDay = timeClient.getDay(); // Marcar que hoy ya se activó a esta hora
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
                testBlinkState[i] = true; // Marcar para encender
                controlRelay(i, true, true); // Forzar encendido
                lastTestBlinkTime[i] = currentTime; // Resetear timer del ciclo de 5s
                // printSerialDebug(String(relayNames[i]) + " Modo Test: ON (1s)");
            } else if (testBlinkState[i] && (currentTime - lastTestBlinkTime[i] >= 1000)) { // Si está encendido por test y pasó 1 segundo
                testBlinkState[i] = false; // Marcar para apagar
                controlRelay(i, false, true); // Forzar apagado
                // No se resetea lastTestBlinkTime[i] aquí, para que se complete el ciclo de 5s antes del próximo ON
            }
        } else { // Si el modo test NO está activo
            if (testBlinkState[i]) { // Si justo se desactivó el modo test y el relé quedó encendido por test
                 testBlinkState[i] = false; // Resetear el estado de parpadeo del test
                 // La lógica de applyScheduledControls se encargará de poner el estado correcto.
                 // Forzar una reevaluación podría ser útil si el cambio de modo test no coincide con un ciclo de control.
                 forceLcdUpdate = true; 
            }
        }
    }
}

// =============================================================================
// IMPLEMENTACIÓN DE FUNCIONES DE UI, ENCODER Y MENÚ
// =============================================================================
// Variables para el debounce del botón del encoder en el loop principal
long debouncedEncoderSWValue = HIGH; // Estado actual debounced del botón (HIGH = no presionado)
unsigned long lastEncoderSWTime = 0;   // Última vez que el botón cambió de estado (para debounce)
const unsigned long ENCODER_SW_DEBOUNCE_MS = 50; // Tiempo de debounce para el botón

// ISR para el encoder (CLK y DT) - Actualiza `encoderPos`
void IRAM_ATTR readEncoderISR() {
    byte clkS = digitalRead(ENCODER_CLK_PIN);
    byte dtS = digitalRead(ENCODER_DT_PIN);
    static byte oldAB = 0; // Almacena el estado anterior de CLK y DT combinados
    byte newAB = (clkS << 1) | dtS; // Combina los dos bits en un byte (CLK en bit 1, DT en bit 0)

    if (newAB != oldAB) { // Solo procesar si hay un cambio de estado
        // Decodificación de encoder de cuadratura estándar (simplificada)
        // Compara la transición de oldAB a newAB para determinar dirección
        if (oldAB == 0b00) { if (newAB == 0b01) encoderPos++; else if (newAB == 0b10) encoderPos--; }
        else if (oldAB == 0b01) { if (newAB == 0b11) encoderPos++; else if (newAB == 0b00) encoderPos--; }
        else if (oldAB == 0b11) { if (newAB == 0b10) encoderPos++; else if (newAB == 0b01) encoderPos--; }
        else if (oldAB == 0b10) { if (newAB == 0b00) encoderPos++; else if (newAB == 0b11) encoderPos--; }
        oldAB = newAB; // Actualizar el estado anterior
    }
}

// ISR para el botón del encoder - Solo establece un flag
void IRAM_ATTR encoderButtonISR() {
    encoderSWStatusChanged = true; // Indica que el botón fue presionado (o liberado)
}

// Procesa las entradas del encoder (rotación y botón) en el loop principal
void processEncoderInput() {
    // Debounce y manejo del botón del encoder
    if (encoderSWStatusChanged) {
        unsigned long currentTime = millis();
        byte currentSWState = digitalRead(ENCODER_SW_PIN); // Leer estado actual del botón
        if (currentSWState == LOW && debouncedEncoderSWValue == HIGH && (currentTime - lastEncoderSWTime > ENCODER_SW_DEBOUNCE_MS)) {
            // Botón presionado (flanco descendente, debounced)
            printSerialDebug("Encoder SW Presionado (Debounced)");
            handleEncoderClick(); // Procesar la acción del click
            resetMenuTimeout();   // Reiniciar timeout de inactividad del menú
            lastEncoderSWTime = currentTime; // Actualizar tiempo para el próximo debounce
        }
        debouncedEncoderSWValue = currentSWState; // Guardar estado actual para la lógica de debounce
        encoderSWStatusChanged = false; // Flag procesado
    }

    // Procesar rotación del encoder
    if (encoderPos != lastReportedEncoderPos) {
        long rotation = encoderPos - lastReportedEncoderPos; // Calcular cuántos "pasos" giró
        lastReportedEncoderPos = encoderPos; // Actualizar la última posición reportada
        printSerialDebug("Encoder Rotado: " + String(rotation > 0 ? "CW" : "CCW") + " (pos: " + String(encoderPos) + ")");
        handleEncoderRotation(rotation); // Procesar la acción de rotación
        resetMenuTimeout();              // Reiniciar timeout de inactividad
        forceLcdUpdate = true;           // Marcar para redibujar el LCD ya que la selección/valor cambió
    }
}

// Inicializa y entra al sistema de menú
void enterMenu() {
    currentUIState = UI_MENU_NAVIGATE; // Establecer estado de navegación de menú
    currentMenuDepth = 0;              // Nivel 0 (menú principal)
    // Cargar el menú principal en la pila de navegación
    menuNavigationStack[0] = {menuPrincipal_MainMenu, (byte)(sizeof(menuPrincipal_MainMenu) / sizeof(MenuItem)), 0, 0};
    currentActiveMenu = menuNavigationStack[0].menuItems;       // Menú actual
    currentActiveMenuSize = menuNavigationStack[0].menuSize;    // Tamaño del menú actual
    selectedMenuItemIndex = menuNavigationStack[0].selectedIndexOnScreen; // Índice seleccionado en pantalla (0 o 1)
    topMenuItemIndex = menuNavigationStack[0].topItemIndexInArray;      // Índice del array en la línea superior
    printSerialDebug("Entrando al Menu Principal.");
    forceLcdUpdate = true; // Forzar redibujo del LCD
}

// Navega hacia atrás en la jerarquía del menú
void navigateBack() {
    if (currentMenuDepth > 0) { // Si estamos en un submenú
        currentMenuDepth--;      // Decrementar profundidad
        // Restaurar estado del menú desde la pila
        currentActiveMenu = menuNavigationStack[currentMenuDepth].menuItems;
        currentActiveMenuSize = menuNavigationStack[currentMenuDepth].menuSize;
        selectedMenuItemIndex = menuNavigationStack[currentMenuDepth].selectedIndexOnScreen;
        topMenuItemIndex = menuNavigationStack[currentMenuDepth].topItemIndexInArray;
        currentUIState = UI_MENU_NAVIGATE; // Volver al estado de navegación
        printSerialDebug("Menu: Atras a nivel " + String(currentMenuDepth));
    } else { // Si estábamos en el menú principal, "Atrás" significa salir
        currentUIState = UI_HOME;
        printSerialDebug("Menu: Atras desde Menu Principal -> UI_HOME");
    }
    forceLcdUpdate = true; // Forzar redibujo
}

// Guarda el valor que se estaba editando
void saveEditedValue() {
    if (!itemBeingEditedOrShown) return; // Seguridad: no hacer nada si no hay ítem en edición

    printSerialDebug("Guardando valor para: " + String(itemBeingEditedOrShown->name));
    bool preferenceChanged = false; // Flag para saber si se debe guardar en Preferences

    // Actualizar la variable de configuración (`currentConfig`) según el tipo de ítem
    if (itemBeingEditedOrShown->type == MENU_ITEM_INT_VALUE) {
        if (*itemBeingEditedOrShown->target.intValueTarget != editingIntValueBuffer) {
            *itemBeingEditedOrShown->target.intValueTarget = editingIntValueBuffer; // Actualizar variable en RAM
            preferenceChanged = true;
        }
        printSerialDebug("Valor INT guardado: " + String(editingIntValueBuffer));
    } else if (itemBeingEditedOrShown->type == MENU_ITEM_BOOL_SI_NO) {
        if (*itemBeingEditedOrShown->target.boolValueTarget != editingBoolValueBuffer) {
            *itemBeingEditedOrShown->target.boolValueTarget = editingBoolValueBuffer; // Actualizar variable en RAM
            preferenceChanged = true;
        }
        printSerialDebug("Valor BOOL guardado: " + String(editingBoolValueBuffer ? "Si" : "No"));
    }

    // Si el valor cambió y el ítem tiene una clave de Preferences asociada, guardar en NVS
    if (preferenceChanged && itemBeingEditedOrShown->prefKey != nullptr) {
        // preferences.begin(PREFERENCES_NAMESPACE, false); // Asegurarse que Preferences está abierto
        if (itemBeingEditedOrShown->type == MENU_ITEM_INT_VALUE) {
            preferences.putInt(itemBeingEditedOrShown->prefKey, editingIntValueBuffer);
        } else if (itemBeingEditedOrShown->type == MENU_ITEM_BOOL_SI_NO) {
            preferences.putBool(itemBeingEditedOrShown->prefKey, editingBoolValueBuffer);
        }
        // preferences.end(); // Opcional: cerrar Preferences para asegurar guardado.
                           // Si se hacen muchas escrituras, es mejor mantenerlo abierto y cerrarlo al final.
                           // putX() generalmente escribe al NVS.
        printSerialDebug("Preferencia guardada para key: " + String(itemBeingEditedOrShown->prefKey));
    }
    
    itemBeingEditedOrShown = nullptr; // Limpiar puntero al ítem editado
    currentUIState = UI_MENU_NAVIGATE; // Volver al estado de navegación del menú
    forceLcdUpdate = true; // Forzar redibujo del menú
}

// Maneja la lógica de rotación del encoder
void handleEncoderRotation(long rotation) {
    if (currentUIState == UI_HOME) {
        enterMenu(); // Si estamos en HOME, cualquier rotación entra al menú
    } else if (currentUIState == UI_MENU_NAVIGATE) { // Navegando un menú
        if (rotation > 0) { // Giro horario (mover selección hacia abajo en pantalla)
            // Si la selección actual no es la última línea Y no es el último ítem visible
            if (selectedMenuItemIndex < min((byte)(LCD_ROWS - 1), (byte)(currentActiveMenuSize - topMenuItemIndex - 1))) {
                selectedMenuItemIndex++;
            } else if (topMenuItemIndex + LCD_ROWS < currentActiveMenuSize) { // Si hay más ítems abajo para hacer scroll
                topMenuItemIndex++; // Hacer scroll del menú hacia abajo
            }
        } else { // Giro antihorario (mover selección hacia arriba en pantalla)
            if (selectedMenuItemIndex > 0) { // Si no estamos en la primera línea de la selección
                selectedMenuItemIndex--;
            } else if (topMenuItemIndex > 0) { // Si no estamos al inicio del menú (se puede scrollear arriba)
                topMenuItemIndex--; // Hacer scroll del menú hacia arriba
            }
        }
        // Guardar la posición actual de navegación en la pila (para "Atrás")
        menuNavigationStack[currentMenuDepth].selectedIndexOnScreen = selectedMenuItemIndex;
        menuNavigationStack[currentMenuDepth].topItemIndexInArray = topMenuItemIndex;
    } else if (currentUIState == UI_EDIT_INT_VALUE && itemBeingEditedOrShown) { // Editando un valor entero
        int step = itemBeingEditedOrShown->stepValue != 0 ? itemBeingEditedOrShown->stepValue : 1;
        editingIntValueBuffer += (rotation > 0 ? step : -step); // Incrementar/decrementar buffer
        // Aplicar límites min/max
        if (editingIntValueBuffer < itemBeingEditedOrShown->minValue) editingIntValueBuffer = itemBeingEditedOrShown->minValue;
        if (editingIntValueBuffer > itemBeingEditedOrShown->maxValue) editingIntValueBuffer = itemBeingEditedOrShown->maxValue;
    } else if (currentUIState == UI_EDIT_BOOL_VALUE && itemBeingEditedOrShown) { // Editando un valor Sí/No
        editingBoolValueBuffer = !editingBoolValueBuffer; // Cambiar entre true/false con cualquier rotación
    } else if (currentUIState == UI_SHOW_INFO) { // Si se está mostrando información
        currentUIState = UI_MENU_NAVIGATE; // Cualquier rotación vuelve al menú
        itemBeingEditedOrShown = nullptr;  // Limpiar
    }
    forceLcdUpdate = true; // Marcar para redibujar el LCD
}

// Maneja la lógica de click del botón del encoder
void handleEncoderClick() {
    if (currentUIState == UI_HOME) {
        enterMenu(); // Si estamos en HOME, un click entra al menú
        forceLcdUpdate = true;
        return;
    }
    resetMenuTimeout(); // Reiniciar timeout en cualquier click dentro del menú

    if (currentUIState == UI_MENU_NAVIGATE) { // Si estamos navegando un menú
        // Obtener el ítem realmente seleccionado (considerando el scroll)
        const MenuItem* selectedItem = &currentActiveMenu[topMenuItemIndex + selectedMenuItemIndex];
        itemBeingEditedOrShown = selectedItem; // Guardar referencia para edición o mostrar info
        printSerialDebug("Menu Click: " + String(selectedItem->name));

        switch (selectedItem->type) {
            case MENU_ITEM_SUBMENU: // Si es un submenú
                if (currentMenuDepth < MAX_MENU_DEPTH - 1) { // Evitar desbordamiento de la pila
                    // Guardar estado actual del menú en la pila
                    menuNavigationStack[currentMenuDepth].selectedIndexOnScreen = selectedMenuItemIndex;
                    menuNavigationStack[currentMenuDepth].topItemIndexInArray = topMenuItemIndex;
                    currentMenuDepth++; // Incrementar profundidad
                    // Cargar el nuevo submenú en la pila y como activo
                    menuNavigationStack[currentMenuDepth] = {selectedItem->target.subMenu, selectedItem->subMenuSize, 0, 0};
                    currentActiveMenu = menuNavigationStack[currentMenuDepth].menuItems;
                    currentActiveMenuSize = menuNavigationStack[currentMenuDepth].menuSize;
                    selectedMenuItemIndex = 0; // Resetear selección para el nuevo menú
                    topMenuItemIndex = 0;      // Resetear scroll para el nuevo menú
                    printSerialDebug("Entrando a Submenu: " + String(selectedItem->name));
                } else { printSerialDebug("Error: Maxima profundidad de menu alcanzada."); }
                break;
            case MENU_ITEM_INT_VALUE: // Si es un valor entero editable
                editingIntValueBuffer = *selectedItem->target.intValueTarget; // Cargar valor actual al buffer
                currentUIState = UI_EDIT_INT_VALUE; // Cambiar a estado de edición
                printSerialDebug("Editando INT: " + String(selectedItem->name));
                break;
            case MENU_ITEM_BOOL_SI_NO: // Si es un valor booleano editable
                editingBoolValueBuffer = *selectedItem->target.boolValueTarget; // Cargar valor actual al buffer
                currentUIState = UI_EDIT_BOOL_VALUE; // Cambiar a estado de edición
                printSerialDebug("Editando BOOL: " + String(selectedItem->name));
                break;
            case MENU_ITEM_ACTION: // Si es una acción
                if (selectedItem->target.actionFunc) {
                    selectedItem->target.actionFunc(); // Ejecutar la función de acción
                }
                // El estado de la UI puede cambiar dentro de la función de acción (ej. Salir)
                break;
            case MENU_ITEM_BACK: // Si es "Atrás"
                navigateBack();
                break;
            case MENU_ITEM_INFO_STRING: // Si es información de texto
                infoStringToDisplay = selectedItem->target.infoString; // Preparar string
                currentUIState = UI_SHOW_INFO; // Cambiar a estado de mostrar info
                break;
            case MENU_ITEM_INFO_FUNCTION: // Si es información de función
                if (selectedItem->target.infoFunc) {
                    infoStringToDisplay = selectedItem->target.infoFunc(); // Llamar función y preparar string
                } else {
                    infoStringToDisplay = "Error func";
                }
                currentUIState = UI_SHOW_INFO; // Cambiar a estado de mostrar info
                break;
        }
    } else if (currentUIState == UI_EDIT_INT_VALUE || currentUIState == UI_EDIT_BOOL_VALUE) { // Si estábamos editando y se hizo click
        saveEditedValue(); // Guardar el valor y volver al menú de navegación
    } else if (currentUIState == UI_SHOW_INFO) { // Si se estaba mostrando información y se hizo click
        currentUIState = UI_MENU_NAVIGATE; // Volver al menú de navegación
        itemBeingEditedOrShown = nullptr;  // Limpiar puntero
    }
    forceLcdUpdate = true; // Marcar para redibujar el LCD
}

// --- Funciones de Visualización del LCD ---
// bool forceLcdUpdate = true; // Declarada globalmente

// Actualiza el display LCD según el estado actual de la UI
void updateDisplay() {
    if (currentUIState == UI_TEMP_MESSAGE) {
        if (millis() - tempMessageStartTime >= tempMessageDuration) { // Si el mensaje temporal ya expiró
            currentUIState = previousUIState; // Volver al estado anterior
            forceLcdUpdate = true; // Forzar redibujo de la pantalla previa
        } else {
            return; // Mensaje temporal aún activo, no hacer nada más
        }
    }
    
    // Optimización: Solo redibujar si 'forceLcdUpdate' es true.
    // Este flag se activa cuando cambia un estado o se recibe nueva data que debe mostrarse.
    if (!forceLcdUpdate) return;

    // Llama a la función de display correspondiente al estado actual
    switch (currentUIState) {
        case UI_HOME:             displayHomeScreen();        break;
        case UI_MENU_NAVIGATE:    displayMenuScreen();        break;
        case UI_EDIT_INT_VALUE:   displayEditIntScreen();     break;
        case UI_EDIT_BOOL_VALUE:  displayEditBoolScreen();    break;
        case UI_SHOW_INFO:        displayInfoScreen();        break;
        default:                  displayHomeScreen();        break; // Pantalla por defecto
    }
    forceLcdUpdate = false; // Resetea el flag después de redibujar
}

// Muestra la pantalla principal (Home)
void displayHomeScreen() {
    lcd.clear();
    String tempStr = (currentTemperature == -99.0) ? "--.-" : String(currentTemperature, 1);
    String humStr = (currentHumidity == -99.0) ? "--.-" : String(currentHumidity, 1);
    lcd.setCursor(0, 0); lcd.print("T:" + tempStr + "C H:" + humStr + "%");
    
    lcd.setCursor(0, 1);
    if (wifiConnected && timeClient.isTimeSet()) { lcd.print("Online  " + getFormattedTime()); }
    else if (wifiConnected && !timeClient.isTimeSet()) { lcd.print("Online  --:--"); } // Conectado pero sin hora NTP
    else if (!wifiConnected && !wifiManager.getConfigPortalActive()) { lcd.print("Offline --:--"); } // Desconectado y no en portal
    else if (wifiManager.getConfigPortalActive()) { // En modo Portal de Configuración WiFiManager
        lcd.clear(); 
        lcd.setCursor(0,0); 
        String portalSsid = wifiManager.getConfigPortalSSID();
        String linea1Display = ""; // Mantener consistencia con el callback
        int espacioParaSsid = LCD_COLS - linea1Display.length();
        if (portalSsid.length() > espacioParaSsid) {
            linea1Display += portalSsid.substring(0, espacioParaSsid);
        } else {
            linea1Display += portalSsid;
        }
        lcd.print(linea1Display);
        
        lcd.setCursor(0,1);
        String linea2Display = "" + WiFi.softAPIP().toString();
        if (linea2Display.length() > LCD_COLS) {
            linea2Display = linea2Display.substring(0, LCD_COLS);
        }
        lcd.print(linea2Display);
    } else { lcd.print("Modo AP"); } // Otro caso de AP (no debería ocurrir con WiFiManager)
}

// Muestra el menú/submenú actual en el LCD
void displayMenuScreen() {
    lcd.clear();
    if (!currentActiveMenu || currentActiveMenuSize == 0) {
        lcd.print("Menu Vacio!"); return;
    }

    // Muestra hasta LCD_ROWS ítems, empezando desde topMenuItemIndex
    for (byte i = 0; i < LCD_ROWS; i++) {
        byte itemIndexInArray = topMenuItemIndex + i; // Índice real en el array del menú
        if (itemIndexInArray < currentActiveMenuSize) { // Si el ítem existe
            lcd.setCursor(0, i); // Posicionar cursor en la fila 'i'
            if (i == selectedMenuItemIndex) { // Si es el ítem actualmente seleccionado EN PANTALLA
                lcd.print(">"); // Indicador de selección
            } else {
                lcd.print(" "); // Espacio para alinear
            }
            // Truncar nombre del ítem si es muy largo para el LCD
            String itemName = currentActiveMenu[itemIndexInArray].name;
            if (itemName.length() > (LCD_COLS - 1)) { // -1 por el cursor '>' o espacio
                itemName = itemName.substring(0, LCD_COLS - 1);
            }
            lcd.print(itemName);
        }
    }
}

// Muestra la pantalla de edición para un valor entero
void displayEditIntScreen() {
    lcd.clear();
    if (!itemBeingEditedOrShown) { lcd.print("Error Edicion"); return; }
    
    lcd.setCursor(0, 0);
    String itemName = itemBeingEditedOrShown->name;
    if (itemName.length() > LCD_COLS) itemName = itemName.substring(0, LCD_COLS); // Truncar
    lcd.print(itemName);

    String valStr = String(editingIntValueBuffer); // Valor actual del buffer de edición
    if (itemBeingEditedOrShown->unit) valStr += " " + String(itemBeingEditedOrShown->unit); // Añadir unidad si existe
    
    lcd.setCursor(0, 1);
    String fullEditStr = "<" + valStr + ">"; // Formato <VALOR>
    // Truncar si es muy largo, intentando mantener los <>
    if (fullEditStr.length() > LCD_COLS) {
        int valActualLen = valStr.length();
        int maxInternalLen = LCD_COLS - 2; // Espacio para <>
        if (valActualLen > maxInternalLen) {
            valStr = valStr.substring(0, maxInternalLen - 2) + ".."; // Truncar y añadir ".."
        }
        fullEditStr = "<" + valStr + ">";
    }
    // Centrar el string en el LCD
    int len = fullEditStr.length();
    int pad = (LCD_COLS - len) / 2;
    for(int p=0; p<pad; ++p) lcd.print(" ");
    lcd.print(fullEditStr);
}

// Muestra la pantalla de edición para un valor booleano (Sí/No)
void displayEditBoolScreen() {
    lcd.clear();
    if (!itemBeingEditedOrShown) { lcd.print("Error Edicion"); return; }

    lcd.setCursor(0, 0);
    String itemName = itemBeingEditedOrShown->name;
    if (itemName.length() > LCD_COLS) itemName = itemName.substring(0, LCD_COLS); // Truncar
    lcd.print(itemName);

    String valStr = editingBoolValueBuffer ? "Si" : "No"; // Valor actual del buffer
    
    lcd.setCursor(0, 1);
    String fullEditStr = "<" + valStr + ">"; // Formato <VALOR>
    // Centrar
    int len = fullEditStr.length();
    int pad = (LCD_COLS - len) / 2;
    for(int p=0; p<pad; ++p) lcd.print(" ");
    lcd.print(fullEditStr);
}

// Muestra una pantalla de información
void displayInfoScreen() {
    lcd.clear();
    if (!itemBeingEditedOrShown) { lcd.print("Error Info"); return; }

    lcd.setCursor(0,0);
    String itemName = itemBeingEditedOrShown->name;
    // Truncar nombre del ítem si es muy largo para la primera línea
    if (itemName.length() > LCD_COLS) itemName = itemName.substring(0, LCD_COLS);
    lcd.print(itemName);
    
    lcd.setCursor(0,1);
    // Truncar la cadena de información si es muy larga para la segunda línea
    if (infoStringToDisplay.length() > LCD_COLS) {
        lcd.print(infoStringToDisplay.substring(0, LCD_COLS));
    } else {
        lcd.print(infoStringToDisplay);
    }
    // Esta pantalla se mantiene visible hasta el próximo click del encoder o timeout del menú
}

// Muestra un mensaje temporal en el LCD
void displayTemporaryMessage(String line1, String line2, int durationMs) {
    // Evitar interrumpir un mensaje temporal si ya hay uno importante (o el mismo)
    if (currentUIState == UI_TEMP_MESSAGE && millis() < tempMessageStartTime + tempMessageDuration) { 
        // Podríamos tener una cola de mensajes o prioridades si fuera necesario
        return; 
    }
    previousUIState = currentUIState; // Guardar estado actual para volver
    currentUIState = UI_TEMP_MESSAGE;
    tempMessageStartTime = millis();
    tempMessageDuration = durationMs;

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(line1.substring(0, LCD_COLS)); // Truncar a tamaño del LCD
    lcd.setCursor(0, 1); lcd.print(line2.substring(0, LCD_COLS)); // Truncar a tamaño del LCD
    printSerialDebug("LCD Temp Msg: " + line1 + " / " + line2);
    // No se llama a forceLcdUpdate aquí, el loop de updateDisplay se encarga de volver al previousUIState
}

// Reinicia el temporizador de inactividad del menú
void resetMenuTimeout() {
    lastEncoderActivityTime = millis();
}

// Verifica si el menú debe cerrarse por inactividad
void checkMenuTimeout() {
    if (currentUIState != UI_HOME && currentUIState != UI_TEMP_MESSAGE) { // Si estamos en algún menú o pantalla de edición/info
        if (millis() - lastEncoderActivityTime >= MENU_TIMEOUT_MS) {
            printSerialDebug("Timeout de menu, volviendo a HOME.");
            currentUIState = UI_HOME;
            forceLcdUpdate = true; // Forzar redibujo de la pantalla HOME
        }
    }
}

// =============================================================================
// IMPLEMENTACIÓN DE FUNCIONES DE WIFI, NTP, PREFERENCIAS, FS, WEB SERVER
// (Mayormente sin cambios significativos respecto a la versión anterior,
//  se han revisado comentarios y pequeñas lógicas)
// =============================================================================

void setupWiFi() {
    WiFi.mode(WIFI_STA);
    wifiManager.setDebugOutput(true);
    wifiManager.setMinimumSignalQuality(20);
    String apName = String(AP_SSID_PREFIX); 
    
    lcd.clear(); lcd.setCursor(0,0); lcd.print("Conectando WiFi");
    printSerialDebug("Intentando conectar a WiFi o iniciar portal de configuracion...");

    wifiManager.setAPCallback([](WiFiManager *myWiFiManager) {
        String portalSsid = myWiFiManager->getConfigPortalSSID();
        String portalIp = WiFi.softAPIP().toString(); // Obtener la IP del AP

        printSerialDebug("Portal AP Activo. SSID: " + portalSsid + ", IP: " + portalIp);
        
        // Actualizar el LCD directamente aquí
        lcd.clear();
        lcd.setCursor(0, 0);
        String linea1Display = ""; 
        int espacioParaSsid = LCD_COLS - linea1Display.length();
        if (portalSsid.length() > espacioParaSsid) {
            linea1Display += portalSsid.substring(0, espacioParaSsid);
        } else {
            linea1Display += portalSsid;
        }
        lcd.print(linea1Display);
        
        lcd.setCursor(0, 1);
        // Formatear Línea 2: "IP: [IP_DEL_AP]"
        String linea2Display = "" + portalIp;
        if (linea2Display.length() > LCD_COLS) {
            linea2Display = linea2Display.substring(0, LCD_COLS);
        }
        lcd.print(linea2Display);

        // Ya no es estrictamente necesario forceLcdUpdate = true; aquí
        // porque hemos actualizado el LCD directamente.
        // Pero no hace daño dejarlo por si displayHomeScreen() tiene otra lógica que deba ejecutarse.
        // forceLcdUpdate = true; 
    });

    wifiManager.setSaveConfigCallback([]() {
        printSerialDebug("Nuevas credenciales WiFi guardadas. Reiniciando...");
        delay(1000); 
        ESP.restart();
    });
    wifiManager.setConfigPortalTimeout(120); 

    if (wifiManager.autoConnect(apName.c_str())) {
        wifiConnected = true;
        printSerialDebug("WiFi Conectado!");
    } else {
        printSerialDebug("Fallo al conectar WiFi y el portal se cerro o tuvo timeout.");
        wifiConnected = false;
        // Si el portal se activó, el callback ya actualizó el LCD.
        // Si el portal tuvo timeout ANTES de que el usuario conectara, el LCD mostrará lo del callback.
    }
    forceLcdUpdate = true; // Para que el loop principal actualice con el estado final (Online/Offline)
                           // Si el portal sigue activo (sin timeout), displayHomeScreen() volverá a mostrar la info del AP.
}

void updateNTP() {
    if (wifiConnected) {
        if (timeClient.isTimeSet()) { // Si ya tenemos la hora, solo actualizar
            timeClient.update();
        } else { // Si no tenemos la hora, forzar la primera obtención
            if (timeClient.forceUpdate()) {
                printSerialDebug("NTP obtenido por primera vez: " + timeClient.getFormattedTime());
                forceLcdUpdate = true; // Para mostrar la hora en el LCD
            } else {
                printSerialDebug("Fallo al obtener NTP inicial.");
            }
        }
    }
}

String getFormattedTime(bool withSeconds) {
    if (!wifiConnected || !timeClient.isTimeSet()) {
        return withSeconds ? "--:--:--" : "--:--";
    }
    String timeStr = timeClient.getFormattedTime(); // Formato HH:MM:SS
    if (withSeconds) {
        return timeStr;
    } else {
        return timeStr.substring(0, 5); // Formato HH:MM
    }
}

String getFormattedDateTime() {
    if (!wifiConnected || !timeClient.isTimeSet()) {
        return "---- -- --:--:--"; // Placeholder
    }
    time_t epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime(&epochTime); // Convierte epoch a estructura tm (UTC)
    
    char buffer[20];
    // El offset UTC-3 ya se aplicó en la configuración de NTPClient
    int year = ptm->tm_year + 1900;
    int month = ptm->tm_mon + 1;
    int day = ptm->tm_mday;
    // Usar las horas, minutos y segundos directamente de timeClient que ya tiene el offset
    sprintf(buffer, "%04d-%02d-%02d-%02d-%02d-%02d", year, month, day, 
            timeClient.getHours(), timeClient.getMinutes(), timeClient.getSeconds());
    return String(buffer);
}

String getLocalIP() {
    if (wifiConnected) {
        return WiFi.localIP().toString();
    } else if (wifiManager.getConfigPortalActive()) {
        return WiFi.softAPIP().toString(); // IP del portal de configuración
    }
    return "N/A";
}

void loadDefaultPreferences() {
    printSerialDebug("Cargando preferencias por defecto.");
    // Asignar todos los valores por defecto a la estructura currentConfig
    currentConfig = {
        60, false, // thFrecuenciaMuestreo, thModoTest
        6, 0, false, // lucesHoraON, lucesHoraOFF, lucesModoTest
        6, 0, false, // ventHoraON, ventHoraOFF, ventModoTest
        0, 1, false, // extrFrecuenciaHoras, extrDuracionMinutos, extrModoTest
        0, 1, 21, false, // riegoFrecuenciaDias, riegoDuracionMinutos, riegoHoraDisparo, riegoModoTest
        1, 4380, false // registroFrecuenciaHoras, registroTamMax, registroModoTest
    };
    savePreferences(); // Guardar estos defaults en NVS para la próxima vez
}

void loadPreferences() {
    printSerialDebug("Cargando preferencias...");
    // preferences.begin() ya se llamó en setup()
    if (!preferences.isKey(KEY_TH_FREQ)) { // Si una clave principal no existe, cargar defaults
        loadDefaultPreferences();
    } else { // Cargar todas las configuraciones desde NVS
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

void savePreferences() { // Guarda toda la estructura currentConfig en NVS
    printSerialDebug("Guardando todas las preferencias...");
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
    printSerialDebug("Todas las preferencias guardadas en NVS.");
}

void resetWiFiCredentialsAndRestart() {
    printSerialDebug("Borrando credenciales WiFi y reiniciando...");
    wifiManager.resetSettings(); // Borra las credenciales WiFi guardadas
    delay(1000);
    ESP.restart();
}

void initLittleFS() {
    if (!LittleFS.begin(true)) { // true = formatea si no se puede montar
        printSerialDebug("Error al montar LittleFS. Se intentara formatear.");
        if (!LittleFS.begin(true)) { // Intentar de nuevo formateando
             printSerialDebug("FALLO CRITICO: No se pudo montar/formatear LittleFS.");
             return;
        }
    }
    printSerialDebug("LittleFS montado correctamente.");
    // Opcional: Listar archivos para depuración
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while(file){
        printSerialDebug("  FS File: " + String(file.name()) + " Size: " + String(file.size()));
        file = root.openNextFile();
    }
    root.close(); // Cerrar el directorio raíz
    // file.close(); // No es necesario aquí, file ya estaría cerrado o sería inválido si no hay más
}

void logData() {
    if (!timeClient.isTimeSet()) {
        printSerialDebug("Registro de datos omitido: hora no sincronizada.");
        return;
    }
    rotateLogIfNeeded(); // Asegurar que haya espacio antes de escribir, rotando si es necesario

    File logFile = LittleFS.open(LOG_FILE, FILE_APPEND);
    if (!logFile) {
        printSerialDebug("Error al abrir archivo de log para escritura (append).");
        return;
    }

    String dateTime = getFormattedDateTime();
    String logEntry = dateTime + "," +
                      String(currentTemperature,1) + "," + // Temperatura con 1 decimal
                      String(currentHumidity,1) + "," +  // Humedad con 1 decimal
                      (relayStates[0] ? "ON" : "OFF") + "," + // Estado Luces
                      (relayStates[1] ? "ON" : "OFF") + "," + // Estado Ventilación
                      (relayStates[2] ? "ON" : "OFF") + "," + // Estado Extracción
                      (relayStates[3] ? "ON" : "OFF");       // Estado Riego

    if (logFile.println(logEntry)) {
        printSerialDebug("Datos registrados en log: " + logEntry);
    } else {
        printSerialDebug("Error al escribir en el archivo de log.");
    }
    logFile.close();
}

int getLogEntryCount() {
    File logFile = LittleFS.open(LOG_FILE, FILE_READ);
    if (!logFile) {
        printSerialDebug("Archivo de log no encontrado para contar entradas.");
        return 0;
    }
    int count = 0;
    while (logFile.available()) {
        logFile.readStringUntil('\n'); // Leer una línea
        count++;
    }
    logFile.close();
    return count;
}

void rotateLogIfNeeded() {
    int currentEntries = getLogEntryCount();
    if (currentEntries < currentConfig.registroTamMax) {
        return; // No se necesita rotar, hay espacio
    }
    printSerialDebug("Rotando log. Entradas actuales: " + String(currentEntries) + ", Max permitido: " + String(currentConfig.registroTamMax));

    File oldLog = LittleFS.open(LOG_FILE, FILE_READ);
    if (!oldLog) {
        printSerialDebug("Error al abrir archivo de log para rotar (lectura).");
        return;
    }

    String tempLogFileName = String(LOG_FILE) + ".tmp"; // Nombre para archivo temporal
    File tempLog = LittleFS.open(tempLogFileName.c_str(), FILE_WRITE);
    if (!tempLog) {
        printSerialDebug("Error al crear archivo de log temporal para rotacion.");
        oldLog.close();
        return;
    }

    // Omitir la primera línea (la más antigua) del archivo original
    if (oldLog.available()) {
        oldLog.readStringUntil('\n'); 
    }

    // Copiar el resto de las líneas del archivo original al archivo temporal
    while (oldLog.available()) {
        tempLog.println(oldLog.readStringUntil('\n'));
    }
    oldLog.close(); // Cerrar archivo original
    tempLog.close(); // Cerrar archivo temporal (asegura que se escriba todo)

    // Reemplazar el log antiguo con el temporal
    if (!LittleFS.remove(LOG_FILE)) { // Borrar el archivo de log original
        printSerialDebug("Error al borrar el archivo de log original durante la rotacion.");
    } else {
        if (!LittleFS.rename(tempLogFileName.c_str(), LOG_FILE)) { // Renombrar el temporal al nombre original
            printSerialDebug("Error al renombrar el archivo de log temporal.");
        } else {
            printSerialDebug("Log rotado exitosamente.");
        }
    }
}

// Sirve un archivo desde LittleFS al cliente web
// path: ruta del archivo en LittleFS (ej. "/index.html")
// download: si es true, añade cabecera para forzar la descarga con el nombre del archivo
void serveFile(String path, String contentType, bool download) {
    if (LittleFS.exists(path)) { // Verificar si el archivo existe
        File file = LittleFS.open(path, "r"); // Abrir el archivo en modo lectura
        if (!file) { // Si no se pudo abrir el archivo (aunque exista, podría ser un error)
            printSerialDebug("Error al abrir archivo (aunque existe): " + path);
            handleNotFound(); // Manejar como si no se encontrara
            return;
        }

        String fileName = path; // Por defecto, usar la ruta completa como nombre de archivo
        if (path.lastIndexOf('/') >= 0) { // Extraer solo el nombre de archivo de la ruta
            fileName = path.substring(path.lastIndexOf('/') + 1);
        }

        if (download) { // Si se debe forzar la descarga
             // Enviar cabecera Content-Disposition para sugerir al navegador que descargue el archivo
             // y con qué nombre de archivo hacerlo.
             server.sendHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
        }

        server.streamFile(file, contentType); // Enviar el contenido del archivo al cliente
        file.close(); // Cerrar el archivo

        // Construir el mensaje de depuración usando String para concatenación segura
        String debugMsg = "Sirviendo archivo";
        debugMsg += (download ? " para descarga: " : ": ");
        debugMsg += path;
        debugMsg += " como ";
        debugMsg += fileName;
        printSerialDebug(debugMsg);

    } else { // Si el archivo no existe en la ruta especificada
        printSerialDebug("Archivo no encontrado para servir: " + path);
        handleNotFound(); // Manejar error 404 (Página No Encontrada)
    }
}

// --- Configuración de Rutas del Servidor Web ---
void setupWebServer() {
    server.on("/", HTTP_GET, handleRoot);                     // Página principal
    server.on("/data", HTTP_GET, handleData);                 // Endpoint para obtener datos JSON
    server.on("/control", HTTP_GET, handleControl);           // Endpoint para controlar relés
    server.on("/riego_manual", HTTP_GET, handleRiegoManual);   // Endpoint para activar riego manual
    server.on("/descargarLogCsv", HTTP_GET, handleDownloadLog); // Endpoint para descargar el log CSV
    server.on("/ultimasEntradasLog", HTTP_GET, handleLastLogEntries); // Endpoint para obtener últimas N entradas del log
    server.onNotFound(handleNotFound);                        // Manejador para rutas no encontradas (404)
    // server.begin(); // Se llama en setup() después de que WiFi conecte
}

// --- Manejadores de Rutas del Servidor Web ---
void handleRoot() { // Sirve el archivo HTML principal
    serveFile(WEB_MONITOR_HTML_FILE, "text/html", false); // false = no forzar descarga
}

void handleData() { // Envía datos actuales de sensores y relés como JSON
    String json = "{";
    json += "\"temperatura\":" + String(currentTemperature, 1) + ",";
    json += "\"humedad\":" + String(currentHumidity, 1) + ",";
    json += "\"luces\":\"" + String(relayStates[0] ? "ON" : "OFF") + "\",";
    json += "\"ventilacion\":\"" + String(relayStates[1] ? "ON" : "OFF") + "\",";
    json += "\"extraccion\":\"" + String(relayStates[2] ? "ON" : "OFF") + "\",";
    json += "\"riego\":\"" + String(relayStates[3] ? "ON" : "OFF") + "\"";
    json += "}";
    server.send(200, "application/json", json);
    // printSerialDebug("Sirviendo /data: " + json.substring(0, 50) + "..."); // Truncar para no llenar el log serial
}

void handleControl() { // Maneja comandos para controlar relés
    String componente = server.arg("componente");
    String accion = server.arg("accion"); // "on", "off", o "toggle"

    printSerialDebug("Recibido comando /control: componente=" + componente + ", accion=" + accion);
    int relayIndex = -1;
    bool* testModeFlag = nullptr; // Puntero para poder desactivar el modo test del componente específico

    if (componente == "luces") { relayIndex = 0; testModeFlag = &currentConfig.lucesModoTest; }
    else if (componente == "ventilacion") { relayIndex = 1; testModeFlag = &currentConfig.ventModoTest; }
    else if (componente == "extraccion") { relayIndex = 2; testModeFlag = &currentConfig.extrModoTest; }
    else if (componente == "riego") { relayIndex = 3; testModeFlag = &currentConfig.riegoModoTest; }
    
    if (relayIndex != -1) {
        bool newState;
        if (accion == "on") newState = true;
        else if (accion == "off") newState = false;
        else if (accion == "toggle") newState = !relayStates[relayIndex]; // Cambiar al estado opuesto
        else {
            server.send(400, "text/plain", "Accion invalida");
            return;
        }
        // Si el componente estaba en modo test y se controla desde la web, desactivar el modo test
        if (testModeFlag && *testModeFlag) {
            *testModeFlag = false; // Desactivar modo test en la configuración
            savePreferences();     // Guardar el cambio en Preferences
            printSerialDebug("Modo Test desactivado para " + componente + " via web.");
        }
        controlRelay(relayIndex, newState, true); // true para manualOverride, actualiza estado y relé
        server.send(200, "text/plain", "OK, " + componente + " " + (newState ? "ON" : "OFF"));
    } else {
        server.send(400, "text/plain", "Componente invalido");
    }
}

void handleRiegoManual() { // Maneja la activación manual del riego
    printSerialDebug("Recibido comando /riego_manual");
    if (!riegoActive) { // Solo si no está ya regando
        // Si el modo test de riego estaba activo, desactivarlo
        if (currentConfig.riegoModoTest) {
            currentConfig.riegoModoTest = false;
            savePreferences();
            printSerialDebug("Modo Test Riego desactivado via web (activacion manual).");
        }
        controlRelay(3, true, true); // Encender relé de riego (manualOverride = true)
        riegoActive = true; // Marcar que el ciclo de riego manual está activo
        riegoStartTime = millis(); // Registrar inicio para controlar duración (usará currentConfig.riegoDuracionMinutos)
        printSerialDebug("Riego manual iniciado.");
        server.send(200, "text/plain", "OK, Riego manual iniciado");
    } else {
        server.send(200, "text/plain", "Riego ya se encuentra activo");
    }
}

void handleDownloadLog() { // Permite descargar el archivo de log CSV completo
    serveFile(LOG_FILE, "text/csv", true); // true para forzar descarga con nombre de archivo
}

void handleLastLogEntries() { // Envía las últimas N líneas del archivo de log
    int linesToShow = 15; // Número de líneas por defecto a mostrar
    if (server.hasArg("lineas")) { // Si el cliente especifica cuántas líneas
        linesToShow = server.arg("lineas").toInt();
        if (linesToShow <= 0 || linesToShow > 100) linesToShow = 15; // Limitar para evitar uso excesivo de memoria
    }

    File logFile = LittleFS.open(LOG_FILE, FILE_READ);
    if (!logFile) {
        server.send(500, "text/plain", "Error al abrir archivo de log");
        return;
    }
    
    // Leer las últimas N líneas (forma eficiente para archivos potencialmente grandes)
    // Esto es una simplificación: lee todo y luego extrae. Para archivos muy grandes, sería mejor leer desde el final.
    String lineBuffer[linesToShow]; // Buffer para almacenar las líneas
    int currentLineCount = 0;       // Contador de líneas leídas
    int bufferIndex = 0;            // Índice para el buffer circular

    while (logFile.available()){
        String line = logFile.readStringUntil('\n');
        lineBuffer[bufferIndex % linesToShow] = line; // Almacenar en buffer circular
        bufferIndex++;
        currentLineCount++;
    }
    logFile.close();

    String output = "";
    // Reconstruir las últimas N líneas en el orden correcto desde el buffer circular
    int startReadingIndex = (currentLineCount < linesToShow) ? 0 : (bufferIndex % linesToShow);
    for (int i = 0; i < min(linesToShow, currentLineCount); ++i) {
        output += lineBuffer[startReadingIndex] + "\n";
        startReadingIndex = (startReadingIndex + 1) % linesToShow;
    }
    
    server.send(200, "text/plain", output); // Enviar las líneas como texto plano
    printSerialDebug("Sirviendo ultimas " + String(min(linesToShow, currentLineCount)) + " lineas del log.");
}

void handleNotFound() { // Manejador para error 404 (Página No Encontrada)
    String message = "404 - Pagina No Encontrada\n\n";
    message += "URI: "; message += server.uri();
    message += "\nMetodo: "; message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArgumentos: "; message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
    printSerialDebug("HTTP 404: " + server.uri());
}

// --- Utilidades ---
// Imprime mensajes de depuración al Monitor Serie con timestamp
void printSerialDebug(String message) {
    Serial.println("[" + String(millis() / 1000.0, 2) + "s] " + message);
}

// Controla el parpadeo del LED integrado según el modo de operación
void blinkLED() {
    unsigned long currentTime = millis();
    
    if (wifiManager.getConfigPortalActive()) { // Modo Configuración (Portal AP activo)
        // Parpadeo lento: 1 segundo ON, 1 segundo OFF
        if (currentTime - lastLEDToggleTime >= ledBlinkIntervalConfig) {
            ledBlinkState = !ledBlinkState;
            digitalWrite(LED_BUILTIN_PIN, ledBlinkState);
            lastLEDToggleTime = currentTime;
        }
    } else if (wifiConnected) { // Modo Normal (Conectado a WiFi)
        // Doble parpadeo rápido cada 3 segundos
        if (ledBlinkCount < NORMAL_MODE_BLINK_COUNT * 2) { // *2 porque es ON y OFF para cada parpadeo
            if (currentTime - lastLEDToggleTime >= ledBlinkIntervalNormal) {
                ledBlinkState = !ledBlinkState;
                digitalWrite(LED_BUILTIN_PIN, ledBlinkState);
                lastLEDToggleTime = currentTime;
                ledBlinkCount++;
            }
        } else { // Pausa larga para completar el ciclo de 3 segundos
            if (currentTime - lastLEDToggleTime >= ledBlinkPauseNormal) {
                ledBlinkCount = 0; // Reiniciar ciclo de parpadeo
                lastLEDToggleTime = currentTime; // Para que el primer blink del nuevo ciclo sea más preciso
            }
        }
    } else { // Offline o intentando conectar (sin portal activo)
        // Parpadeo muy lento (ej. 0.25 Hz: 2s ON, 2s OFF)
         if (currentTime - lastLEDToggleTime >= 2000) { 
            ledBlinkState = !ledBlinkState;
            digitalWrite(LED_BUILTIN_PIN, ledBlinkState);
            lastLEDToggleTime = currentTime;
        }
    }
}
