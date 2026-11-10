# Partner program applications (drafts for Don to paste)

## Garmin Connect Developer Program
Apply: https://developer.garmin.com/gc-developer-program/overview/ ("Request access")

Company: DS3design (with Next Mile Apparel)
Product: Next Mile Tracker, a small WiFi desk display (ESP32, 2.8" screen) that shows a runner's running miles for the current calendar year, progress toward a yearly goal, and their most recent run.
Use of Garmin data: Activity API only - activity type, start time, distance and duration of running activities. Read-only. Nothing is shown publicly, sold, or shared; data is cached only on the runner's own device, and unlinking deletes it.
Users: individual runners who buy or win a tracker; starting with a local charity raffle (Pacegiving).
Why the partner API: today owners must paste a Personal API key, which Garmin's FAQ says isn't intended for multi-user products. We want a proper "Connect with Garmin" OAuth flow.

## COROS API
Status: firmware uses the self-service COROS MCP (OAuth 2.0 + PKCE, no approval; US server mcpus.coros.com, public client registered via dynamic registration).
Partner API (multi-user, webhooks) is for "established platforms with a demonstrated user base": form at https://coros-teams.feishu.cn/share/base/form/shrcnLqSduZsaNhbvDJTO2x0Vlf (needs a Feishu/Lark login) plus an email to api@coros.com.

Same company/product text as above. Data requested: workout list (sport type, start time, distance, duration) for the authorized user only, read-only, displayed on that user's own device.

## Suunto API (partner program, companies only)
Status: application submitted (Cloud API, workout data only) and Suunto Cloud API Agreement signed by Don Schomer III. Reply within ~2 weeks to splashco.brand@gmail.com.
Apply: https://apizone.suunto.com -> Partner program

Same company/product text. Requested scope: workout summaries for the authorized user (activity type, start time, distance, duration). OAuth 2.0 from the tracker's setup page. Expected volume: under 500 users in the first year.
