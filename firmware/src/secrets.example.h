#pragma once
// Copy to secrets.h (git-ignored) and fill in. Leave a pair empty to hide that brand from setup.
// Every OAuth app's redirect / callback must be LINK_PAGE_URL (Strava: callback domain = its host).
#define LINK_PAGE_URL "https://ds3design.github.io/nextmile-tracker/link/"
// https://admin.polaraccesslink.com
#define POLAR_CLIENT_ID ""
#define POLAR_CLIENT_SECRET ""
// Google Cloud console > OAuth client (Web application), Google Health API enabled
#define GOOGLE_CLIENT_ID ""
#define GOOGLE_CLIENT_SECRET ""
// https://developer-dashboard.whoop.com (scopes: read:workout, read:profile, offline)
#define WHOOP_CLIENT_ID ""
#define WHOOP_CLIENT_SECRET ""
// https://cloud.ouraring.com/oauth/applications (scope: workout)
#define OURA_CLIENT_ID ""
#define OURA_CLIENT_SECRET ""
// https://developer.withings.com/dashboard (Public API, scope: user.activity)
#define WITHINGS_CLIENT_ID ""
#define WITHINGS_CLIENT_SECRET ""
// https://www.strava.com/settings/api (needs a Strava subscription)
#define STRAVA_CLIENT_ID ""
#define STRAVA_CLIENT_SECRET ""
