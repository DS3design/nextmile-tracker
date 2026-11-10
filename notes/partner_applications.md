# Partner program applications (drafts for Don to paste)

## Garmin Connect Developer Program
Apply: https://developer.garmin.com/gc-developer-program/overview/ ("Request access")

Company: DS3design (with Next Mile Apparel)
Product: Next Mile Tracker, a small WiFi desk display (ESP32, 2.8" screen) that shows a runner's running miles for the current calendar year, progress toward a yearly goal, and their most recent run.
Use of Garmin data: Activity API only - activity type, start time, distance and duration of running activities. Read-only. Nothing is shown publicly, sold, or shared; data is cached only on the runner's own device, and unlinking deletes it.
Users: individual runners who buy or win a tracker; starting with a local charity raffle (Pacegiving), then small batches sold by Next Mile Apparel.
Why the partner API: today owners must paste a Personal API key, which Garmin's FAQ says isn't intended for multi-user products. We want a proper "Connect with Garmin" OAuth flow.

## COROS API
Apply: COROS support -> "API Application" (https://support.coros.com, search "API")

Same company/product text as above. Data requested: workout list (sport type, start time, distance, duration) for the authorized user only, read-only, displayed on that user's own device.

## Suunto API (partner program, companies only)
Apply: https://apizone.suunto.com -> Partner program

Same company/product text. Requested scope: workout summaries for the authorized user (activity type, start time, distance, duration). OAuth 2.0 from the tracker's setup page. Expected volume: under 500 users in the first year.
