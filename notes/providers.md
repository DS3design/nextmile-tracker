# Data-source research (checked 2026-11-09)

## Strava
- "A Strava subscription is a prerequisite for creating an app." New apps: 1 athlete; dashboard upgrade to 10; >10 needs review. Don won't pay -> Strava kept in code but hidden unless STRAVA_CLIENT_ID set.

## Garmin Connect Personal API (free, per-user key)
- User: connect.garmin.com/app/settings/personalApiToken -> Create Key -> pick expiry 30/90/180/365 days (email reminder ~1 week before). Key shown once.
- Token: POST https://personal.connectapi.garmin.com/personalapitoken-service/personal-api/personal-api-key/token  header `Authorization: ApiKey <key>` -> access token valid 24 h (no refresh token; just call again).
- FAQ: "not intended for commercial applications serving multiple end users" (gray area for selling).
- Activities: GET https://personal.connectapi.garmin.com/activitylist-service/activities/search/activities
  params: start (offset, default 0), limit (default 99), startDate/endDate (YYYY-MM-DD, local start, inclusive), activityType (parent key e.g. running), activitySubType, sortOrder, excludeChildren.
  Response: array of SummarizedActivityDTO: activityId, activityName, startTimeLocal "yyyy-MM-dd HH:mm:ss", startTimeGMT, distance (m), duration, movingDuration, elapsedDuration (s), activityType{typeId,typeKey,parentTypeId}. Full history available.
- Business program (Starter): apply at https://www.garmin.com/en-US/forms/StarterGarminConnectDeveloperAccess/ (push only, no historical data, limited users).

## Polar AccessLink (free, one client for all users)
- Create client at https://admin.polaraccesslink.com with a Polar Flow account. OAuth2 auth code; tokens don't expire (expires_in ~1 yr, "will not expire unless revoked"); response includes x_user_id.
- Must POST /v3/users {"member-id": "..."} after token.
- GET https://www.polaraccesslink.com/v3/exercises (Bearer user token). "Only exercises uploaded to Flow in the last 30 days are available. Only exercises uploaded after the user is registered with your client." => board must accumulate locally; no pre-link history, no last-year total (manual goal / starting miles).
- Exercise fields: id, upload_time, start_time, duration "PT2H44M", distance (m), sport, detailed_sport_info (e.g. RUNNING, TRAIL_RUNNING, TREADMILL_RUNNING...).
- Rate: 500+20*users /15 min, 5000+100*users /24 h.

## Google Health API (Fitbit / Pixel Watch) - Fitbit Web API turned off Nov 2 2026
- OAuth 2.0 Google; scope https://www.googleapis.com/auth/googlehealth.activity_and_fitness.readonly (restricted). Unverified app limited to 100 users; beyond needs OAuth verification + annual CASA third-party security assessment.
- Exercises: GET https://health.googleapis.com/v4/users/me/dataTypes/exercise/dataPoints?filter=exercise.interval.civil_start_time >= "2026-01-01" AND exercise.interval.civil_start_time < "2027-01-01" (URL-encode). Page size max 25 for exercise, nextPageToken. Full history.
- Fields: interval (start/end + offsets), exerciseType (RUNNING, WALKING...), displayName, activeDuration "1800s", metricsSummary.distanceMillimeters.

## Others
- Suunto: partner program for companies only, "we do not provide this for personal use"; ~2 weeks response.
- Wahoo Cloud API: request + approval per app.
- Coros: support site behind Cloudflare, unverified (believed partner application).
- Apple: no web API -> iPhone Shortcut idea (untested). Samsung Health: Android SDK only. Amazfit: none found.

### Google exercise data point (example)
{"name":"users/.../dataTypes/exercise/dataPoints/...","exercise":{"interval":{"startTime":"2026-04-20T08:00:00Z","startUtcOffset":"0s","endTime":"...","endUtcOffset":"0s"},"exerciseType":"RUNNING","metricsSummary":{"distanceMillimeters":5000000,...},"displayName":"Run","activeDuration":"1800s"}}
Local start = startTime + startUtcOffset. Response list key likely "dataPoints" + "nextPageToken" (verify on device).

# Implementation plan (user approved 2026-11-09)
User: "what about all of the major watches? ... they can pick their watch brand and then it asks for their key" + "can we add in a OTA update so that others could be added in the future?"
Build now: Garmin (personal key pasted on setup page), Polar (Connect button, Don's free client), Fitbit/Pixel (Google OAuth, <=100 users), Apple Watch (iPhone Shortcut POSTs to board, untested), Strava hidden unless STRAVA_CLIENT_ID set.
- Refactor strava.cpp -> activities.cpp (shared Run cache /runs.bin, totals, last-year, recompute) + providers: strava/garmin/polar/google/apple implementing fetch(year, full) -> list of {id, meters, moving_s, local start, type, name}.
  Polar: accumulate (can't delete), no history before link; offer "miles before linking" start offset + manual goal. Apple: board endpoint POST /api/apple {ytd_m, runs, last...} or per-workout list.
- Settings: provider enum in Preferences; brand picker page at /link (after WiFi). Link page docs/link/ must handle state=ip|provider for Polar/Google redirects.
- main.cpp: replace strava:: calls with provider-agnostic api (src lines ~165-260 use strava::linked/sync/syncLastYear/totals/configured).
- OTA: partitions -> otadata + app0/app1 0x1A0000 each + LittleFS 0xB0000 (704 KB) -> re-encode intro.bin to <=~450 KB (tools/make_assets.py quality/fps). Daily check https://ds3design.github.io/nextmile-tracker/fw/manifest.json {version, env bins url, sha256, sig}; verify ECDSA P-256 signature (mbedtls) over sha256 with embedded public key before Update.end(); private key kept out of git (ask Don to store as secret).
- Draft partner applications (Garmin Starter, Coros, Suunto) as text for Don.

# Progress
- DONE: firmware/src/activities.{h,cpp} (shared cache, carry-in, mergeWindow/merge/replaceYear), net.{h,cpp} (HTTPS helper), source.h (provider API).
- TODO: source.cpp dispatcher + src_strava.cpp (port from strava.cpp, then delete strava.*), src_garmin.cpp, src_polar.cpp, src_google.cpp, src_apple.cpp; update main.cpp (strava:: -> src::, acts::totals) and web.cpp (brand picker /link, /garmin form, /apple shortcut page, carry-in setting, /exchange?src=); docs/link/index.html state "ip|kind"; secrets.example.h add POLAR_/GOOGLE_ ids; OTA.
- DONE: source.cpp (all providers; strava.cpp/h removed). acts::setAbsolute for Apple. secrets.example.h has POLAR_/GOOGLE_/STRAVA_/OTA_PUBKEY_PEM.
- Apple JSON: POST /api/apple?t=<token> {"year","miles","runs","last":{"name","miles","minutes","start":"yyyy-MM-dd HH:mm"},"last_year_miles"}
- state param now "ip|kind" (kind int: 1 strava 2 garmin 3 polar 4 google 5 apple) -> docs/link must split & pass src=kind to /exchange.
- DONE: main.cpp mechanical strava:: -> src::/acts:: (still: Screen::Link uses ui::linkStrava with /link URL - rename copy to "Link your watch"; NotConfigured screen now unused).
- NEXT: web.cpp lines 71-201 still use strava:: (root page, /link, /go, /exchange). Rewrite /link as brand picker (src::available), /garmin form POST key, /apple page with token + shortcut steps + POST /api/apple, /go?k=, /exchange?src=&code=&scope=, carry-in "miles before linking" field when !src::hasHistory(). Then docs/link/index.html split state "ip|kind".
- DONE: web.cpp (picker /link, /go?k=, /garmin, /apple, /api/apple, /exchange?src=, Polar carry-in), docs/link state split, ui.cpp copy, app::onPushed/carryInMi/setCarryInMi, d.source label.
- NEXT: compile both envs; fix errors; sim renders; OTA (partitions, ota.cpp, tools/sign_release.py, manifest), smaller intro; docs/index.html + README + card provider text; partner application drafts; commit.
- DONE OTA: partitions (app0/app1 0x1A0000, LittleFS 0xA0000), ota.{h,cpp} (manifest docs/fw/manifest.json, sha256 + ECDSA P-256 verify, boot-count rollback), tools/release.py keygen/publish, firmware/src/ota_pubkey.h, FW_VERSION/FW_ENV flags, /update route, intro re-encoded q=10 (510 KB).
- Signing key generated at ~/.nextmile/ota_signing_key.pem on Devin VM ONLY -> Don must back it up (offer to save as Devin secret NEXTMILE_OTA_KEY).
- DONE: signing key offered as user secret NEXTMILE_OTA_SIGNING_KEY (release.py reads NEXTMILE_OTA_KEY path; write secret to file first).
- DONE: color themes. Plan: ui.cpp colour constants -> macros on current theme struct (bg, fg, grey, dim, panel, track, accent); ~8 themes (Next Mile, Light, Blue, Green, Orange, Purple, Pink, Gold); Settings.theme in Preferences; device: Info screen gets a "COLORS" button (tap cycles + redraws), web settings dropdown; sim renders each theme.
- User said: will create GitHub repo soon; yes to walkthrough of Polar + Google dev apps (check Google testing-mode 7-day refresh token expiry!).
- tools/write_secrets.py builds secrets.h from NEXTMILE_POLAR_/GOOGLE_ CLIENT_ID/SECRET secrets (requested from Don as user secrets).
