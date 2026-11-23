#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
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

// Mapeamento dos Pinos I2C
#define OLED_SDA 14 // GPIO14 -> Pino D5
#define OLED_SCL 12 // GPIO12 -> Pino D6

// Configurações do Display OLED SSD1306 (128x64)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ESP8266WebServer server(80);

unsigned long ultimoTempoAtualizacao = 0;
const long intervalo = 1000; // Atualiza display/serial a cada 1 segundo

// Função para formatar a data/hora em JSON para a página web
String getJsonTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "{\"erro\": \"Falha ao obter horario\"}";
  }

  char dataStr[12];
  char horaStr[10];
  strftime(dataStr, sizeof(dataStr), "%d/%m/%Y", &timeinfo);
  strftime(horaStr, sizeof(horaStr), "%H:%M:%S", &timeinfo);

  String json = "{";
  json += "\"data\":\"" + String(dataStr) + "\",";
  json += "\"hora\":\"" + String(horaStr) + "\"";
  json += "}";
  return json;
}

// Rota Principal: HTML da página Web
void handleRoot() {
  String html = "<!DOCTYPE html><html lang='pt-br'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>ESP8266 - Relogio Completo</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; background-color: #121212; color: #ffffff; text-align: center; margin-top: 10%; }";
  html += ".card { background: #1e1e1e; display: inline-block; padding: 30px 50px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.5); }";
  html += "h1 { color: #00adb5; margin-bottom: 20px; }";
  html += ".time { font-size: 2.5em; font-weight: bold; color: #eee; margin: 10px 0; }";
  html += ".date { font-size: 1.2em; color: #888; }";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='card'>";
  html += "<h1>ESP8266 Relogio Web</h1>";
  html += "<div class='date'>Data: <span id='data'>--/--/----</span></div>";
  html += "<div class='time'><span id='hora'>00:00:00</span></div>";
  html += "</div>";
  html += "<script>";
  html += "function atualizarHorario() {";
  html += "  fetch('/api/time')";
  html += "    .then(response => response.json())";
  html += "    .then(data => {";
  html += "      if(!data.erro) {";
  html += "        document.getElementById('data').innerText = data.data;";
  html += "        document.getElementById('hora').innerText = data.hora;";
  html += "      }";
  html += "    });";
  html += "}";
  html += "setInterval(atualizarHorario, 1000);";
  html += "atualizarHorario();";
  html += "</script>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// Rota API para requisições assíncronas do JS
void handleTimeApi() {
  server.send(200, "application/json", getJsonTime());
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // 1. Inicialização do Display OLED I2C (Pinos D5 e D6)
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Falha ao inicializar o display SSD1306"));
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Conectando...");
    display.display();
  }

  // 2. Conexão Wi-Fi
  Serial.print("Conectando a ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi conectado!");
  Serial.print("IP: http://");
  Serial.println(WiFi.localIP());

  // 3. Sincronização de horário NTP
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  Serial.println("Aguardando sincronização NTP...");

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nHorário sincronizado!");

  // 4. Configuração das rotas do Servidor Web
  server.on("/", handleRoot);
  server.on("/api/time", handleTimeApi);
  server.begin();
  Serial.println("Servidor Web iniciado!");
}

void loop() {
  // Mantém o servidor web atendendo às requisições do navegador
  server.handleClient();

  // Atualização periódica do Display e do Monitor Serial sem travar a execução (usando millis)
  unsigned long tempoAtual = millis();
  if (tempoAtual - ultimoTempoAtualizacao >= intervalo) {
    ultimoTempoAtualizacao = tempoAtual;

    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char dataStr[12];
      char horaStr[10];

      strftime(dataStr, sizeof(dataStr), "%d/%m/%Y", &timeinfo);
      strftime(horaStr, sizeof(horaStr), "%H:%M:%S", &timeinfo);

      // Exibição no Monitor Serial
      Serial.print("Data: ");
      Serial.print(dataStr);
      Serial.print(" | Hora: ");
      Serial.println(horaStr);

      // Exibição no Display OLED
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("Relogio ESP8266");
      display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

      display.setCursor(15, 20);
      display.setTextSize(1);
      display.print("Data: ");
      display.println(dataStr);

      display.setCursor(15, 40);
      display.setTextSize(2);
      display.println(horaStr);

      display.display();
    }
  }
}
