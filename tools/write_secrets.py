#!/usr/bin/env python3
"""Writes firmware/src/secrets.h (git-ignored) from NEXTMILE_* environment variables."""
import os
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
keys = ['POLAR_CLIENT_ID', 'POLAR_CLIENT_SECRET', 'GOOGLE_CLIENT_ID', 'GOOGLE_CLIENT_SECRET',
        'STRAVA_CLIENT_ID', 'STRAVA_CLIENT_SECRET', 'WHOOP_CLIENT_ID', 'WHOOP_CLIENT_SECRET',
        'OURA_CLIENT_ID', 'OURA_CLIENT_SECRET', 'WITHINGS_CLIENT_ID', 'WITHINGS_CLIENT_SECRET']
out = ['#pragma once', '#define LINK_PAGE_URL "https://ds3design.github.io/nextmile-tracker/link/"']
for k in keys:
    v = os.environ.get('NEXTMILE_' + k, '').strip()
    out.append(f'#define {k} "{v}"')
    print(k, 'set' if v else 'empty')
open(os.path.join(ROOT, 'firmware', 'src', 'secrets.h'), 'w').write('\n'.join(out) + '\n')
