#include "web.h"

#include <LittleFS.h>

#include "ledfx.h"

#include <WebServer.h>
#include <WiFi.h>

#include "app.h"
#include "activities.h"
#include "extras.h"
#include "ota.h"
#include "source.h"
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

// Two-runner mode: every page is for runner 0 or 1, picked by ?r=1. Pages keep it in their links and forms.
int linkRunner = 0;  // who is part-way through linking a brand that comes back through /exchange

int pick() {
  const int r = app::cfg.duo && server.arg("r") == "1" ? 1 : 0;
  src::use(r);
  return r;
}
String rq(int r, char sep = '?') { return r ? String(sep) + "r=1" : String(); }
String rIn(int r) { return r ? String(F("<input type=hidden name=r value=1>")) : String(); }

String miles(float m) {
  char b[24];
  ui::formatMiles(b, sizeof b, m / 1609.344f, 1);
  return b;
}

void handleRoot() {
  const int r = pick();
  if (!src::linked()) return redirect("/link" + rq(r));
  const acts::Totals& t = acts::totals();
  UiData d = app::uiData();
  String b;
  if (app::cfg.duo) {  // a tab for each runner
    b += F("<div class=row>");
    for (int i = 0; i < 2; i++) {
      UiData o = app::uiData(i);
      String n = o.athlete[0] ? esc(String(o.athlete)) : String(i ? "Runner 2" : "Runner 1");
      b += "<a class='btn" + String(i == r ? "" : " ghost") + "' href='/" + rq(i) + "'>" + n + "</a>";
    }
    b += F("</div>");
  }
  b += F("<div class=card><div class=dim>");
  b += esc(String(d.athlete));
  b += F("</div><div class=big>");
  b += miles(t.meters);
  b += F(" mi</div><div class=dim>");
  b += String(d.year) + " &middot; " + t.runs + " " +
       (app::kindsOf(r) == 2 ? (t.runs == 1 ? "walk" : "walks")
        : app::kindsOf(r) == 3 ? (t.runs == 1 ? "activity" : "activities")
                              : (t.runs == 1 ? "run" : "runs"));
  uint32_t ago = app::lastSyncAgoS();
  b += ago == UINT32_MAX ? String(" &middot; not synced yet") : " &middot; synced " + String(ago / 60) + " min ago";
  const extras::Weather& w = extras::weather();
  if (w.valid) {
    b += F("<div class=dim>Today");
    if (app::cfg.placeName.length()) b += " in " + esc(app::cfg.placeName);
    b += ": " + String(w.temp) + "&deg;F " + extras::describe(w.code) + " &middot; high " + w.hi + " / low " + w.lo +
         " &middot; " + w.rain + "% rain &middot; wind " + w.wind + " mph</div>";
  }
  b += F("</div></div><form method=post action=/save>");
  b += rIn(r);

  b += F("<h2>Yearly goal</h2><label class=row><input type=radio name=gm value=auto");
  if (app::goalOf(r) <= 0) b += F(" checked");
  b += F("> Beat last year");
  if (t.lastYear == d.year - 1 && t.lastYearMeters > 0)
    b += " (" + miles(t.lastYearMeters) + " mi in " + t.lastYear + ")";
  b += F("</label><label class=row><input type=radio name=gm value=own");
  if (app::goalOf(r) > 0) b += F(" checked");
  b += F("> My own goal</label><input type=number name=goal min=1 max=10000 step=1 inputmode=numeric placeholder='Miles this year' value='");
  if (app::goalOf(r) > 0) b += String((int)app::goalOf(r));
  b += F("' onfocus=\"document.querySelector('[value=own]').checked=true\">");

  if (src::kind() == src::POLAR) {
    b += F("<label>Miles you ran this year before linking</label><input type=number name=carry min=0 max=10000 "
           "step=0.1 inputmode=decimal value='");
    b += String(app::carryInMi(), 1);
    b += F("'><div class=dim>Polar only shares runs from after you link, so add earlier miles here.</div>");
  }
  b += F("<h2>Screen</h2><label>Name on screen</label><input type=text name=name maxlength=30 placeholder='");
  b += esc(src::athleteName());
  b += F("' value='");
  b += esc(app::nameOf(r));
  b += F("'><label>Brightness</label><input type=range name=bright min=20 max=255 value=");
  b += String(app::cfg.bright);
  b += F("><label>Count my</label>");
  static const char* const KINDS[] = {"Runs", "Walks and hikes", "Runs, walks and hikes"};
  for (int i = 1; i <= 3; i++)
    b += "<label class=row><input type=radio name=kinds value=" + String(i) + (app::kindsOf(r) == i ? " checked> " : "> ") +
         KINDS[i - 1] + "</label>";
  b += F("<label class=row><input type=checkbox name=duo value=1");
  if (app::cfg.duo) b += F(" checked");
  b += F("> Two runners, side by side</label><div class=dim>A second person links their own watch and gets their own "
         "goal. Tap the tracker's screen to see your miles added up.</div>");
  if (app::cfg.duo) {
    b += F("<label>Shared goal for the two of you (miles this year)</label><input type=number name=shared min=0 max=20000 "
           "step=1 inputmode=numeric placeholder='Both goals added up' value='");
    if (app::cfg.sharedGoal > 0) b += String((int)app::cfg.sharedGoal);
    b += F("'>");
  }
  b += F("<label>Colors</label><select name=theme>");
  for (int i = 0; i < ui::themeCount(); i++)
    b += "<option value=" + String(i) + (i == app::cfg.theme ? " selected>" : ">") + ui::themeName(i) + "</option>";
  b += F("</select><div class=dim>Or tap the tracker's screen, then tap Colors to cycle through them.</div>"
         "<label>Time zone (for the new-year reset)</label><select name=tz>");
  bool matched = false;
  for (const TzEntry& z : TZ_TABLE) {
    if (!z.label) continue;
    bool sel = !matched && app::cfg.tz == z.posix;
    matched = matched || sel;
    b += "<option value='" + String(z.posix) + "'" + (sel ? " selected" : "") + ">" + z.label + "</option>";
  }
  if (!matched) b += "<option value='" + esc(app::cfg.tz) + "' selected>Phone's zone (" + esc(app::cfg.tz) + ")</option>";
  b += F("</select><h2>Weather &amp; lights</h2><label>Your ZIP code or town (for today's running weather)</label>"
         "<input type=text name=place maxlength=40 placeholder='e.g. 95662' value='");
  b += esc(app::cfg.place);
  b += F("'>");
  String ws = extras::weatherStatus();
  if (ws.length()) b += "<div class=dim>" + esc(ws) + "</div>";
  else if (app::cfg.placeName.length()) b += "<div class=dim>Showing weather for " + esc(app::cfg.placeName) + "</div>";
  b += F("<label class=row><input type=checkbox name=adim value=1");
  if (app::cfg.autoDim) b += F(" checked");
  b += F("> Dim the screen in a dark room</label><label class=row><input type=checkbox name=nightclk value=1");
  if (app::cfg.nightClock) b += F(" checked");
  b += F("> At night, show a dim clock instead of turning the screen off</label>"
         "<label class=row><input type=checkbox name=ledon value=1");
  if (app::cfg.led == 2) b += F(" checked");
  b += F("> Keep the lights on the back on all the time</label><div class=dim>Otherwise they light up for 6 hours after new "
         "miles, starting with fireworks, and put on a show for milestones. They use your color theme.</div>"
         "<label>Light effect after new miles</label><select name=ledfx>");
  for (int i = 0; i < ledfx::EFFECT_N; i++)
    b += "<option value=" + String(i) + (i == app::cfg.ledFx ? " selected>" : ">") + ledfx::name(i) + "</option>";
  b += F("</select><label>Number of lights on the strip</label><input type=number name=ledn min=1 max=60 step=1 "
         "inputmode=numeric value=");
  b += String(app::cfg.ledCount);
  b += F("><button>Save</button></form>");

  b += F("<h2>Photo</h2><div class=dim>Pick a photo and it shows faintly behind the miles.</div>"
         "<input type=file id=ph accept='image/*'><div id=pst class=dim></div>");
  if (LittleFS.exists("/photo.jpg"))
    b += F("<form method=post action=/photo/remove><button class=ghost>Remove the photo</button></form>");
  // The phone shrinks the photo to the screen's 800x480 before sending it.
  b += F("<script>document.getElementById('ph').onchange=function(e){var f=e.target.files[0];if(!f)return;"
         "var s=document.getElementById('pst');s.textContent='Sending...';var i=new Image();i.onload=function(){"
         "var c=document.createElement('canvas');c.width=800;c.height=480;var k=Math.max(800/i.width,480/i.height),"
         "w=i.width*k,h=i.height*k;c.getContext('2d').drawImage(i,(800-w)/2,(480-h)/2,w,h);c.toBlob(function(bl){"
         "var d=new FormData();d.append('p',bl,'photo.jpg');fetch('/photo',{method:'POST',body:d}).then(function(r){"
         "if(r.ok)location.reload();else s.textContent='That didn\\'t work. Try again.'}).catch(function(){"
         "s.textContent='That didn\\'t work. Try again.'})},'image/jpeg',0.85)};i.onerror=function(){"
         "s.textContent='That photo couldn\\'t be opened. Try another one.'};i.src=URL.createObjectURL(f)};</script>");

  b += F("<h2>More</h2><form method=post action=/sync>");
  b += rIn(r);
  b += F("<button class=ghost>Refresh miles now</button></form>");
  if (app::cfg.duo) {
    src::use(1);
    if (!src::linked()) b += F("<a class=btn href='/link?r=1'>Link the second runner's watch</a>");
    src::use(r);
  }
  b += F("<form method=post action=/update><button class=ghost>Check for software update</button></form>"
         "<a class='btn ghost' href=/link");
  b += rq(r);
  b += F(">Link a different watch</a>"
         "<form method=post action=/wifi><button class=ghost>Change WiFi</button></form>"
         "<form method=post action=/reset onsubmit=\"return confirm('Erase WiFi, watch link and settings?')\">"
         "<button class=warn>Erase everything</button></form>"
         "<p class=dim>Next Mile Apparel &middot; DS3design &middot; Data from ");
  b += src::label(src::kind());
  b += " &middot; v" + String(ota::version()) + " &middot; light " + String(extras::lightRaw());
  String us = app::updateStatus();
  if (us.length()) b += " &middot; " + esc(us);
  b += F("</p>");
  sendPage("Next Mile Tracker", b);
}

void handleSave() {
  const int r = pick();
  Settings& c = app::cfg;
  String& name = app::nameOf(r);
  name = server.arg("name");
  name.trim();
  if (server.arg("gm") == "own" && server.arg("goal").toFloat() > 0) app::goalOf(r) = server.arg("goal").toFloat();
  else app::goalOf(r) = 0;
  int br = server.arg("bright").toInt();
  if (br >= 20 && br <= 255) c.bright = br;
  int kinds = server.arg("kinds").toInt();
  if (kinds < 1 || kinds > 3) kinds = app::kindsOf(r);
  String tz = server.arg("tz");
  bool tzChanged = tz.length() && tz != c.tz;
  if (tzChanged) {
    c.tz = tz;
    c.tzManual = true;
    app::applyTz();
  }
  bool resync = tzChanged || kinds != app::kindsOf(r);
  app::kindsOf(r) = kinds;
  int th = server.hasArg("theme") ? server.arg("theme").toInt() : -1;
  if (th >= 0 && th < ui::themeCount()) {
    c.theme = th;
    ui::setTheme(th);
  }
  if (server.hasArg("carry")) app::setCarryInMi(server.arg("carry").toFloat());
  String place = server.arg("place");
  place.trim();
  if (place != c.place) {
    c.place = place;
    extras::locationChanged();
  }
  c.autoDim = server.hasArg("adim");
  c.led = server.hasArg("ledon") ? 2 : 1;
  c.nightClock = server.hasArg("nightclk");
  if (server.hasArg("ledfx")) c.ledFx = constrain(server.arg("ledfx").toInt(), 0, ledfx::EFFECT_N - 1);
  if (server.hasArg("ledn")) c.ledCount = constrain(server.arg("ledn").toInt(), 1, ledfx::MAX_LEDS);
  if (server.hasArg("shared")) c.sharedGoal = max(0.0f, server.arg("shared").toFloat());
  const bool duoOn = server.hasArg("duo") && !c.duo;
  c.duo = server.hasArg("duo");
  app::saveSettings();
  app::requestSync(resync);
  if (duoOn) {  // just switched on: straight to linking the second runner, unless they already have
    src::use(1);
    if (!src::linked()) return redirect("/link?r=1");
    src::use(r);
  }
  redirect("/" + rq(c.duo ? r : 0));
}

// Step 1 of linking: pick a brand. The script tacks the phone's time zone onto each link for the new-year reset.
void handleLink() {
  const int r = pick();
  String b = r ? F("<p><b>Second runner:</b> which watch or app records your runs?</p>")
               : F("<p>Which watch or app records your runs?</p>");
  const src::Kind order[] = {src::GARMIN, src::APPLE, src::COROS, src::GOOGLE, src::POLAR, src::WHOOP, src::OURA, src::WITHINGS, src::WAHOO, src::INTERVALS, src::STRAVA};
  for (src::Kind k : order) {
    if (!src::available(k)) continue;
    b += "<a class='btn ghost' href='/go?k=" + String((int)k) + "" + rq(r, '&') + "'>" + src::label(k) + "</a>";
  }
  b += F("<p class=dim>Suunto, Zwift or something else? Connect it to a free intervals.icu account, then pick "
         "intervals.icu here. More brands arrive in free updates.</p><script>"
         "var z='';try{z=Intl.DateTimeFormat().resolvedOptions().timeZone||''}catch(e){}"
         "document.querySelectorAll('a[href^=\"/go\"]').forEach(function(a){a.href+='&tz='+encodeURIComponent(z)+"
         "'&off='+new Date().getTimezoneOffset()})</script>");
  sendPage("Link your watch", b);
}

void handleGo() {
  const int r = pick();
  linkRunner = r;
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
  src::Kind k = (src::Kind)server.arg("k").toInt();
  if (k == src::GARMIN) return redirect("/garmin" + rq(r));
  if (k == src::INTERVALS) return redirect("/intervals" + rq(r));
  if (k == src::APPLE) {
    if (src::kind() != src::APPLE || !src::linked()) src::linkApple();
    app::onLinked();
    return redirect("/apple" + rq(r));
  }
  if (!src::available(k)) return sendPage("Not available yet", F("<p>That brand isn't switched on in this tracker yet.</p><a class=btn href=/link>Back</a>"));
  redirect(src::authorizeUrl(k, app::ip()));
}

void linkedPage() {
  const int r = src::current();
  app::onLinked();
  String n = esc(src::athleteName());
  sendPage("You're all set", "<p>Linked to " + String(src::label(src::kind())) + (n.length() ? " as <b>" + n + "</b>" : String("")) +
                                 ". Your tracker is loading this year's miles now.</p>" +
                                 (n.length() ? String()
                                             : String(String(F("<form method=post action=/name>")) + rIn(r) + F("<label>Your name for the screen</label>"
                                                        "<input type=text name=name maxlength=30 autocomplete=given-name>"
                                                        "<button>Save name</button></form>"))) +
                                 "<a class='btn ghost' href='/" + rq(r) + "'>Goal &amp; settings</a>");
}

void handleName() {
  const int r = pick();
  String& name = app::nameOf(r);
  name = server.arg("name");
  name.trim();
  app::saveSettings();
  redirect("/" + rq(r));
}

void handleIntervals() {
  const int r = pick();
  if (server.method() == HTTP_POST) {
    src::Result res = src::linkIntervals(server.arg("key"));
    if (res == src::OK) return linkedPage();
    return sendPage("Key not accepted", "<p>" + String(src::describe(res)) +
                                            ". Make sure you copied the whole key.</p><a class=btn href='/intervals" + rq(r) + "'>Try again</a>");
  }
  sendPage("Link intervals.icu",
           String(F("<ol><li>Make a free account at intervals.icu and connect your watch under <b>Settings &rarr; Connections</b> "
             "(Garmin, COROS, Suunto, Polar, Wahoo and more).</li><li>On the same Settings page, scroll to <b>Developer "
             "Settings</b> and generate an <b>API key</b>, then copy it.</li><li>Come back here, paste it below and tap "
             "<b>Link</b>.</li></ol>"
             "<a class='btn ghost' target=_blank href='https://intervals.icu/settings'>Open intervals.icu</a>"
             "<form method=post action=/intervals>")) + rIn(r) + F("<label>API key</label><input type=text name=key autocomplete=off "
             "autocapitalize=off spellcheck=false required><button>Link</button></form>"
             "<p class=dim>Connect your watch to intervals.icu directly. Runs it gets from Strava can't be passed on to "
             "other apps.</p>"));
}

void handleGarmin() {
  const int r = pick();
  if (server.method() == HTTP_POST) {
    src::Result res = src::linkGarmin(server.arg("key"));
    if (res == src::OK) return linkedPage();
    return sendPage("Key not accepted", "<p>" + String(src::describe(res)) +
                                            ". Make sure you copied the whole key.</p><a class=btn href='/garmin" + rq(r) + "'>Try again</a>");
  }
  sendPage("Link Garmin",
           String(F("<ol><li>Tap <b>Open Garmin Connect</b> and sign in.</li><li>Tap <b>Create Key</b>, choose <b>1 year</b>, "
             "then copy the key. Garmin only shows it once.</li><li>Come back here, paste it below and tap <b>Link</b>.</li></ol>"
             "<a class='btn ghost' target=_blank href='https://connect.garmin.com/app/settings/personalApiToken'>Open Garmin Connect</a>"
             "<form method=post action=/garmin>")) + rIn(r) + F("<label>Personal API key</label><input type=text name=key autocomplete=off "
             "autocapitalize=off spellcheck=false required><button>Link</button></form>"
             "<p class=dim>Garmin emails you before the key expires. Make a new one and paste it here the same way.</p>"));
}

void handleApple() {
  const int r = pick();
  String url = "http://" + app::ip() + "/api/apple?t=" + src::appleToken();
  String b = F("<p>Apple Watch runs live in the Health app on your iPhone, so an iPhone Shortcut sends your miles to the "
               "tracker. Set it up once:</p><ol>"
               "<li>Open the <b>Shortcuts</b> app &rarr; <b>Automation</b> &rarr; <b>+</b> &rarr; <b>Time of Day</b>, every day "
               "(for example 9 PM), <b>Run Immediately</b>.</li>"
               "<li>Add <b>Find Health Samples</b>: Workouts, Type is Running, Start Date is in this year. Then "
               "<b>Calculate Statistics</b> &rarr; Sum of Distance (miles).</li>"
               "<li>Add <b>Get Contents of URL</b>: Method POST, Request Body JSON with keys <code>year</code>, "
               "<code>miles</code> (the sum), <code>runs</code> (count) and the URL below.</li></ol>"
               "<label>Tracker address for the Shortcut</label><input type=text readonly onclick='this.select()' value='");
  b += esc(url);
  b += F("'><p class=dim>Your iPhone must be home on the same WiFi when it runs. A ready-made Shortcut link is on the "
         "Next Mile Tracker help page.</p><a class='btn ghost' href='/");
  b += rq(r);
  b += F("'>Goal &amp; settings</a>");
  sendPage("Link Apple Watch", b);
}

void handleApplePush() {
  src::Result r = src::AUTH;
  for (int i = 0; i < app::runners() && r == src::AUTH; i++) {  // the token says whose iPhone it is
    src::use(i);
    if (src::kind() == src::APPLE) r = src::applePush(server.arg("t"), server.arg("plain"));
  }
  if (r == src::OK) app::onPushed();
  server.send(r == src::OK ? 200 : (r == src::AUTH ? 403 : 400), "application/json",
              r == src::OK ? "{\"ok\":true}" : "{\"ok\":false}");
}

// Step 2: the GitHub Pages link page forwards the one-time code here, with src=<brand number>.
void handleExchange() {
  src::use(app::cfg.duo ? linkRunner : 0);
  src::Kind k = server.hasArg("src") ? (src::Kind)server.arg("src").toInt() : src::STRAVA;
  if (server.hasArg("error") || !server.hasArg("code")) {
    return sendPage("Not linked", String(F("<p>Your watch account wasn't connected (Cancel was tapped).</p><a class=btn href='/link")) + rq(linkRunner) + "'>Try again</a>");
  }
  src::Result r = src::exchange(k, server.arg("code"), server.arg("scope"));
  if (r == src::OK) return linkedPage();
  if (r == src::SCOPE) {
    return sendPage("One more step", String(F("<p>The tracker needs permission to read your activities. Tap below and leave "
                                       "the activity boxes ticked.</p><a class=btn href='/link")) + rq(linkRunner) + "'>Try again</a>");
  }
  sendPage("Something went wrong", "<p>" + String(src::describe(r)) +
                                       ". Check the tracker's WiFi and try again.</p><a class=btn href='/link" + rq(linkRunner) + "'>Try again</a>");
}

File upFile;
void handlePhotoUpload() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) upFile = LittleFS.open("/photo.tmp", "w");
  else if (u.status == UPLOAD_FILE_WRITE && upFile) upFile.write(u.buf, u.currentSize);
  else if (u.status == UPLOAD_FILE_END && upFile) upFile.close();
  else if (u.status == UPLOAD_FILE_ABORTED && upFile) upFile.close(), LittleFS.remove("/photo.tmp");
}
void handlePhoto() {
  if (!LittleFS.exists("/photo.tmp")) return server.send(400, "text/plain", "No photo");
  LittleFS.remove("/photo.jpg");
  LittleFS.rename("/photo.tmp", "/photo.jpg");
  app::photoChanged();
  server.send(200, "text/plain", "ok");
}
void handlePhotoRemove() {
  LittleFS.remove("/photo.jpg");
  app::photoChanged();
  redirect("/");
}

void handleSync() {
  const int r = pick();
  app::requestSync(true);
  redirect("/" + rq(r));
}

void handleUpdate() {
  sendPage("Checking for updates", F("<p>If there's a new version the tracker downloads it, shows progress on its screen and "
                                     "restarts. Your link and goal are kept.</p><a class='btn ghost' href=/>Back</a>"));
  app::requestUpdate();
}

void handleWifi() {
  sendPage("Change WiFi", "<p>Look at the tracker's screen. It now shows the WiFi networks it can see.</p><ol>"
                          "<li>Tap your home WiFi on the tracker's screen.</li>"
                          "<li>Type the WiFi password on the tracker's keyboard and tap <b>Connect</b>.</li></ol>"
                          "<p class=dim>Or tap <b>Use my phone</b> on the tracker and scan the code.</p>");
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
  server.on("/name", HTTP_POST, handleName);
  server.on("/link", HTTP_GET, handleLink);
  server.on("/go", HTTP_GET, handleGo);
  server.on("/exchange", HTTP_GET, handleExchange);
  server.on("/garmin", HTTP_ANY, handleGarmin);
  server.on("/intervals", HTTP_ANY, handleIntervals);
  server.on("/apple", HTTP_GET, handleApple);
  server.on("/api/apple", HTTP_POST, handleApplePush);
  server.on("/sync", HTTP_POST, handleSync);
  server.on("/photo", HTTP_POST, handlePhoto, handlePhotoUpload);
  server.on("/photo/remove", HTTP_POST, handlePhotoRemove);
  server.on("/wifi", HTTP_POST, handleWifi);
  server.on("/update", HTTP_POST, handleUpdate);
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
