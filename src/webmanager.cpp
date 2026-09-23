#include "Arduino.h"
#include "webmanager.h"
#include <ElegantOTA.h>
#include "config.h"
#include "marstek.h"

extern Marstek* marstek;

WebServer webServer(80);

static const char* _clientId = nullptr;

static void sendCommandResult(bool ok) {
  webServer.send(ok ? 200 : 500, "application/json",
                 ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void setupWebServer(const char* clientId) {
  _clientId = clientId;

  webServer.on("/", []() {
    unsigned long uptimeSec = millis() / 1000;
    unsigned long h = uptimeSec / 3600;
    unsigned long m = (uptimeSec % 3600) / 60;
    unsigned long s = uptimeSec % 60;
    char uptimeStr[12];
    snprintf(uptimeStr, sizeof(uptimeStr), "%02lu:%02lu:%02lu", h, m, s);
    String mode = marstek ? marstek->getControlModeString() : "unknown";
    const MarstekTelemetry* t = marstek ? &marstek->getTelemetry() : nullptr;

    String html = F(
      "<!DOCTYPE html>"
      "<html lang=\"en\"><head>"
      "<meta charset=\"UTF-8\">"
      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
      "<title>Marstek ModBus Gateway</title>"
      "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/@picocss/pico@2/css/pico.min.css\">"
      "<script src=\"https://unpkg.com/htmx.org@2.0.4\"></script>"
      "</head><body>"
      "<main class=\"container\">"
      "<h1>Marstek ModBus Gateway</h1>"
      "<article>"
      "<table>"
      "<tbody>"
      "<tr><th>Client ID</th><td id=\"clientId\">");
    html += _clientId;
    html += F("</td></tr>"
      "<tr><th>Uptime</th><td id=\"uptime\">");
    html += uptimeStr;
    html += F("</td></tr>"
      "<tr><th>Modbus</th><td id=\"healthy\">");
    html += (marstek && marstek->isHealthy()) ? "OK" : "Error";
    html += F("</td></tr>"
      "<tr><th>Control mode</th><td id=\"mode\">");
    html += mode;
    html += F("</td></tr>"
      "<tr><th>Battery</th><td id=\"battery\">");
    if (t) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%.2f V / %.2f A / %ld W",
               t->batteryVoltage, t->batteryCurrent, (long)t->batteryPower);
      html += buf;
    }
    html += F("</td></tr>"
      "<tr><th>AC</th><td id=\"ac\">");
    if (t) {
      char buf[64];
      snprintf(buf, sizeof(buf), "%.1f V / %.2f A / %ld W / %.2f Hz",
               t->acVoltage, t->acCurrent, (long)t->acPower, t->acFrequency);
      html += buf;
    }
    html += F("</td></tr>"
      "<tr><th>Versions</th><td id=\"versions\">");
    if (t) {
      char buf[32];
      snprintf(buf, sizeof(buf), "SW %.2f / FW %u",
               t->softwareVersion / 100.0f, t->firmwareVersion);
      html += buf;
    }
    html += F("</td></tr>"
      "<tr><th>MAC</th><td id=\"mac\">");
    if (t) {
      html += t->mac;
    }
    html += F("</td></tr>"
      "</tbody>"
      "</table>"
      "<div hx-get=\"/status\" hx-trigger=\"every 2s\" hx-swap=\"none\" hx-on::after-request=\""
        "var d=JSON.parse(event.detail.xhr.responseText);"
        "document.getElementById('uptime').textContent=d.uptime;"
        "document.getElementById('healthy').textContent=d.healthy?'OK':'Error';"
        "document.getElementById('mode').textContent=d.mode;"
        "document.getElementById('battery').textContent=d.battery;"
        "document.getElementById('ac').textContent=d.ac;"
        "document.getElementById('versions').textContent=d.versions;"
        "document.getElementById('mac').textContent=d.mac;"
      "\"></div>"
      "</article>"
      "<article>"
      "<h2>Controls</h2>"
      "<label for=\"watts\">Power (W, 0-2500)</label>"
      "<input type=\"number\" id=\"watts\" name=\"watts\" min=\"0\" max=\"2500\" value=\"800\">"
      "<div style=\"display:flex;gap:1rem;\">"
      "<button hx-get=\"/charge\" hx-include=\"#watts\" hx-swap=\"none\">Charge</button>"
      "<button hx-get=\"/discharge\" hx-include=\"#watts\" hx-swap=\"none\" class=\"secondary\">Discharge</button>"
      "<button hx-get=\"/stop\" hx-swap=\"none\" class=\"contrast\">Stop</button>"
      "</div>"
      "</article>"
      "</main>"
      "</body></html>"
    );
    webServer.send(200, "text/html", html);
  });

  webServer.on("/status", []() {
    unsigned long uptimeSec = millis() / 1000;
    unsigned long h = uptimeSec / 3600;
    unsigned long m = (uptimeSec % 3600) / 60;
    unsigned long s = uptimeSec % 60;
    char uptimeStr[12];
    snprintf(uptimeStr, sizeof(uptimeStr), "%02lu:%02lu:%02lu", h, m, s);

    char battery[48] = "";
    char ac[64] = "";
    char versions[32] = "";
    String mode = "unknown";
    String mac;
    bool healthy = false;
    if (marstek) {
      const MarstekTelemetry& t = marstek->getTelemetry();
      healthy = marstek->isHealthy();
      mode = marstek->getControlModeString();
      mac = t.mac;
      snprintf(battery, sizeof(battery), "%.2f V / %.2f A / %ld W",
               t.batteryVoltage, t.batteryCurrent, (long)t.batteryPower);
      snprintf(ac, sizeof(ac), "%.1f V / %.2f A / %ld W / %.2f Hz",
               t.acVoltage, t.acCurrent, (long)t.acPower, t.acFrequency);
      snprintf(versions, sizeof(versions), "SW %.2f / FW %u",
               t.softwareVersion / 100.0f, t.firmwareVersion);
    }

    String json = "{\"clientId\":\"";
    json += _clientId;
    json += "\",\"uptime\":\"";
    json += uptimeStr;
    json += "\",\"healthy\":";
    json += healthy ? "true" : "false";
    json += ",\"mode\":\"";
    json += mode;
    json += "\",\"battery\":\"";
    json += battery;
    json += "\",\"ac\":\"";
    json += ac;
    json += "\",\"versions\":\"";
    json += versions;
    json += "\",\"mac\":\"";
    json += mac;
    json += "\"}";
    webServer.send(200, "application/json", json);
  });

  webServer.on("/charge", []() {
    int watts = webServer.hasArg("watts") ? webServer.arg("watts").toInt() : -1;
    bool ok = marstek && watts >= 0 && watts <= 2500 &&
              marstek->setChargePower((uint16_t)watts);
    sendCommandResult(ok);
  });

  webServer.on("/discharge", []() {
    int watts = webServer.hasArg("watts") ? webServer.arg("watts").toInt() : -1;
    bool ok = marstek && watts >= 0 && watts <= 2500 &&
              marstek->setDischargePower((uint16_t)watts);
    sendCommandResult(ok);
  });

  webServer.on("/stop", []() {
    sendCommandResult(marstek && marstek->stopControl());
  });

  ElegantOTA.begin(&webServer, OTA_USERNAME_STR, OTA_PASSWORD_STR);
  webServer.begin();
  Serial.println("[INIT] HTTP server started");
}

void loopWebServer() {
  webServer.handleClient();
  ElegantOTA.loop();
}
