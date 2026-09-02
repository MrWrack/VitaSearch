# VitaSearch 0.7 HTTPS Preview — native Spotify UI + modern web proxy

VitaSearch is a PS Vita VitaSDK client paired with a Node/Playwright proxy. Modern websites render on Chromium on the proxy, while the Vita receives frames and sends navigation input.

## New in 0.4

Spotify now has a **native Vita UI** instead of relying on the rendered Spotify control page.

- START toggles Web / native Spotify
- Native Now Playing view with title, artist, active Connect device and album cover
- Permanent mini-player over the web view
- X play/pause, L previous, R next
- Left/right seek 10 seconds; up/down changes Spotify Connect volume
- Triangle opens a native Spotify search keyboard
- Search results are navigated with D-pad; X starts a track; SELECT adds it to queue
- Front touch zones for previous/play-next and progress seeking
- If Spotify is not connected, SELECT opens the proxy-hosted OAuth PKCE login flow
- Proxy exposes compact `/spotify/native/*` endpoints tailored for the Vita client

Spotify playback remains on an official Spotify Connect device. VitaSearch controls playback using Spotify's API; it does not decrypt, capture or rebroadcast Spotify audio.

## Proxy setup

Requires Node.js 20+.

```bash
cd proxy
npm install
npx playwright install chromium
```

Create a Spotify developer app. Configure the redirect URI to match `SPOTIFY_REDIRECT_URI`; default:

```text
http://127.0.0.1:8080/spotify/callback
```

Start:

```bash
SPOTIFY_CLIENT_ID=YOUR_CLIENT_ID npm start
```

## Vita proxy address

Edit `ux0:data/vitasearch/config.txt`, for example:

```text
http://192.168.1.50:8080
```

## Build the VPK

Requires VitaSDK, vita2d and libcurl:

```bash
cd vita
mkdir -p build && cd build
cmake ..
make -j4
```

Output: `VitaSearch.vpk`

## Controls

Web: D-pad cursor, X click, Triangle address/search, L/R back-forward, Square reload, START Spotify.

Spotify: X play/pause, L/R previous-next, Left/Right seek, Up/Down volume, Triangle search, Square refresh, SELECT login/control page, START web.

START+SELECT exits the application.


## Security hardening in 0.6

- Proxy requires `VITASEARCH_API_KEY` when exposed on the LAN.
- Vita sends the key in `X-VitaSearch-Key`; do not commit your real key to GitHub.
- Chromium blocks loopback, RFC1918/private, link-local and `.local` web targets by default (`ALLOW_PRIVATE_TARGETS=0`) to reduce SSRF/LAN-pivot risk.
- Request body limits, rate limiting, hidden Express signature and security headers are enabled.
- Spotify image proxy only accepts HTTPS Spotify CDN hosts.
- Spotify OAuth PKCE `state` remains one-time and expires automatically.
- Privacy API can clear cookies, cache, local/session storage, history view, or the saved Spotify token.
- Maximum browser sessions defaults to 6.

Use `ux0:data/vitasearch/config.txt` with **two lines**:

```text
http://192.168.1.50:8080
YOUR_SAME_VITASEARCH_API_KEY
```

Generate a key on the proxy PC, for example `openssl rand -hex 24`, and put the same value in the proxy environment and the Vita config file. Keep the key private and never commit it to GitHub.

Start example:

```bash
VITASEARCH_API_KEY=YOUR_LONG_RANDOM_KEY SPOTIFY_CLIENT_ID=YOUR_CLIENT_ID npm start
```

The proxy is still HTTP on your trusted LAN by default. For untrusted networks or Internet exposure, place it behind a TLS reverse proxy/VPN; do not port-forward port 8080 directly to the Internet.


## HTTPS between the Vita and the proxy (0.7)

VitaSearch can now use HTTPS for the LAN connection to the Playwright proxy. The Node proxy starts a TLS 1.2+ listener when both `TLS_CERT_FILE` and `TLS_KEY_FILE` are set. The old HTTP port can redirect to HTTPS with `REDIRECT_HTTP_TO_HTTPS=1`.

Example proxy environment:

```text
PORT=8080
HTTPS_PORT=8443
HOST=0.0.0.0
VITASEARCH_API_KEY=YOUR_LONG_RANDOM_KEY
TLS_CERT_FILE=/path/to/fullchain.pem
TLS_KEY_FILE=/path/to/privkey.pem
REDIRECT_HTTP_TO_HTTPS=1
```

Vita config (`ux0:data/vitasearch/config.txt`) supports three lines:

```text
https://vitasearch.example.com:8443
YOUR_SAME_VITASEARCH_API_KEY

```

Line 3 is optional and is the path to a custom CA certificate. With a private/local CA, copy its PEM certificate to the Vita and configure:

```text
https://192.168.1.50:8443
YOUR_SAME_VITASEARCH_API_KEY
ux0:data/vitasearch/ca.pem
```

Certificate verification and hostname verification remain enabled. Do **not** disable TLS verification. If you use an IP address, the certificate must contain that IP address in its Subject Alternative Name (SAN). A normal public certificate for a hostname is easier when the Vita trusts its issuing CA.

The HTTPS proxy protects Vita-to-proxy traffic. Chromium on the proxy already uses normal HTTPS/TLS when websites themselves use HTTPS.


## VitaSearch v0.8 settings

In Web mode press **SELECT** to open Settings. The menu now contains:

- **JavaScript** — ON/OFF per current Chromium browser session. Changing it recreates the session safely and reloads the current page.
- **Clear cookies** — removes cookies for the current session.
- **Clear search history** — clears VitaSearch's proxy-side search list.
- **Clear browser history** — resets the current browsing context and returns it to Google.
- **Clear cache** — clears Chromium's browser cache for the current session.
- **Clear site data** — clears cookies plus localStorage/sessionStorage for the current session.
- **Clear all browser data** — clears cookies, site data, cache, search history, and resets browsing history.

Controls: **D-pad Up/Down** selects an item, **X** toggles/executes it, and **O** returns to the browser.


## Spotify callback status (v0.9)

VitaSearch keeps the Spotify OAuth callback on the proxy. The Vita never stores the
Spotify client secret or receives access/refresh tokens.

Native status endpoint:

`GET /spotify/native/status`

It reports only safe UI state: connected, callback status, token active/inactive,
and the active Spotify Connect device name/type. It never returns Spotify tokens.

Suggested Vita status panel:

- Connected / Not connected
- Callback: OK / Not connected / Error
- Token: Active / Inactive
- Device: active Spotify Connect device

For HTTPS deployments, configure the Spotify redirect URI to the exact callback URL
registered for the proxy. For local development, use an explicit loopback callback
such as `http://127.0.0.1:8080/spotify/callback`.


## VitaSearch v0.9.1 controls

Browser control system:
- X = select / mouse click
- O = back / close current keyboard/menu
- Square = open search/address keyboard
- Triangle = search/submit
- D-pad Up/Down = scroll page
- Left stick = move mouse pointer
- L/R = browser back/forward
- Touch = tap search bar or web content

The search/address bar is native VitaSearch chrome at the top of the screen.
Tapping it opens the keyboard. Touching web content acts as a mouse click.


## VitaSearch v0.9.2 Searchbar + Keyboard

- Touch searchbar: opens keyboard.
- Square: opens keyboard.
- Triangle: searches/submits the current text.
- O: closes keyboard and returns to browser.
- Touch outside the search field: closes keyboard.
- The typed value remains visible in the native searchbar.
- URLs/domains still open directly; normal text uses Google search.


## VitaSearch v0.9.3 Tabs

- Up to 6 browser tabs.
- Tabs are shown above the search bar.
- Touch a tab to select it.
- Touch the `x` area to close it.
- Touch `+` to create a new tab.
- D-pad Left/Right switches between tabs while browsing.
- Tabs share one Chromium BrowserContext, so cookies/login state are shared like a normal browser.
- Closing the final remaining tab resets it to Google instead of leaving the app without a page.


## VitaSearch v0.9.4 Settings

Settings is now split into dedicated categories:

- Browser
- Network
- Privacy
- Clear data
- Proxy / HTTPS
- Spotify
- Controls
- Appearance
- About VitaSearch

Clear data has its own page for cookies, search history, browser history, cache,
site data and all browser data. Controls has its own reference page with the
project's X/O/Square/Triangle, D-pad, L-stick, L/R and touch mappings.


## VitaSearch v0.9.5 Spotify Touch
Touch Previous, Play/Pause, Next, seek drag and volume drag are wired to the existing Spotify Connect API endpoints.


## VitaSearch v0.9.6 Network Status

The browser now checks `/health` on the VitaSearch proxy and displays live status:

- Green: proxy and internet are reachable.
- Yellow: proxy is reachable but internet check failed.
- Red: proxy is unreachable/offline.
- HTTPS state is shown.
- Response latency in milliseconds is shown.
- Settings -> Network displays the same detailed state.

The proxy health check tests outbound internet access with a small HTTPS request.


## VitaSearch v0.9.7 Default Search + Spotify Status

Settings now has a dedicated Default Search category:
- Use VitaSearch search: ON/OFF
- Search engine: Google / Bing / DuckDuckGo
- ON uses the selected engine for normal text searches.
- OFF uses the original Google fallback.

This changes the default search behavior inside VitaSearch. It does not modify the
PS Vita system browser's global default provider.

Spotify status is now live on the Vita side through `/spotify/native/status`:
Connected, Callback, Token state and current Connect device are displayed.
Access/refresh tokens remain stored on the proxy and are never returned to the UI.


## VitaSearch v0.99 RC1

Release-candidate stability pass:

- Added the missing libcurl include for the native network probe.
- Added a forward declaration for `refresh_frame()` used by tab helpers.
- Fixed the proxy middleware name used by tabs and Spotify status.
- Spotify status now has safe defaults before the first request.
- The browser retries proxy session creation after a connection loss.
- Package version is now `0.99.0-rc.1`.

This ZIP still needs a real VitaSDK compile in GitHub Actions before v1.0.


## v0.99 RC2 workflow fix
GitHub Actions host-tools installation now uses `apt-get` instead of Alpine `apk` for the current VitaSDK container.
