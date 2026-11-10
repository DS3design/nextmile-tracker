#pragma once
// Small HTTPS helper shared by the data sources.
#include <Arduino.h>
#include <ArduinoJson.h>

namespace net {
struct Req {
  const char* method = "GET";
  String url;
  String body;
  const char* contentType = "application/x-www-form-urlencoded";
  String auth;  // full Authorization header value
  JsonDocument* filter = nullptr;
};
// Returns the HTTP status (or <0 on transport/parse error); 200/201 bodies are parsed into `doc`.
int call(const Req& r, JsonDocument& doc);
String urlEncode(const String& s);
}  // namespace net
