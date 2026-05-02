#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// Pines para LEDs de ambientes
#define LED_SALA_1       22
#define LED_SALA_2       23
#define LED_SALA_3       24
#define LED_COMEDOR_1    25
#define LED_COMEDOR_2    26
#define LED_COCINA_1     27
#define LED_COCINA_2     28
#define LED_COCINA_3     29
#define LED_BANO_1       30
#define LED_BANO_2       31
#define LED_HABITACION_1 32
#define LED_HABITACION_2 33

// LEDs de estado
#define LED_AZUL         10
#define LED_VERDE        11
#define LED_ROJO         12

// Otros pines
#define MOTOR_PIN        9
#define SERVO_PIN        8
#define BOTON_PIN        7

// Inicialización de LCD y Servo
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo servoPuerta;

// Ángulos del servo
#define ANGULO_ABIERTO   90
#define ANGULO_CERRADO   0

// Direcciones en EEPROM para cada modo
#define EEPROM_MODO_FIESTA      0
#define EEPROM_MODO_RELAJADO    25
#define EEPROM_MODO_NOCHE       50
#define EEPROM_MODO_ENCENDER    75
#define EEPROM_MODO_APAGAR      100
#define EEPROM_VALIDACION       125
#define EEPROM_MAGIC_NUMBER     0xAB
#define EEPROM_MSG_OFFSET       2
#define EEPROM_MSG_SIZE         17

// Tiempo de parpadeo para LEDs
#define TIEMPO_PARPADEO         300
const int ledsAmbientes[] = {22,23,24,25,26,27,28,29,30,31,32,33};
const int NUM_LEDS = sizeof(ledsAmbientes) / sizeof(ledsAmbientes[0]);

// Estructura para configuración de modos
struct ConfiguracionModo {
  bool ventilador;
  bool leds[12];
  char tipoLeds;
  char mensajeLCD[17];
};

// Variables globales
String modoActual = "INICIO";
bool puertaAbierta = false;
bool estadoBotonAnterior = LOW;
unsigned long tiempoAnteriorAlternancia = 0;
bool estadoAlternancia = false;
const unsigned long INTERVALO_ALTERNANCIA = 500;

String bufferSerial = "";
bool cargandoArchivo = false;
bool errorArchivo = false;

ConfiguracionModo configActual;
ConfiguracionModo cfgTemp;
int modoSeleccionado = -1;

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

  for (int i = 0; i < NUM_LEDS; i++) {
    pinMode(ledsAmbientes[i], OUTPUT);
    digitalWrite(ledsAmbientes[i], LOW);
  }
  pinMode(LED_AZUL, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);
  pinMode(BOTON_PIN, INPUT_PULLUP);
  servoPuerta.attach(SERVO_PIN);
  
  // Corrección: puerta cerrada al inicio
  puertaAbierta = false;
  servoPuerta.write(ANGULO_CERRADO);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("CASA INTELIGENTE");
  lcd.setCursor(0,1);
  lcd.print("Iniciando...");
  delay(2000);

  if (EEPROM.read(EEPROM_VALIDACION) == EEPROM_MAGIC_NUMBER) {
    lcd.clear();
    lcd.print("Cargando config");
    lcd.setCursor(0,1);
    lcd.print("desde EEPROM...");
    delay(1500);
  } else {
    guardarConfiguracionDefecto();
  }

  digitalWrite(LED_AZUL, HIGH);
  digitalWrite(LED_ROJO, LOW);
  lcd.clear();
  lcd.print("Sistema Listo");
  lcd.setCursor(0,1);
  lcd.print("Esperando...");
  
  Serial.println("READY");
  Serial1.println("READY");
}

// ---------------- LOOP PRINCIPAL ----------------
void loop() {
  if (Serial.available() > 0) procesarSerialUSB();
  if (Serial1.available() > 0) procesarSerialBluetooth();
  manejarBotonPuerta();
  ejecutarEfectosLeds();
  delay(10);
}

// ---------------- PROCESAMIENTO USB ----------------
void procesarSerialUSB() {
  char c = Serial.read();
  if (c == '\n' || c == '\r') {
    if (bufferSerial.length() > 0) {
      procesarLineaArchivoOrg(bufferSerial);
      bufferSerial = "";
    }
  } else {
    bufferSerial += c;
  }
}

// ---------------- PROCESAMIENTO BLUETOOTH ----------------
void procesarSerialBluetooth() {
  String comando = Serial1.readStringUntil('\n');
  comando.trim();
  if (comando.length() > 0) {
    ejecutarComando(comando);
  }
}

// ---------------- PROCESAR ARCHIVO .ORG ----------------
void procesarLineaArchivoOrg(String linea) {
  linea.trim();
  if (linea.length() == 0 || linea.startsWith("//")) return;

  if (linea.equalsIgnoreCase("ERROR_SINTAXIS")) {
    digitalWrite(LED_ROJO, HIGH);
    lcd.clear();
    lcd.print("ERROR SINTAXIS");
    lcd.setCursor(0,1);
    lcd.print("Ver archivo .org");
    return;
  }

  if (linea.equalsIgnoreCase("conf_ini")) {
    cargandoArchivo = true;
    errorArchivo = false;
    digitalWrite(LED_ROJO, LOW);
    lcd.clear();
    lcd.print("Cargando .org");
    return;
  }

  if (linea.equalsIgnoreCase("conf:fin")) {
    if (cargandoArchivo && !errorArchivo) {
      if (modoSeleccionado != -1) {
        guardarConfiguracion(modoSeleccionado, cfgTemp);
        modoSeleccionado = -1;
      }
      EEPROM.write(EEPROM_VALIDACION, EEPROM_MAGIC_NUMBER);
      
      // Parpadeo LED verde sin bloquear completamente
      for (int i=0; i<3; i++) {
        digitalWrite(LED_VERDE, HIGH);
        delay(TIEMPO_PARPADEO);
        digitalWrite(LED_VERDE, LOW);
        delay(TIEMPO_PARPADEO);
      }
      
      lcd.clear();
      lcd.print("Config guardada");
      delay(2000);
      lcd.clear();
      lcd.print("Sistema Listo");
    }
    cargandoArchivo = false;
    return;
  }

  if (cargandoArchivo) procesarComandoOrg(linea);
}

// Procesa comandos específicos del archivo .org
void procesarComandoOrg(String linea) {
  linea.trim();

  // Detectar inicio de modo (incluye encender_todo y apagar_todo)
  if (linea.equalsIgnoreCase("modo_fiesta") ||
      linea.equalsIgnoreCase("modo_relajado") ||
      linea.equalsIgnoreCase("modo_noche") ||
      linea.equalsIgnoreCase("encender_todo") ||
      linea.equalsIgnoreCase("apagar_todo")) {

    if (modoSeleccionado != -1) {
      guardarConfiguracion(modoSeleccionado, cfgTemp);
    }
    cfgTemp = {};

    if (linea.equalsIgnoreCase("modo_fiesta")) modoSeleccionado = EEPROM_MODO_FIESTA;
    else if (linea.equalsIgnoreCase("modo_relajado")) modoSeleccionado = EEPROM_MODO_RELAJADO;
    else if (linea.equalsIgnoreCase("modo_noche")) modoSeleccionado = EEPROM_MODO_NOCHE;
    else if (linea.equalsIgnoreCase("encender_todo")) modoSeleccionado = EEPROM_MODO_ENCENDER;
    else if (linea.equalsIgnoreCase("apagar_todo")) modoSeleccionado = EEPROM_MODO_APAGAR;
    return;
  }

  if (modoSeleccionado != -1) {
    if (linea.startsWith("Ventilador:")) {
      String valor = linea.substring(linea.indexOf(":")+1);
      valor.trim();
      cfgTemp.ventilador = valor.equalsIgnoreCase("ON");
    }
    else if (linea.startsWith("LED'S:") || linea.startsWith("LEDS:")) {
      String valor = linea.substring(linea.indexOf(":")+1);
      valor.trim();
      if (valor.equalsIgnoreCase("Alternandose")) cfgTemp.tipoLeds = 'A';
      else if (valor.equalsIgnoreCase("ON") || valor.equalsIgnoreCase("Encendidos")) cfgTemp.tipoLeds = 'F';
      else cfgTemp.tipoLeds = 'O';
    }
    else if (linea.startsWith("Mensaje en LCD:")) {
      String mensaje = linea.substring(linea.indexOf(":")+1);
      mensaje.trim();
      // Remover comillas si existen
      if (mensaje.startsWith("\"")) mensaje = mensaje.substring(1);
      if (mensaje.endsWith("\"")) mensaje = mensaje.substring(0, mensaje.length()-1);
      mensaje.toCharArray(cfgTemp.mensajeLCD, 17);
    }
  }
}

// ---------------- EJECUTAR COMANDOS BLUETOOTH ----------------
void ejecutarComando(String comando) {
  comando.trim();
  comando.toLowerCase();
  
  // Resetear LED rojo al recibir comando válido
  digitalWrite(LED_ROJO, LOW);

  if (comando == "modo_fiesta") {
    modoActual = "FIESTA";
    cargarConfiguracion(EEPROM_MODO_FIESTA);
  } else if (comando == "modo_relajado") {
    modoActual = "RELAJADO";
    cargarConfiguracion(EEPROM_MODO_RELAJADO);
  } else if (comando == "modo_noche") {
    modoActual = "NOCHE";
    cargarConfiguracion(EEPROM_MODO_NOCHE);
  } else if (comando == "encender_todo") {
    modoActual = "ENCENDER_TODO";
    cargarConfiguracion(EEPROM_MODO_ENCENDER);
  } else if (comando == "apagar_todo") {
    modoActual = "APAGAR_TODO";
    cargarConfiguracion(EEPROM_MODO_APAGAR);
  } else if (comando == "estado") {
    reportarEstado();
    return;
  } else {
    digitalWrite(LED_ROJO, HIGH);
    lcd.clear();
    lcd.print("ERROR:");
    lcd.setCursor(0,1);
    lcd.print("Modo invalido");
    Serial1.println("Error");
    delay(2000);
    actualizarLCD();
    return;
  }
  
  digitalWrite(MOTOR_PIN, configActual.ventilador ? HIGH : LOW);
  actualizarLCD();
  Serial1.println("Modo: " + modoActual);
}

// ---------------- FUNCIONES DE APOYO ----------------
void reportarEstado() {
  String estado = "Modo: " + modoActual + " Vent:" + (configActual.ventilador ? "ON" : "OFF");
  estado += " Puerta:" + String(puertaAbierta ? "ABR" : "CERR");
  Serial.println(estado);
  Serial1.println(estado);
  lcd.clear();
  lcd.print("Estado:");
  lcd.setCursor(0,1);
  lcd.print(modoActual);
  delay(2000);
  actualizarLCD();
}

void encenderTodosLosLeds() {
  for (int i=0; i<NUM_LEDS; i++) {
    digitalWrite(ledsAmbientes[i], HIGH);
  }
}

void apagarTodosLosLeds() {
  for (int i=0; i<NUM_LEDS; i++) {
    digitalWrite(ledsAmbientes[i], LOW);
  }
}

void ejecutarEfectosLeds() {
  if (configActual.tipoLeds == 'A') {
    unsigned long ahora = millis();
    if (ahora - tiempoAnteriorAlternancia >= INTERVALO_ALTERNANCIA) {
      tiempoAnteriorAlternancia = ahora;
      estadoAlternancia = !estadoAlternancia;
      for (int i=0; i<NUM_LEDS; i++) {
        digitalWrite(ledsAmbientes[i], (i%2==0) ? estadoAlternancia : !estadoAlternancia ? HIGH : LOW);
      }
    }
  } else if (configActual.tipoLeds == 'F') {
    encenderTodosLosLeds();
  } else if (configActual.tipoLeds == 'O') {
    apagarTodosLosLeds();
  }
}

void manejarBotonPuerta() {
  bool estadoBotonActual = digitalRead(BOTON_PIN);
  if (estadoBotonActual == HIGH && estadoBotonAnterior == LOW) {
    delay(50);
    if (digitalRead(BOTON_PIN) == HIGH) {
      puertaAbierta = !puertaAbierta;
      servoPuerta.write(puertaAbierta ? ANGULO_ABIERTO : ANGULO_CERRADO);
      lcd.clear();
      lcd.print(puertaAbierta ? "Puerta: ABRE" : "Puerta: CERRADA");
      delay(1000);
      actualizarLCD();
    }
  }
  estadoBotonAnterior = estadoBotonActual;
}

void actualizarLCD() {
  lcd.clear();
  
  // Usar mensaje personalizado si existe, sino usar por defecto
  if (configActual.mensajeLCD[0] != '\0') {
    lcd.print(configActual.mensajeLCD);
  } else {
    if (modoActual == "FIESTA") {
      lcd.print("Modo: FIESTA");
      lcd.setCursor(0,1);
      lcd.print("Air:ON LED:Alt");
    } else if (modoActual == "RELAJADO") {
      lcd.print("Modo: RELAJADO");
      lcd.setCursor(0,1);
      lcd.print("Air:OFF LED:OFF");
    } else if (modoActual == "NOCHE") {
      lcd.print("Modo: NOCHE");
      lcd.setCursor(0,1);
      lcd.print("Air:OFF LED:OFF");
    } else if (modoActual == "ENCENDER_TODO") {
      lcd.print("LED'S: ON");
      lcd.setCursor(0,1);
      lcd.print("Air: ON");
    } else if (modoActual == "APAGAR_TODO") {
      lcd.print("LED'S: OFF");
      lcd.setCursor(0,1);
      lcd.print("Air: OFF");
    } else {
      lcd.print("ERROR:");
      lcd.setCursor(0,1);
      lcd.print("Modo invalido");
    }
  }
}

// ---------------- EEPROM ----------------
void guardarConfiguracion(int dir, ConfiguracionModo cfg) {
  EEPROM.update(dir, cfg.ventilador ? 1 : 0);
  EEPROM.update(dir+1, cfg.tipoLeds);
  for (int i=0; i<16; i++) {
    EEPROM.update(dir+EEPROM_MSG_OFFSET+i, cfg.mensajeLCD[i]);
  }
  for (int i=0; i<12; i++) {
    EEPROM.update(dir+EEPROM_MSG_OFFSET+EEPROM_MSG_SIZE+i, cfg.leds[i] ? 1 : 0);
  }
}

void cargarConfiguracion(int dir) {
  configActual.ventilador = EEPROM.read(dir) == 1;
  configActual.tipoLeds = EEPROM.read(dir+1);
  for (int i=0; i<16; i++) {
    configActual.mensajeLCD[i] = EEPROM.read(dir+EEPROM_MSG_OFFSET+i);
  }
  configActual.mensajeLCD[16] = '\0';
  for (int i=0; i<12; i++) {
    configActual.leds[i] = EEPROM.read(dir+EEPROM_MSG_OFFSET+EEPROM_MSG_SIZE+i) == 1;
  }
}

void guardarConfiguracionDefecto() {
  ConfiguracionModo fiesta, relajado, noche, encender, apagar;
  
  fiesta.ventilador = true; fiesta.tipoLeds='A';
  strcpy(fiesta.mensajeLCD, "Modo: FIESTA");
  for (int i=0; i<12; i++) fiesta.leds[i]=true;
  
  relajado.ventilador = false; relajado.tipoLeds='O';
  strcpy(relajado.mensajeLCD, "Modo: RELAJADO");
  for (int i=0; i<12; i++) relajado.leds[i]=false;
  
  noche.ventilador = false; noche.tipoLeds='O';
  strcpy(noche.mensajeLCD, "Modo: NOCHE");
  for (int i=0; i<12; i++) noche.leds[i]=false;
  
  encender.ventilador = true; encender.tipoLeds='F';
  strcpy(encender.mensajeLCD, "LED'S: ON");
  for (int i=0; i<12; i++) encender.leds[i]=true;
  
  apagar.ventilador = false; apagar.tipoLeds='O';
  strcpy(apagar.mensajeLCD, "LED'S: OFF");
  for (int i=0; i<12; i++) apagar.leds[i]=false;

  guardarConfiguracion(EEPROM_MODO_FIESTA, fiesta);
  guardarConfiguracion(EEPROM_MODO_RELAJADO, relajado);
  guardarConfiguracion(EEPROM_MODO_NOCHE, noche);
  guardarConfiguracion(EEPROM_MODO_ENCENDER, encender);
  guardarConfiguracion(EEPROM_MODO_APAGAR, apagar);

  EEPROM.write(EEPROM_VALIDACION, EEPROM_MAGIC_NUMBER);
}
