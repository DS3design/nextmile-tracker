#pragma once
// IANA zone -> POSIX TZ rule. Entries with a label show in the settings drop-down.
struct TzEntry {
  const char* iana;
  const char* label;
  const char* posix;
};

static const TzEntry TZ_TABLE[] = {
    {"America/Los_Angeles", "US Pacific", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Denver", "US Mountain", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Phoenix", "Arizona", "MST7"},
    {"America/Chicago", "US Central", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/New_York", "US Eastern", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Anchorage", "Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Pacific/Honolulu", "Hawaii", "HST10"},
    {"America/Halifax", "Atlantic (Canada)", "AST4ADT,M3.2.0,M11.1.0"},
    {"America/St_Johns", "Newfoundland", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"America/Puerto_Rico", "Puerto Rico", "AST4"},
    {"Europe/London", "UK", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin", "Ireland", "IST-1GMT0,M10.5.0,M3.5.0/1"},
    {"Europe/Paris", "Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Athens", "Eastern Europe", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Asia/Tokyo", "Japan", "JST-9"},
    {"Australia/Perth", "Australia West", "AWST-8"},
    {"Australia/Brisbane", "Queensland", "AEST-10"},
    {"Australia/Sydney", "Australia East", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Pacific/Auckland", "New Zealand", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"UTC", "UTC", "UTC0"},
    {"America/Vancouver", nullptr, "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Tijuana", nullptr, "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Boise", nullptr, "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Edmonton", nullptr, "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Winnipeg", nullptr, "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Detroit", nullptr, "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Toronto", nullptr, "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Indiana/Indianapolis", nullptr, "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Kentucky/Louisville", nullptr, "EST5EDT,M3.2.0,M11.1.0"},
    {"Europe/Berlin", nullptr, "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", nullptr, "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome", nullptr, "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Amsterdam", nullptr, "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Australia/Melbourne", nullptr, "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Etc/UTC", nullptr, "UTC0"},
};
