#include "web.h"

#include <WebServer.h>
#include <WiFi.h>

#include "app.h"
#include "strava.h"
#include "tz.h"

namespace {

WebServer server(80);
bool running = false;

const char CSS[] PROGMEM = R"CSS(
body{margin:0;background:#000;color:#fff;font:16px/1.45 -apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:460px;margin:0 auto;padding:20px}
h1{font-size:22px;letter-spacing:.04em;text-transform:uppercase;border-bottom:2px solid #e00;padding-bottom:8px;margin:0 0 16px}
h2{font-size:14px;letter-spacing:.08em;text-transform:uppercase;color:#e00;margin:24px 0 8px}
.card{background:#141414;border-radius:10px;padding:14px 16px;margin:10px 0}
.big{font-size:34px;font-weight:700}.dim{color:#999;font-size:14px}
label{display:block;margin:12px 0 4px;color:#ccc}
input[type=text],input[type=number],select{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #333;background:#0b0b0b;color:#fff;font-size:16px}
input[type=range]{width:100%}.row{display:flex;gap:8px;align-items:center}.row input{width:auto}
button,.btn{display:block;width:100%;box-sizing:border-box;padding:12px;margin:10px 0;border:0;border-radius:8px;background:#e00;color:#fff;font-size:16px;font-weight:600;text-align:center;text-decoration:none}
.ghost{background:#222}.warn{background:#400;color:#fbb}
)CSS";

String esc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (char ch : s) {
    switch (ch) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += ch;
    }
  }
  return o;
}

void sendPage(const String& title, const String& body, int code = 200) {
  String h;
  h.reserve(body.length() + 2200);
  h += F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>");
  h += esc(title);
  h += F("</title><style>");
  h += FPSTR(CSS);
  h += F("</style></head><body><main><h1>");
  h += esc(title);
  h += F("</h1>");
  h += body;
  h += F("</main></body></html>");
  server.send(code, "text/html", h);
}

void redirect(const String& to) {
  server.sendHeader("Location", to);
  server.send(302, "text/plain", "");
}

String miles(float m) {
  char b[24];
  ui::formatMiles(b, sizeof b, m / 1609.344f, 1);
  return b;
}

void handleRoot() {
  if (!strava::linked()) return redirect("/link");
  const strava::Totals& t = strava::totals();
  UiData d = app::uiData();
  String b;
  b += F("<div class=card><div class=dim>");
  b += esc(String(d.athlete));
  b += F("</div><div class=big>");
  b += miles(t.meters);
  b += F(" mi</div><div class=dim>");
  b += String(d.year) + " &middot; " + t.runs + (t.runs == 1 ? " run" : " runs");
  uint32_t ago = app::lastSyncAgoS();
  b += ago == UINT32_MAX ? String(" &middot; not synced yet") : " &middot; synced " + String(ago / 60) + " min ago";
  b += F("</div></div><form method=post action=/save>");

  b += F("<h2>Yearly goal</h2><label class=row><input type=radio name=gm value=auto");
  if (app::cfg.goalMi <= 0) b += F(" checked");
  b += F("> Beat last year");
  if (t.lastYear == d.year - 1 && t.lastYearMeters > 0)
    b += " (" + miles(t.lastYearMeters) + " mi in " + t.lastYear + ")";
  b += F("</label><label class=row><input type=radio name=gm value=own");
  if (app::cfg.goalMi > 0) b += F(" checked");
  b += F("> My own goal</label><input type=number name=goal min=1 max=10000 step=1 inputmode=numeric placeholder='Miles this year' value='");
  if (app::cfg.goalMi > 0) b += String((int)app::cfg.goalMi);
  b += F("' onfocus=\"document.querySelector('[value=own]').checked=true\">");

  b += F("<h2>Screen</h2><label>Name on screen</label><input type=text name=name maxlength=30 placeholder='");
  b += esc(strava::athleteName());
  b += F("' value='");
  b += esc(app::cfg.name);
  b += F("'><label>Brightness</label><input type=range name=bright min=20 max=255 value=");
  b += String(app::cfg.bright);
  b += F("><label class=row><input type=checkbox name=walks value=1");
  if (app::cfg.walks) b += F(" checked");
  b += F("> Count walks and hikes too</label><label>Time zone (for the new-year reset)</label><select name=tz>");
  bool matched = false;
  for (const TzEntry& z : TZ_TABLE) {
    if (!z.label) continue;
    bool sel = !matched && app::cfg.tz == z.posix;
    matched = matched || sel;
    b += "<option value='" + String(z.posix) + "'" + (sel ? " selected" : "") + ">" + z.label + "</option>";
  }
  if (!matched) b += "<option value='" + esc(app::cfg.tz) + "' selected>Phone's zone (" + esc(app::cfg.tz) + ")</option>";
  b += F("</select><button>Save</button></form>");

  b += F("<h2>More</h2><form method=post action=/sync><button class=ghost>Update now</button></form>"
         "<a class='btn ghost' href=/link>Link a different Strava account</a>"
         "<form method=post action=/wifi><button class=ghost>Change WiFi</button></form>"
         "<form method=post action=/reset onsubmit=\"return confirm('Erase WiFi, Strava link and settings?')\">"
         "<button class=warn>Erase everything</button></form>"
         "<p class=dim>Next Mile Apparel &middot; DS3design &middot; Data from Strava</p>");
  sendPage("Next Mile Tracker", b);
}

void handleSave() {
  Settings& c = app::cfg;
  c.name = server.arg("name");
  c.name.trim();
  if (server.arg("gm") == "own" && server.arg("goal").toFloat() > 0) c.goalMi = server.arg("goal").toFloat();
  else c.goalMi = 0;
  int br = server.arg("bright").toInt();
  if (br >= 20 && br <= 255) c.bright = br;
  bool walks = server.hasArg("walks");
  String tz = server.arg("tz");
  bool tzChanged = tz.length() && tz != c.tz;
  if (tzChanged) {
    c.tz = tz;
    c.tzManual = true;
    app::applyTz();
  }
  bool resync = tzChanged || walks != c.walks;
  c.walks = walks;
  app::saveSettings();
  app::requestSync(resync);
  redirect("/");
}

// Step 1 of linking: grab the phone's time zone, then bounce to Strava.
void handleLink() {
  if (!strava::configured()) {
    return sendPage("Not ready", "<p>This tracker's firmware is missing its Strava app keys. Contact DS3design.</p>", 500);
  }
  sendPage("Connect Strava",
           F("<p>Opening Strava&hellip;</p><p class=dim>On the Strava page tap <b>Authorize</b>. Leave "
             "&ldquo;View data about your private activities&rdquo; ticked so every run counts.</p>"
             "<a class=btn id=go href=/go>Continue to Strava</a><script>"
             "var z='';try{z=Intl.DateTimeFormat().resolvedOptions().timeZone||''}catch(e){}"
             "location.replace('/go?tz='+encodeURIComponent(z)+'&off='+new Date().getTimezoneOffset())"
             "</script>"));
}

void handleGo() {
  Settings& c = app::cfg;
  if (!c.tzManual && server.hasArg("tz")) {
    String iana = server.arg("tz");
    String posix;
    for (const TzEntry& z : TZ_TABLE) {
      if (iana == z.iana) posix = z.posix;
    }
    if (posix.isEmpty() && server.hasArg("off")) {
      int off = server.arg("off").toInt();  // minutes west of UTC, like POSIX
      char b[24];
      snprintf(b, sizeof b, "LCL%s%d:%02d", off < 0 ? "-" : "", abs(off) / 60, abs(off) % 60);
      posix = b;
    }
    if (posix.length() && posix != c.tz) {
      c.tz = posix;
      app::applyTz();
      app::saveSettings();
    }
  }
  redirect(strava::authorizeUrl(app::ip()));
}

// Step 2: the GitHub Pages link page forwards Strava's one-time code here.
void handleExchange() {
  if (server.hasArg("error") || !server.hasArg("code")) {
    return sendPage("Not linked", F("<p>Strava wasn't connected (Cancel was tapped).</p><a class=btn href=/link>Try again</a>"));
  }
  strava::Result r = strava::exchange(server.arg("code"), server.arg("scope"));
  if (r == strava::OK) {
    app::onLinked();
    String n = esc(strava::athleteName());
    return sendPage("You're all set", "<p>Linked to Strava" + (n.length() ? " as <b>" + n + "</b>" : String("")) +
                                          ". Your tracker is loading this year's runs now.</p>"
                                          "<a class='btn ghost' href=/>Goal &amp; settings</a>");
  }
  if (r == strava::SCOPE) {
    return sendPage("One more step", F("<p>The tracker needs permission to read your activities. Tap below and leave "
                                       "the activity boxes ticked.</p><a class=btn href=/link>Try again</a>"));
  }
  sendPage("Something went wrong", "<p>" + String(strava::describe(r)) +
                                       ". Check the tracker's WiFi and try again.</p><a class=btn href=/link>Try again</a>");
}

void handleSync() {
  app::requestSync(false);
  redirect("/");
}

void handleWifi() {
  sendPage("Change WiFi", "<p>On your phone, join the WiFi network <b>" AP_NAME
                          "</b>. A setup page will open where you can pick your new WiFi.</p>");
  app::requestPortal();
}

void handleReset() {
  sendPage("Erasing", F("<p>The tracker is erasing everything and restarting into setup.</p>"));
  app::requestReset();
}

}  // namespace

namespace web {

void begin() {
  if (running) return;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/link", HTTP_GET, handleLink);
  server.on("/go", HTTP_GET, handleGo);
  server.on("/exchange", HTTP_GET, handleExchange);
  server.on("/sync", HTTP_POST, handleSync);
  server.on("/wifi", HTTP_POST, handleWifi);
  server.on("/reset", HTTP_POST, handleReset);
  server.onNotFound([] { redirect("/"); });
  server.begin();
  running = true;
}

void stop() {
  if (!running) return;
  server.stop();
  running = false;
}

void loop() {
  if (running) server.handleClient();
}

}  // namespace web
