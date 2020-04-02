/*
   AirQ 0.1 - Medidor de la calidad del aire
   Autor: David Fdez. Mtnez. - 02/04/2020
*/

// Librería que vamos a utilizar
#include <LiquidCrystal_I2C.h> // para la pantalla LCD
#include "DHT.h" // para el sensor de temperatura y humedad
#include "RTClib.h" // para el reloj de tiempo real (RTC)
#include "WiFiEsp.h" //Libreria para comunicarse facilmente con el modulo ESP01 wifi
#include "SoftwareSerial.h" // La necesita WiFiEsp
#include <Wire.h> // Para poder utilizar el bus de comunicaciones I2C (pantalla y reloj de tiempo real)

// definiciones
#define TIPO_DHT DHT11 // tipo de sensor de temperatura/humedad que vamos a usar
#define PIN_DHT11 2 // pin para el sensor de temperatura/humedad
#define PIN_LED 4 // pin para el led de alarma
#define PIN_ZUMBADOR 6 // pin para el zumbador de alarma
#define PIN_RX 11 // pin de recepción para el módulo wifi
#define PIN_MQ_DIGITAL 9 // pin digital para el sensor MQ-135 que utilizaremos para alarma
#define PIN_TX 12 // pin de transmisión para el módulo wifi
#define PIN_MQ_ANALOGICO PIN_A0 // pin analógico para el sensor MQ-135 que utilizaremos para la medición

// constantes
const String ID_DISPOSITIVO = "01"; // para identificar el dispositivo que está realizando las medidas cuando se tengan varios
const int RETARDO = 2000; // indica en milisegundos cada cuánto tiempo realizamos medición, actualizamos la pantalla...

// Configuracion wifi
char ssid[] = "PON_AQUI_TU_SSID"; // SSID (Nombre de la red WiFi)
char pass[] = "PON_AQUI_TU_PASS"; // Contraseña
int status = WL_IDLE_STATUS; // Estado del ESP. No tocar.

// Configuración de IFTTT
char host[] = "maker.ifttt.com"; // url del sitio IFTTT maker
char evento[]   = "controla_calidad_aire"; // evento que vamos a utilizar para registrar las medidas en la hoja Google
char key[] = "PON_AQUI_TU_KEY"; // clave de la API de IFTTT. La podemos consultar con nuestra cuenta en IFTTT.

// Control del tiempo transcurrido para el envío de información al servicio web
long marcaTiempoAnteriorServicioWeb = 0; // me servirá para saber si tengo que enviar ya la información al servicio web o no
const long intervaloServicioWeb = 60000; // (milisegundos) controla cada cuánto tiempo vamos a enviar la información.
// Por ejemplo, 1 min (1*60*1000) = 60000

// Pantalla
LiquidCrystal_I2C lcd(0x3F, 20, 4); // creamos el objeto que modela la pantalla LCD. Los parámetro son:
// - dirección para el bus I2C (0x3F en mi caso)
// - columnas (20)
// - filas (4)

// Sensor temperatura - humedad
DHT dht(PIN_DHT11, TIPO_DHT); // creamos el objeto que modela el sensor de temperatura y humedad, asociado
// al pin correspondiente y del tipo definido.

// Reloj de tiempo real
RTC_DS3231 rtc; // creamos el objeto que modela el reloj de tiempo real para mostrar fecha/hora en pantalla

// Wifi
WiFiEspClient client;  // creamos el objeto que modela el cliente wifi
SoftwareSerial esp8266(PIN_RX, PIN_TX); // y también al dispositivo ESP8266 que utilizamos para conectar por wifi

// Definimos dos caracteres personalizados para la pantalla: una cara sonriente cuando no hay alarma y una cara
// triste que se visualizará cuando haya alarma.
byte sonrisa[8] = {
  0b00000,
  0b00000,
  0b01010,
  0b00000,
  0b10001,
  0b01110,
  0b00000,
  0b00000
};

byte pena[8] = {
  0b00000,
  0b00000,
  0b01010,
  0b00000,
  0b01110,
  0b10001,
  0b00000,
  0b00000
};

void setup() // este bloque se ejecuta sólo al principio de la ejecución
{
  Serial.begin(9600); // iniciamos comunicación serie para poder ver la información en el monitor serie a 9600 baudios.
  esp8266.begin(9600); // iniciamos la comunicación serie para el ESP (wifi)

  lcd.init(); // Inicializamos el LCD
  lcd.backlight(); // Encendemos la luz de fondo del LCD

  // Damos de alta los dos caracteres especiales que necesitaremos
  lcd.createChar(1, sonrisa);
  lcd.createChar(2, pena);

  lcd.clear(); // Limpiamos la pantalla

  // Escribimos en el LCD el título de la aplicación y la identificación del dispositivo
  lcd.print("[ AirQ v0.1 - ID");
  lcd.print(ID_DISPOSITIVO);
  lcd.print(" ]");

  mostrarIniciandoDisplay(); // mostramos un mensaje indicando que estamos iniciando el dispositivo

  // establecemos un par de pines que van a ser de salida
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_ZUMBADOR, OUTPUT);

  dht.begin(); // inicializamos el sensor de temperatura y humedad

  rtc.begin(); // inicializamos el reloj de tiempo real
  if (rtc.lostPower()) { // si se queda sin batería cogemos la fecha de compilación
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  iniciarWifi(); // llamamos a una función que nos inicializará el wifi
  // verEstadoWifi(); // esta función nos mostraría en el monitor datos de la conexión.
  // la tengo quitada para ahorrar algo de memoria, pero se puede poner para depurar errores.

  borraDisplay(); // borramos la línea 2, 3 y 4 del display para empezar a ofrecer información
}

void loop() // y esto es lo que se ejecuta repetidamente
{
  // recojo la medida de los sensores
  int medidaAnalogica = analogRead(PIN_MQ_ANALOGICO);
  bool estado = digitalRead(PIN_MQ_DIGITAL);
  float temperatura = dht.readTemperature();
  float humedad = dht.readHumidity();
  DateTime ahora = rtc.now();

  // para tener el CO2 en ppm hay que hacer algunas cuentas.
  // Los parámetros están cogidos del proyecto de Ulises Gascon https://github.com/UlisesGascon
  // Supondría que tenemos exactamente el mismo sensor que él.
  // Por lo que todo esto habría que comprobarlo y calcularlo correctamente para el sensor concreto a usar.
  // Ya sea por calibración en ambiente controlado o utilizando las tablas que proporciona el fabricante.
  float tension = medidaAnalogica * (5.0 / 1023.0);
  float resistencia = 1000 * ((5 - tension) / tension);
  double CO2ppm = 245 * pow(resistencia / 5463, -2.26); // el dato que realmente nos interesa...

  // mostramos la información obtenida por el monitor serie (en el PC si lo tenemos conectado)
  mostrarInfoLog(medidaAnalogica, tension, CO2ppm, estado, (int) temperatura, (int) humedad, ahora, ID_DISPOSITIVO);
  // actualizamos la información en la pantalla LCD
  mostrarInfoDisplay(CO2ppm, estado, (int) temperatura, (int) humedad, ahora);

  // Vemos ahora si tenemos que mostrar la alarma. Si estado es verdadero, "no hay de qué preocuparse".
  // Si estado es falso, hemos superado el nivel establecido con el potenciómetro del MQ-135, por lo que
  // encenderemos el LED y emitiremos un pitido en cada ciclo
  if (estado) {
    digitalWrite(PIN_LED, LOW);
  } else {
    digitalWrite(PIN_LED, HIGH);
    tono(1);
  }

  // esperamos el retardo establecido
  delay(RETARDO);

  // ahora vemos si nos toca enviar información por el servicio web
  unsigned long marcaTiempoActual = millis(); // obtenemos la marca de tiempo actual
  if (marcaTiempoActual - marcaTiempoAnteriorServicioWeb > intervaloServicioWeb) { // comprobamos si toca
    Serial.println("Registrando información"); // informamos por el monitor serie que vamos a enviar
    mostrarEnviandoDisplay(); // mostramos en pantalla que se está realizando un envío
    marcaTiempoAnteriorServicioWeb = marcaTiempoActual; // refrescamos la marca de tiempo
    // llamamos a una función que nos hemos definido para el envío de la información obtenida en este ciclo
    consumirServicio(evento, (int) CO2ppm, (int) estado, (int) temperatura, (int) humedad, ID_DISPOSITIVO);
    borraDisplay(); // tras el envío, preparamos la pantalla para volver a mostrar la información
  }
}

// Esta función se encarga de mostrar por el monitor serie (en el PC) los datos que se le proporcionan.
// Es bastante intuitiva.
void mostrarInfoLog(int medidaAnalogica, float tension, double CO2ppm, bool estado,
                    int temperatura, int humedad, DateTime tiempo, String idDispositivo) {
  Serial.print("Medida analógica: ");
  Serial.print(medidaAnalogica);
  Serial.print("    Tension: ");
  Serial.print(tension);
  Serial.print("    PPM CO2: ");
  Serial.print(CO2ppm);
  Serial.print("   Estado: ");
  if (!estado)
  {
    Serial.print("Alarma");
  }
  else
  {
    Serial.print("Normal");
  }
  Serial.print("    Temp: ");
  Serial.print(temperatura);
  Serial.print("    Humedad: ");
  Serial.print(humedad);
  Serial.print("    Hora: ");
  Serial.print(tiempo.year());
  Serial.print("-");
  if (tiempo.month() < 10) Serial.print("0");
  Serial.print(tiempo.month());
  Serial.print("-");
  Serial.print(tiempo.day());
  Serial.print(" ");
  Serial.print(tiempo.hour());
  Serial.print(":");
  Serial.print(tiempo.minute());
  Serial.print(":");
  Serial.print(tiempo.second());
  Serial.print("    ID: ");
  Serial.println(idDispositivo);
}

// esta función se encarga de pintar la información por el display. Es bastante intuitiva.
void mostrarInfoDisplay(double CO2ppm, bool estado, int temperatura, int humedad, DateTime ahora) {
  lcd.setCursor(0, 1);
  lcd.print("CO2: ");
  for (int n = 5 ; n < 19; n++)
  {
    lcd.print(" ");
  }
  lcd.setCursor(5, 1);
  lcd.print(CO2ppm);
  lcd.print(" ppm");
  lcd.setCursor(19, 1);
  if (estado) {
    lcd.write(1);
  } else {
    lcd.write(2);
  }
  lcd.setCursor(0, 2);
  lcd.print("Temp: ");
  lcd.print(temperatura);
  lcd.print((char)223);
  lcd.print("C");
  lcd.setCursor(11, 2);
  lcd.print("Hum: ");
  lcd.print(humedad);
  lcd.print(" %");
  lcd.setCursor(3, 3);
  if (ahora.day() < 10) lcd.print("0");
  lcd.print(ahora.day(), DEC);
  lcd.print('/');
  if (ahora.month() < 10) lcd.print("0");
  lcd.print(ahora.month(), DEC);
  lcd.print('/');
  lcd.print(ahora.year() % 1000);
  lcd.print(' ');
  lcd.print(ahora.hour(), DEC);
  lcd.print(':');
  if (ahora.minute() < 10) lcd.print("0");
  lcd.print(ahora.minute(), DEC);
}

// Esta función se encarga de pintar el mensaje cuando el dispositivo se está iniciando.
void mostrarIniciandoDisplay() {
  lcd.setCursor(0, 1);
  lcd.print("                    ");
  lcd.setCursor(0, 2);
  lcd.print("   Iniciando ...    ");
  lcd.setCursor(0, 3);
  lcd.print("                    ");
}

// Esta función se encarga de pintar el mensaje cuando el dispositivo está enviado información.
void mostrarEnviandoDisplay() {
  lcd.setCursor(0, 1);
  lcd.print("                    ");
  lcd.setCursor(0, 2);
  lcd.print(" Enviando datos...  ");
  lcd.setCursor(0, 3);
  lcd.print("                    ");
}

// Esta función deja en blanco todo el display salvo la primera línea.
void borraDisplay() {
  lcd.setCursor(0, 1);
  lcd.print("                    ");
  lcd.setCursor(0, 2);
  lcd.print("                    ");
  lcd.setCursor(0, 3);
  lcd.print("                    ");
}

// esta función se encarga de iniciar el módulo ESP (wifi)
void iniciarWifi() {
  WiFi.init(&esp8266); // inicio Wifi

  //intentar iniciar el modulo ESP
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("Modulo no presente. Reinicie el Arduino y el ESP01 (Quite el cable que va de CH_PD a 3.3V y vuelvalo a colocar)");
    while (true); // no hemos podido iniciar el wifi, nos quedamos aquí hasta que nos reinien el dispositivo
  }

  // Al llegar aquí hay conexión. Intenta conectar a la red wifi concreta que se ha configurado
  while ( status != WL_CONNECTED) {
    Serial.print("Intentando conectar a la red WiFi: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, pass);
  }
}

// esta función se encarga de enviar mediante el servicio web de IFTTT los datos para que acaben reflejados
// en la hoja de Google
void consumirServicio(String evento, int valor1, int valor2, int valor3, int valor4, String valor5) {
  Serial.println("Iniciando conexion..."); // Informamos por el monitor serie
  if (client.connect(host, 80)) { // Intentamos la conexión
    Serial.println("Conectado al servidor");

    // Construimos la URL
    String url = "/trigger/";
    url += evento;
    url += "/with/key/";
    url += key;
    if (valor1 >= 0) {
      url += "?value1=";
      url += valor1;
    }
    if (valor2 >= 0) {
      url += "&value2=";
      url += valor2;
    }
    if (valor3 >= 0) {
      url += "&value3=";
      url += valor3;
    }
    if (valor4 >= 0) { // TRUCO: como IFTTT sólo permite 3 valores, enviamos más valores en el mismo parámetro "valor3",
      url += "|||";    //        separando los valores mediante tres barras horizontales |||
      url += valor4;   //        Eso en la hoja Google se va a interpretar como que van en otra columna
    }
    /*if(valor5 != ""){
      url += "|||";
      url += valor5;
      }*/

    Serial.print("Solicitando URL: "); // mostramos por el monitor serie la URL que vamos a utilizar
    Serial.println(url);

    // Realizamos la petición GET
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Connection: close\r\n\r\n");

    // Leemos la respuesta y la pintamos en pantalla
    while (client.available()) {
      char c = client.read();
      Serial.write(c);
    }

    Serial.println(); // espaciamos un poco la salida por el monitor serie

    // Desconexion
    if (client.connected()) {
      Serial.println();
      Serial.println("Desconectando del servidor...");
      client.flush();
      client.stop();
    }
  }
}

// Esta función emite un tono por el zumbador activo.
// Podemos variar el tono enviando un retardo mayor o menor
void tono(int retardo) {
  unsigned char i;
  for (i = 0; i < 100; i++)
  {
    digitalWrite(PIN_ZUMBADOR, HIGH);
    delay(retardo);//wait for 2ms
    digitalWrite(PIN_ZUMBADOR, LOW);
    delay(retardo);//wait for 2ms
  }
}

// esta función muesta información por pantalla del estado wifi. Interesante si se quieren depurar errores.
//  void verEstadoWifi()
//{
//  // SSID al que nos hemos conectado
//  Serial.print("SSID: ");
//  Serial.println(WiFi.SSID());
//
//  // la IP asignada
//  IPAddress ip = WiFi.localIP();
//  Serial.print("IP: ");
//  Serial.println(ip);
//
//  // fuerza de la señal
//  long rssi = WiFi.RSSI();
//  Serial.print("Señar recibida (RSSI):");
//  Serial.print(rssi);
//  Serial.println(" dBm");
//}
