#include <ESP8266WiFi.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Configurações do Wi-Fi (2.4 GHz)
const char* ssid = "A35 de Karine";
const char* password = "ifgtafacil";

// Configurações de Fuso Horário (BRT = UTC-3)
const char* NTP_SERVER = "time1.google.com"; 
const long GMT_OFFSET_SEC = -3 * 3600; 
const int DAYLIGHT_OFFSET_SEC = 0;     

// Mapeamento dos Pinos I2C customizados
#define OLED_SDA 14 // GPIO14 -> Pino D5
#define OLED_SCL 12 // GPIO12 -> Pino D6

// Configurações do Display OLED SSD1306 (128x64)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(115200);
  delay(500);

  // Inicializa a comunicação I2C com os pinos D5 (SDA) e D6 (SCL)
  Wire.begin(OLED_SDA, OLED_SCL);

  // Inicializa o Display OLED (Endereço I2C 0x3C padrão)
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Falha ao inicializar o display SSD1306"));
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Iniciando...");
    display.display();
  }

  // Conexão Wi-Fi
  Serial.print("Conectando a ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("Wi-Fi conectado!");
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());

  // Configuração do horário via NTP
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  Serial.println("Aguardando sincronização do horário NTP...");

  // Espera pela sincronização do horário
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nHorário sincronizado com sucesso!");
}

void loop() {
  struct tm timeinfo;

  if (getLocalTime(&timeinfo)) {
    // Formatação de data e hora
    char dataStr[12];
    char horaStr[10];

    strftime(dataStr, sizeof(dataStr), "%d/%m/%Y", &timeinfo);
    strftime(horaStr, sizeof(horaStr), "%H:%M:%S", &timeinfo);

    // 1. Exibição no Monitor Serial
    Serial.print("Data: ");
    Serial.print(dataStr);
    Serial.print(" | Hora: ");
    Serial.println(horaStr);

    // 2. Exibição no Display OLED
    display.clearDisplay();
    
    // Cabeçalho
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Relogio NTP Wi-Fi");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // Data
    display.setCursor(15, 20);
    display.setTextSize(1);
    display.print("Data: ");
    display.println(dataStr);

    // Hora
    display.setCursor(15, 40);
    display.setTextSize(2);
    display.println(horaStr);

    display.display();
  } else {
    Serial.println("Falha ao obter o horário.");
  }

  delay(1000);
}
