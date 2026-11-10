#include "net.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "certs_gen.h"

namespace net {

int call(const Req& r, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setCACert(ROOT_CAS);
  client.setTimeout(15);
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the JSON can be parsed straight off the socket
  http.setTimeout(15000);
  if (!http.begin(client, r.url)) return -1;
  if (r.auth.length()) http.addHeader("Authorization", r.auth);
  http.addHeader("Accept", "application/json");
  int code;
  if (strcmp(r.method, "GET") == 0) {
    code = http.GET();
  } else {
    http.addHeader("Content-Type", r.contentType);
    code = http.sendRequest(r.method, r.body);
  }
  if (code == 200 || code == 201) {
    DeserializationError e = r.filter ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*r.filter))
                                      : deserializeJson(doc, http.getStream());
    if (e && e != DeserializationError::EmptyInput) code = -2;
  }
  http.end();
  return code;
}

String urlEncode(const String& s) {
  static const char* hex = "0123456789ABCDEF";
  String o;
  o.reserve(s.length() * 3);
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      o += (char)c;
    } else {
      o += '%';
      o += hex[c >> 4];
      o += hex[c & 15];
    }
  }
  return o;
}

}  // namespace net
