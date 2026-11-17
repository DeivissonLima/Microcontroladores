#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

// --- CONFIGURAÇÕES DE TELA E I2C ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 14 // GPIO14 -> Pino D5
#define OLED_SCL 12 // GPIO12 -> Pino D6

// --- CONFIGURAÇÕES DE HARDWARE ---
#define RELAY_PIN    5  // GPIO5  -> Pino D1
#define BUTTON_PIN   13 // GPIO13 -> Pino D7
#define RELAY_ACTIVE_LOW false // Configurado para disparo em Nível Lógico Alto (NO)

// --- REDE WI-FI OFFLINE ---
const char* AP_SSID = "Timer_ESP8266_AP";
const char* AP_PASS = "12345678";

// --- ESTRUTURA DE AGENDAMENTO ---
struct ScheduleRule {
  bool enabled;
  uint8_t startHour;
  uint8_t startMin;
  uint8_t endHour;
  uint8_t endMin;
  uint8_t daysMask; // Bitmask: Bit 0=Dom, Bit 1=Seg, ..., Bit 6=Sáb
};

#define MAX_RULES 10
ScheduleRule rules[MAX_RULES];

// --- OBJETOS GLOBAIS ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
ESP8266WebServer server(80);
Preferences preferences;

bool relayState = false;
bool manualOverride = false;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
unsigned long lastOLEDUpdate = 0;
unsigned long lastTimeSave = 0;

const char* daysOfWeek[] = {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab"};

// --- PROTÓTIPOS ---
void handleRoot();
void handleGetStatus();
void handleSetTime();
void handleSaveSchedule();
void handleToggleRelay();
void updateRelayLogic();
void saveRulesToFlash();
void loadRulesFromFlash();
void updateOLED();
void setSoftwareTime(int year, int month, int day, int hour, int minute, int second);
struct tm getSoftwareTime();
void saveTimeToFlash();
void loadTimeFromFlash();
bool isRuleActive(const ScheduleRule& r, const struct tm& now);

// --- INTERFACE WEB EMBUTIDA EM PROGMEM ---
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Painel Temporizador ESP8266</title>
  <style>
    :root {
      --bg: #0f172a;
      --card: #1e293b;
      --accent: #3b82f6;
      --text: #f8fafc;
      --text-sub: #94a3b8;
      --success: #22c55e;
      --danger: #ef4444;
      --border: #334155;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: system-ui, -apple-system, sans-serif; }
    body { background: var(--bg); color: var(--text); padding: 16px; max-width: 600px; margin: 0 auto; }
    h1 { text-align: center; font-size: 1.5rem; margin-bottom: 16px; color: var(--accent); }
    .card { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 16px; margin-bottom: 16px; }
    .card-title { font-size: 1.1rem; font-weight: 600; margin-bottom: 12px; display: flex; justify-content: space-between; align-items: center; }
    .status-badge { padding: 4px 12px; border-radius: 20px; font-size: 0.85rem; font-weight: bold; }
    .status-on { background: rgba(34, 197, 94, 0.2); color: var(--success); border: 1px solid var(--success); }
    .status-off { background: rgba(239, 68, 68, 0.2); color: var(--danger); border: 1px solid var(--danger); }
    .time-display { font-size: 2rem; font-weight: 700; text-align: center; margin: 10px 0; font-family: monospace; }
    .btn { background: var(--accent); color: white; border: none; padding: 10px 16px; border-radius: 8px; font-weight: 600; cursor: pointer; width: 100%; transition: 0.2s; margin-top: 6px; }
    .btn:hover { opacity: 0.9; }
    .btn-toggle { background: #6366f1; padding: 14px; font-size: 1rem; }
    .btn-sync { background: #0284c7; }
    .rule-item { background: #0f172a; border: 1px solid var(--border); border-radius: 8px; padding: 12px; margin-bottom: 10px; }
    .rule-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }
    .days-grid { display: grid; grid-template-columns: repeat(7, 1fr); gap: 4px; margin: 8px 0; }
    .day-btn { background: var(--card); border: 1px solid var(--border); color: var(--text-sub); padding: 6px 0; border-radius: 4px; font-size: 0.75rem; cursor: pointer; text-align: center; }
    .day-btn.selected { background: var(--accent); color: white; border-color: var(--accent); }
    .time-inputs { display: flex; gap: 8px; align-items: center; margin-top: 8px; }
    .time-inputs input[type="time"] { background: var(--card); border: 1px solid var(--border); color: var(--text); padding: 6px; border-radius: 6px; flex: 1; text-align: center; }
    .switch { position: relative; display: inline-block; width: 44px; height: 24px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #475569; transition: .3s; border-radius: 24px; }
    .slider:before { position: absolute; content: ""; height: 18px; width: 18px; left: 3px; bottom: 3px; background-color: white; transition: .3s; border-radius: 50%; }
    input:checked + .slider { background-color: var(--success); }
    input:checked + .slider:before { transform: translateX(20px); }
  </style>
</head>
<body>

  <h1>Rel&eacute; Temporizador ESP8266</h1>

  <div class="card">
    <div class="card-title">
      <span>Status do Rel&eacute;</span>
      <span id="relayStatus" class="status-badge status-off">DESLIGADO</span>
    </div>
    <p style="font-size: 0.85rem; color: var(--text-sub); text-align: center;">Modo: <strong id="modeStatus">Autom&aacute;tico</strong></p>
    <button class="btn btn-toggle" onclick="toggleRelay()">Ligar / Desligar Manual</button>
  </div>

  <div class="card">
    <div class="card-title">Rel&oacute;gio RTC (Emulado SW)</div>
    <div class="time-display" id="rtcClock">--:--:--</div>
    <div style="text-align: center; color: var(--text-sub); font-size: 0.9rem; margin-bottom: 10px;" id="rtcDate">--/--/----</div>
    <button class="btn btn-sync" onclick="syncBrowserTime()">Sincronizar com Hor&aacute;rio do Celular</button>
  </div>

  <div class="card">
    <div class="card-title">Agendamentos Semanais (At&eacute; 10)</div>
    <div id="rulesContainer"></div>
    <button class="btn" onclick="saveAllSchedules()" style="background: var(--success); margin-top: 12px; font-size: 1rem;">Salvar Programa&ccedil;&atilde;o</button>
  </div>

  <script>
    let rulesData = [];
    const dayNames = ["Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab"];

    async function fetchStatus() {
      try {
        const res = await fetch('/status');
        const data = await res.json();
        
        document.getElementById('rtcClock').innerText = data.time;
        document.getElementById('rtcDate').innerText = data.date;
        
        const badge = document.getElementById('relayStatus');
        badge.innerText = data.relay ? "LIGADO" : "DESLIGADO";
        badge.className = "status-badge " + (data.relay ? "status-on" : "status-off");
        
        document.getElementById('modeStatus').innerText = data.override ? "Manual (Override)" : "Autom&aacute;tico (Agenda)";

        if (!window.rulesLoaded) {
          rulesData = data.rules;
          renderRules();
          window.rulesLoaded = true;
        }
      } catch (e) {
        console.error("Erro ao buscar status", e);
      }
    }

    function renderRules() {
      const container = document.getElementById('rulesContainer');
      container.innerHTML = '';
      
      for (let i = 0; i < 10; i++) {
        const r = rulesData[i] || { enabled: false, startHour: 8, startMin: 0, endHour: 18, endMin: 0, daysMask: 127 };
        
        const startStr = String(r.startHour).padStart(2, '0') + ':' + String(r.startMin).padStart(2, '0');
        const endStr = String(r.endHour).padStart(2, '0') + ':' + String(r.endMin).padStart(2, '0');

        let daysHtml = '';
        for (let d = 0; d < 7; d++) {
          const isSelected = (r.daysMask & (1 << d)) !== 0;
          daysHtml += `<div class="day-btn ${isSelected ? 'selected' : ''}" onclick="toggleDay(${i}, ${d})">${dayNames[d]}</div>`;
        }

        container.innerHTML += `
          <div class="rule-item">
            <div class="rule-header">
              <strong>Per&iacute;odo #${i + 1}</strong>
              <label class="switch">
                <input type="checkbox" id="rule_en_${i}" ${r.enabled ? 'checked' : ''} onchange="rulesData[${i}].enabled = this.checked">
                <span class="slider"></span>
              </label>
            </div>
            <div class="days-grid">${daysHtml}</div>
            <div class="time-inputs">
              <label>In&iacute;cio:</label>
              <input type="time" id="start_${i}" value="${startStr}" onchange="updateTimeValue(${i})">
              <label>Fim:</label>
              <input type="time" id="end_${i}" value="${endStr}" onchange="updateTimeValue(${i})">
            </div>
          </div>
        `;
      }
    }

    function toggleDay(ruleIndex, dayIndex) {
      rulesData[ruleIndex].daysMask ^= (1 << dayIndex);
      renderRules();
    }

    function updateTimeValue(i) {
      const startVal = document.getElementById(`start_${i}`).value.split(':');
      const endVal = document.getElementById(`end_${i}`).value.split(':');
      rulesData[i].startHour = parseInt(startVal[0]) || 0;
      rulesData[i].startMin = parseInt(startVal[1]) || 0;
      rulesData[i].endHour = parseInt(endVal[0]) || 0;
      rulesData[i].endMin = parseInt(endVal[1]) || 0;
    }

    async function toggleRelay() {
      await fetch('/toggle_relay', { method: 'POST' });
      fetchStatus();
    }

    async function syncBrowserTime() {
      const now = new Date();
      const body = new URLSearchParams({
        year: now.getFullYear(),
        month: now.getMonth() + 1,
        day: now.getDate(),
        hour: now.getHours(),
        minute: now.getMinutes(),
        second: now.getSeconds()
      });
      await fetch('/set_time', { method: 'POST', body: body });
      alert("Hor&aacute;rio sincronizado com sucesso!");
      fetchStatus();
    }

    async function saveAllSchedules() {
      for(let i=0; i<10; i++) updateTimeValue(i);
      await fetch('/save_schedule', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(rulesData)
      });
      alert("Programa&ccedil;&atilde;o salva com sucesso!");
      fetchStatus();
    }

    setInterval(fetchStatus, 1000);
    fetchStatus();
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);

  // 1. Iniciar o Ponto de Acesso Wi-Fi imediatamente
  WiFi.softAP(AP_SSID, AP_PASS);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? LOW : HIGH); // Relé inicia desligado
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // 2. Barramento I2C apenas para o OLED
  Wire.begin(OLED_SDA, OLED_SCL);

  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 20);
    display.println("RTC Emulado OK!");
    display.println(AP_SSID);
    display.display();
  }

  // 3. Inicialização do armazenamento não volátil
  preferences.begin("timer_app", false);
  loadTimeFromFlash();
  loadRulesFromFlash();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleGetStatus);
  server.on("/set_time", HTTP_POST, handleSetTime);
  server.on("/save_schedule", HTTP_POST, handleSaveSchedule);
  server.on("/toggle_relay", HTTP_POST, handleToggleRelay);

  server.begin();
}

void loop() {
  server.handleClient();

  // Leitura com Debouncing do Botão Físico
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    static bool buttonState = HIGH;
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        relayState = !relayState;
        manualOverride = true;
        digitalWrite(RELAY_PIN, relayState ? (RELAY_ACTIVE_LOW ? LOW : HIGH) : (RELAY_ACTIVE_LOW ? HIGH : LOW));
      }
    }
  }
  lastButtonState = reading;

  updateRelayLogic();

  // Atualização da Tela OLED (a cada 1s)
  if (millis() - lastOLEDUpdate >= 1000) {
    lastOLEDUpdate = millis();
    updateOLED();
  }

  // Backup automático do horário para a memória Flash a cada 60s
  if (millis() - lastTimeSave >= 60000) {
    lastTimeSave = millis();
    saveTimeToFlash();
  }
}

// --- FUNÇÕES DE EMULAÇÃO DE RTC VIA SOFTWARE ---
void setSoftwareTime(int year, int month, int day, int hour, int minute, int second) {
  struct tm tm;
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
  tm.tm_isdst = -1;

  time_t t = mktime(&tm);
  struct timeval tv = { t, 0 };
  settimeofday(&tv, NULL);
}

struct tm getSoftwareTime() {
  time_t now = time(NULL);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  return timeinfo;
}

void saveTimeToFlash() {
  time_t now = time(NULL);
  preferences.putULong("last_time", (unsigned long)now);
}

void loadTimeFromFlash() {
  if (preferences.isKey("last_time")) {
    unsigned long savedTime = preferences.getULong("last_time", 0);
    if (savedTime > 0) {
      struct timeval tv = { (time_t)savedTime, 0 };
      settimeofday(&tv, NULL);
    }
  } else {
    setSoftwareTime(2026, 1, 1, 0, 0, 0);
  }
}

// --- LÓGICA DE CONTROLE E INTERFACE ---
void updateOLED() {
  struct tm now = getSoftwareTime();
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("IP: ");
  display.print(WiFi.softAPIP());

  char timeStr[9];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", now.tm_hour, now.tm_min, now.tm_sec);
  display.setTextSize(2);
  display.setCursor(16, 16);
  display.print(timeStr);

  char dateStr[22];
  snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d (%s)", 
           now.tm_mday, now.tm_mon + 1, now.tm_year + 1900, daysOfWeek[now.tm_wday]);
  display.setTextSize(1);
  display.setCursor(0, 38);
  display.print(dateStr);

  display.drawFastHLine(0, 49, 128, SSD1306_WHITE);

  display.setCursor(0, 54);
  display.print("RELE: ");
  display.print(relayState ? "LIGADO" : "DESLIGADO");

  display.setCursor(100, 54);
  display.print(manualOverride ? "[M]" : "[A]");

  display.display();
}

bool isRuleActive(const ScheduleRule& r, const struct tm& now) {
  if (!r.enabled) return false;

  uint8_t dayOfWeek = now.tm_wday; // 0 = Domingo
  if (!(r.daysMask & (1 << dayOfWeek))) return false;

  uint16_t currentMin = now.tm_hour * 60 + now.tm_min;
  uint16_t startMin = r.startHour * 60 + r.startMin;
  uint16_t endMin = r.endHour * 60 + r.endMin;

  if (startMin <= endMin) {
    return (currentMin >= startMin && currentMin < endMin);
  } else {
    return (currentMin >= startMin || currentMin < endMin);
  }
}

void updateRelayLogic() {
  struct tm now = getSoftwareTime();
  bool scheduleDemandsOn = false;

  for (int i = 0; i < MAX_RULES; i++) {
    if (isRuleActive(rules[i], now)) {
      scheduleDemandsOn = true;
      break;
    }
  }

  if (!manualOverride) {
    if (relayState != scheduleDemandsOn) {
      relayState = scheduleDemandsOn;
      digitalWrite(RELAY_PIN, relayState ? (RELAY_ACTIVE_LOW ? LOW : HIGH) : (RELAY_ACTIVE_LOW ? HIGH : LOW));
    }
  }
}

// --- ROTAS DO SERVIDOR WEB ---
void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleGetStatus() {
  struct tm now = getSoftwareTime();
  char timeStr[10], dateStr[12];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", now.tm_hour, now.tm_min, now.tm_sec);
  snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", now.tm_mday, now.tm_mon + 1, now.tm_year + 1900);

  String json = "{";
  json += "\"time\":\"" + String(timeStr) + "\",";
  json += "\"date\":\"" + String(dateStr) + "\",";
  json += "\"relay\":" + String(relayState ? "true" : "false") + ",";
  json += "\"override\":" + String(manualOverride ? "true" : "false") + ",";
  json += "\"rules\":[";

  for (int i = 0; i < MAX_RULES; i++) {
    json += "{";
    json += "\"enabled\":" + String(rules[i].enabled ? "true" : "false") + ",";
    json += "\"startHour\":" + String(rules[i].startHour) + ",";
    json += "\"startMin\":" + String(rules[i].startMin) + ",";
    json += "\"endHour\":" + String(rules[i].endHour) + ",";
    json += "\"endMin\":" + String(rules[i].endMin) + ",";
    json += "\"daysMask\":" + String(rules[i].daysMask);
    json += "}";
    if (i < MAX_RULES - 1) json += ",";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void handleSetTime() {
  if (server.hasArg("year")) {
    int year = server.arg("year").toInt();
    int month = server.arg("month").toInt();
    int day = server.arg("day").toInt();
    int hour = server.arg("hour").toInt();
    int minute = server.arg("minute").toInt();
    int second = server.arg("second").toInt();

    setSoftwareTime(year, month, day, hour, minute, second);
    saveTimeToFlash();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleToggleRelay() {
  relayState = !relayState;
  manualOverride = true;
  digitalWrite(RELAY_PIN, relayState ? (RELAY_ACTIVE_LOW ? LOW : HIGH) : (RELAY_ACTIVE_LOW ? HIGH : LOW));
  server.send(200, "text/plain", "OK");
}

void handleSaveSchedule() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    
    int ruleIdx = 0;
    int pos = 0;

    while (ruleIdx < MAX_RULES && (pos = body.indexOf('{', pos)) != -1) {
      int endPos = body.indexOf('}', pos);
      if (endPos == -1) break;
      String item = body.substring(pos, endPos);

      rules[ruleIdx].enabled = (item.indexOf("\"enabled\":true") != -1);
      
      auto getVal = [&](String key) -> int {
        int kpos = item.indexOf(key);
        if (kpos == -1) return 0;
        int vpos = item.indexOf(':', kpos) + 1;
        return item.substring(vpos).toInt();
      };

      rules[ruleIdx].startHour = getVal("\"startHour\"");
      rules[ruleIdx].startMin = getVal("\"startMin\"");
      rules[ruleIdx].endHour = getVal("\"endHour\"");
      rules[ruleIdx].endMin = getVal("\"endMin\"");
      rules[ruleIdx].daysMask = getVal("\"daysMask\"");

      ruleIdx++;
      pos = endPos + 1;
    }

    manualOverride = false;
    saveRulesToFlash();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void saveRulesToFlash() {
  preferences.putBytes("rules", rules, sizeof(rules));
}

void loadRulesFromFlash() {
  if (preferences.isKey("rules")) {
    preferences.getBytes("rules", rules, sizeof(rules));
  } else {
    for (int i = 0; i < MAX_RULES; i++) {
      rules[i].enabled = false;
      rules[i].startHour = 8;
      rules[i].startMin = 0;
      rules[i].endHour = 18;
      rules[i].endMin = 0;
      rules[i].daysMask = 127;
    }
    saveRulesToFlash();
  }
}
