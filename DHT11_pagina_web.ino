#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// Wi-Fi
const char* ssid = "DLS's Wi-Fi";
const char* password = "12345678";

// NTP Google
const char* NTP_SERVER = "time1.google.com";
const long GMT_OFFSET_SEC = -3 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

// Sensor DHT - Certifique-se de que o cabo DATA esta no GPIO 4
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Display OLED 0.42" (ESP32-C3 Integrado)
#define OLED_SDA 5
#define OLED_SCL 6
#define OLED_RST 10

#define SCREEN_WIDTH 72
#define SCREEN_HEIGHT 40

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);
WebServer server(80);

unsigned long ultimoTempoAtualizacao = 0;
const long intervalo = 2000;

float temp = 0.0;
float umid = 0.0;

String getJsonData() {
  struct tm timeinfo;
  char dataStr[12] = "--/--/----";
  char horaStr[10] = "00:00:00";

  if (getLocalTime(&timeinfo)) {
    strftime(dataStr, sizeof(dataStr), "%d/%m/%Y", &timeinfo);
    strftime(horaStr, sizeof(horaStr), "%H:%M:%S", &timeinfo);
  }

  String json = "{";
  json += "\"data\":\"" + String(dataStr) + "\",";
  json += "\"hora\":\"" + String(horaStr) + "\",";
  json += "\"temp\":\"" + String(temp, 1) + "\",";
  json += "\"umid\":\"" + String(umid, 1) + "\"";
  json += "}";
  return json;
}

void handleRoot() {
  String html = "<!DOCTYPE html><html lang='pt-br'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>ESP32-C3 Estacao</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; background-color: #121212; color: #fff; text-align: center; margin-top: 5%; }";
  html += ".card { background: #1e1e1e; display: inline-block; padding: 25px; border-radius: 12px; width: 280px; }";
  html += "h1 { color: #00adb5; font-size: 1.4em; }";
  html += ".time { font-size: 2em; font-weight: bold; margin: 10px 0; }";
  html += ".sensor { font-size: 1.1em; color: #ff9800; margin-top: 15px; }";
  html += ".val { font-size: 1.6em; font-weight: bold; }";
  html += "</style></head><body>";
  html += "<div class='card'><h1>ESP32-C3 Estacao</h1>";
  html += "<div>Data: <span id='data'>--/--/----</span></div>";
  html += "<div class='time'><span id='hora'>00:00:00</span></div>";
  html += "<hr style='border:0.5px solid #333;'>";
  html += "<div class='sensor'>Temperatura</div><div class='val'><span id='temp'>--</span> &deg;C</div>";
  html += "<div class='sensor'>Umidade</div><div class='val'><span id='umid'>--</span> %</div>";
  html += "</div><script>";
  html += "function atualizar() {";
  html += "  fetch('/api/data').then(r => r.json()).then(d => {";
  html += "    document.getElementById('data').innerText = d.data;";
  html += "    document.getElementById('hora').innerText = d.hora;";
  html += "    document.getElementById('temp').innerText = d.temp;";
  html += "    document.getElementById('umid').innerText = d.umid;";
  html += "  });";
  html += "}";
  html += "setInterval(atualizar, 2000); atualizar();";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

void handleDataApi() {
  server.send(200, "application/json", getJsonData());
}

void setup() {
  // Inicialização Serial para USB CDC do ESP32-C3
  Serial.begin(115200);
  delay(2000); // Aguarda estabilização da USB Nativa

  Serial.println("\n--- Inicializando ESP32-C3 ---");

  // Reset Físico do OLED Integrado
  if (OLED_RST != -1) {
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);
    delay(50);
    digitalWrite(OLED_RST, HIGH);
    delay(50);
  }

  dht.begin();
  Wire.begin(OLED_SDA, OLED_SCL);

  // Inicialização do OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Erro ao conectar no OLED!");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Conectando...");
    display.display();
  }

  // Conexão Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Conectando Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi Conectado!");
  Serial.print("IP: http://");
  Serial.println(WiFi.localIP());

  // NTP
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
  }
  Serial.println("Horario NTP obtido com sucesso!");

  // WebServer
  server.on("/", handleRoot);
  server.on("/api/data", handleDataApi);
  server.begin();
}

void loop() {
  server.handleClient();

  unsigned long tempoAtual = millis();
  if (tempoAtual - ultimoTempoAtualizacao >= intervalo) {
    ultimoTempoAtualizacao = tempoAtual;

    // Leitura segura do DHT
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      temp = t;
      umid = h;
    }

    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char horaMinStr[6];
      char horaSecStr[10];
      char dataStr[12];

      strftime(horaMinStr, sizeof(horaMinStr), "%H:%M", &timeinfo);
      strftime(horaSecStr, sizeof(horaSecStr), "%H:%M:%S", &timeinfo);
      strftime(dataStr, sizeof(dataStr), "%d/%m/%Y", &timeinfo);

      // Print seguro na Serial
      Serial.printf("[%s] Temp: %.1f C | Umid: %.1f %%\n", horaSecStr, temp, umid);

      // --- Layout Otimizado para Display 0.42" (72x40) ---
      // --- No loop(), substitua o bloco do display por este: ---
      display.clearDisplay();

      // 1. Hora (HH:MM) - Tamanho 2
      display.setTextSize(2);
      // Adicionamos +28 no X para compensar a margem da tela 0.42"
      display.setCursor(28 + 6, 2); 
      display.print(horaMinStr);

      // Linha divisória centralizada (72px de largura com offset 28)
      display.drawLine(28, 19, 28 + 72, 19, SSD1306_WHITE);

      // 2. Temperatura (Ex: T:25.4C) - Tamanho 1
      display.setTextSize(1);
      display.setCursor(28 + 2, 23);
      display.printf("T:%.1f C", temp);

      // 3. Umidade (Ex: U:60.0%) - Tamanho 1
      display.setCursor(28 + 2, 32);
      display.printf("U:%.1f %%", umid);

      display.display();
    }
  }
}
