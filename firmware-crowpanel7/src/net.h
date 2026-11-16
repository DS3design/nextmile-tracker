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
  const char* accept = "application/json";
  String session;  // Mcp-Session-Id
};
// Returns the HTTP status (or <0 on transport/parse error); 200/201 bodies are parsed into `doc`.
int call(const Req& r, JsonDocument& doc);
// Raw variant (COROS MCP): body returned as text, plus the Content-Type and Mcp-Session-Id response headers.
int callText(const Req& r, String& body, String* contentType, String* session);
String urlEncode(const String& s);
void setPump(void (*fn)());  // called after each request so the screen stays responsive during long syncs
}  // namespace net
