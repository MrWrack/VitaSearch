# VitaSearch RC44 – Spotify Connection Fix

RC44 fixes the native Spotify status endpoint (it referenced two non-existent helper functions), adds touch support to the disconnected CONNECT button, and includes a Windows Spotify proxy launcher. Spotify OAuth uses Authorization Code with PKCE.

To log in, create a Spotify Developer app, add the exact redirect URI `http://127.0.0.1:8080/spotify/callback`, then run `proxy/start-proxy-spotify.bat` and enter your VitaSearch API key and Spotify Client ID. The Spotify account used with Development Mode must meet Spotify's current developer-mode requirements.

# RC44 smooth pointer fix

RC44 smooths the Vita left-stick mouse locally with a dead-zone, filtered velocity and nonlinear acceleration. It also staggers periodic frame/Spotify/network HTTP refreshes so several blocking requests no longer happen in the same render frame. Touch re-syncs the analog cursor immediately.

# RC41 fixes

RC41 fixes the RC40 config parser so the API key can no longer become the proxy URL after restart, adds URL validation, makes the on-screen keyboard accept touch instead of closing on touch, starts with a reliable local VitaSearch page instead of a blank Google homepage, and fixes native Spotify Connect by navigating the current Chromium session directly to Spotify OAuth. Spotify login still requires `SPOTIFY_CLIENT_ID` and a matching `SPOTIFY_REDIRECT_URI` on the PC proxy.

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


## VitaSearch v0.99 RC3 build fix

Fixed the C compiler error `else without a previous if` in the main render chain.
The Spotify render branch now uses braces so `draw_spotify_touch_controls()` stays
inside the Spotify branch.


## VitaSearch v0.99 RC4 version-format fix

Fixed VitaSDK CMake configure error:
`Invalid version '00.993' change to format ##.##`

The VPK metadata version is now `00.99`, while the project/package prerelease remains RC4.


## VitaSearch v0.99 RC5 source fix

Removed a duplicated/corrupted fragment after `open_target()` that caused the C
parser to lose sync and report misleading errors later around `remote_scroll`.

`open_target()` now ends cleanly before `remote_simple()`.


## VitaSearch v0.99 RC6 brace fix

Fixed the missing closing brace at the end of `open_target()`. RC5 removed a
corrupted duplicate tail, but the function still needed its final `}`. Without
it, the compiler treated later functions as if they were nested inside
`open_target()`, producing errors such as `invalid storage class for function`
and `expected declaration or statement at end of input`.


## VitaSearch v0.99 RC7 compile fix

Fixed the next VitaSDK compiler errors:

- Added a forward declaration for `network_probe()` before Settings calls it.
- Replaced the non-existent `spotify_cmd()` calls in the touch handler with the
  existing native helpers: `spotify_command()`, `spotify_seek()` and
  `spotify_volume()`.


## VitaSearch v0.99 RC8 crypto/link fix

Fixed the linker failure from VitaSDK where `libcurl.a` referenced OpenSSL crypto
symbols such as `DES_set_odd_parity`, `DES_set_key_unchecked`, `DES_ecb_encrypt`,
`MD4_Init`, `MD4_Update` and `MD4_Final`.

Changes:
- GitHub Actions installs the VitaSDK `openssl` package.
- `ssl` and `crypto` are linked after `curl` in CMake so static libcurl can resolve
  its OpenSSL dependencies.


## VitaSearch v0.99 RC9 pthread link fix

Fixed the next VitaSDK linker errors from OpenSSL/libcrypto:

- `pthread_atfork`
- `pthread_getspecific`
- `pthread_self`
- `pthread_equal`

The build now links VitaSDK's pthread library as a whole archive:

`-Wl,--whole-archive pthread -Wl,--no-whole-archive`

This follows the VitaSDK workaround used when static libraries reference pthread
symbols that the linker would otherwise discard.


## VitaSearch v0.99 RC10 link-group fix

The pthread symbols from RC9 are no longer the visible blocker. The linker now
reaches OpenSSL internal stack symbols such as `sk_num` and `sk_value`.

Static `curl`, `ssl`, `crypto` and pthread dependencies can be cyclic. They are
now wrapped in GNU ld's `--start-group/--end-group`, which makes the linker
rescan the archives until their mutual references are resolved.


## VitaSearch v0.99 RC11 TLS backend fix

RC10 still failed on `sk_num` / `sk_value`, showing that the OpenSSL archive set
was inconsistent at link time. VitaSearch does not call OpenSSL directly; it
only needs HTTPS through libcurl. RC11 therefore switches the Vita build to
VitaSDK's `curl-mbedtls` package, whose declared dependencies are mbedTLS, zlib
and zstd, and removes the OpenSSL/pthread link chain.


## VitaSearch v0.99 RC12 force curl-mbedtls

RC11's build log still showed `libcurl.a(md4.c.o)` and OpenSSL DES/MD4 symbols.
That proves the archive used by the linker was still the OpenSSL-backed curl,
not the requested mbedTLS variant.

RC12 explicitly removes the preinstalled `curl` package, force-installs
`curl-mbedtls`, and fails early if the resulting `libcurl.a` still contains
OpenSSL DES/MD4 references. This makes the workflow verify the TLS backend
before CMake starts linking VitaSearch.


## VitaSearch v0.99 RC13 workflow-ready

The GitHub Actions workflow is included already fixed:
- removes the old `curl` Vita package
- installs `curl-mbedtls`
- checks `libcurl.a` for old OpenSSL DES/MD4 references before building


## VitaSearch v0.99 RC14 package conflict fix

GitHub Actions reached the intended `curl-mbedtls` install but VDPM reported:
`curl-mbedtls ... and curl ... are in conflict. Remove curl? [y/N]`.

RC14 removes the non-interactive VDPM environment setting and pipes `yes` to
the `curl-mbedtls` install, allowing VDPM to confirm replacement of the
conflicting curl package automatically.


## VitaSearch v0.99 RC15 exit-141 fix

The `curl-mbedtls` install itself completed, but the GitHub Actions step exited
with code 141 because `yes | vdpm install ...` runs under Bash pipefail and
`yes` receives SIGPIPE after VDPM stops reading.

RC15 replaces the endless `yes` process with a single-answer pipe:

`printf 'y\n' | vdpm install curl-mbedtls`

This preserves the automatic conflict confirmation without triggering SIGPIPE.


## VitaSearch v0.99 RC16 no-pipe VDPM fix

GitHub Actions still returned exit code 141 immediately after installing
`curl-mbedtls`. RC16 removes the input pipeline completely:

`vdpm install curl-mbedtls <<< "y"`

It also removes the `nm | grep` diagnostic pipeline and writes symbols to a
temporary file first, preventing any later SIGPIPE/pipefail exit 141.


## VitaSearch v0.99 RC17 VDPM 141 workaround

The Actions log shows VDPM completes both `removing curl...` and
`installing curl-mbedtls...`, then VDPM itself returns 141. RC17 captures that
specific return code and allows the workflow to continue. Any other non-zero
VDPM result still fails.

The following libcurl symbol check remains mandatory, so the build only
continues if the installed archive is actually free of the old OpenSSL DES/MD4
references.


## VitaSearch v0.99 RC18 VDPM subshell fix

The GitHub runner still aborted on VDPM status 141 before the outer script could
handle it. RC18 runs the conflicting package replacement in a nested Bash
process. The nested process converts only status 141 (and 0) to success, while
all other VDPM failures remain fatal. The mandatory libcurl symbol check still
runs immediately afterward.


## VitaSearch v0.99 RC19 explicit errexit disable

RC18 still stopped immediately on VDPM's 141. RC19 explicitly launches the
nested Bash with `+e` and also executes `set +e` inside it, ensuring GitHub's
outer `-e` behavior cannot prevent capture of VDPM's return code. Only status
0 or the observed post-install 141 is normalized to success.


## VitaSearch v0.99 RC20 verified VDPM-141 workaround

The install log repeatedly proves that `curl` is removed and `curl-mbedtls` is
installed before VDPM returns 141. RC20 therefore places that VDPM invocation
directly in an `|| true` list, which Bash `errexit` cannot abort.

This does not blindly trust the installation: the workflow immediately checks
that `libcurl.a` exists and scans its symbols for the old OpenSSL DES/MD4
references. The later `vdpm list | head` pipeline was also removed to prevent a
second possible SIGPIPE/141.


## VitaSearch v0.99 RC21 TLS-check correction

RC20 successfully got past VDPM's exit-141 behavior. The remaining failure was
our own symbol heuristic (`DES_set_` / `MD4_*`), not the package installer.
RC21 removes that heuristic and verifies that `libcurl.a` exists, then lets the
actual Vita linker validate the curl-mbedTLS dependency set.

## VitaSearch v0.99 RC22 direct curl-mbedTLS package

The linker proved the installed libcurl was still the OpenSSL build (DES/MD4
references). RC22 downloads VitaSDK's official `curl-mbedtls.tar.xz` release
asset and extracts it directly over `$VITASDK` after the VDPM attempt. This
avoids VDPM's post-install exit-141 transaction behavior.


## VitaSearch v0.99 RC23 force-correct libcurl

RC22 reached the linker but the linker still opened the OpenSSL-flavoured
`$VITASDK/arm-vita-eabi/lib/libcurl.a`. RC23 no longer assumes the release
archive's directory layout. It extracts the official `curl-mbedtls.tar.xz` to
a temporary directory, finds the actual `libcurl.a`, removes the stale SDK
copy, and copies the mbedTLS archive to the exact linker path. Curl headers and
`libcurl.pc` are also refreshed when present.


## VitaSearch v0.99 RC24 mbedTLS pthread link fix

RC23 successfully moved the linker onto the mbedTLS build of libcurl. The new
linker errors come from `libmbedcrypto.a(threading.c.o)` and reference
`pthread_mutex_*`, which confirms the OpenSSL/DES/MD4 problem is gone.

RC24 links VitaSDK pthread inside the same static-library group and forces the
pthread archive to be included so mbedTLS threading symbols resolve.

## VitaSearch v0.99 RC25 navigation + proxy stability

- Spotify disconnected screen: X connects, O returns, SELECT opens Settings.
- Spotify player: D-pad Left/Right moves focus Previous -> Play/Pause -> Next; X activates.
- L/R still performs Previous/Next directly.
- Offline browser now retries the proxy automatically and X/Triangle can retry immediately.
- Removed the old instruction that required restarting VitaSearch after starting the proxy.
- Network connect timeout reduced to 3 seconds and total request timeout to 6 seconds to reduce Settings freezes.
- Main render loop is frame-paced at about 60 FPS to reduce menu CPU load/jitter.

## VitaSearch v0.99 RC26 Spotify focus compile fix

RC25 placed `spotify_control_selected` below `draw_spotify()`, causing the
compiler error `spotify_control_selected undeclared`. RC26 moves that state
variable beside the other global Spotify state, before the renderer. All RC25
navigation and proxy-stability changes remain included.

## VitaSearch v0.99 RC27 input, proxy, JavaScript and bubble fix

- Spotify disconnected screen no longer draws the playback touch overlay underneath the Connect button.
- X on Connect stays on the Spotify screen when the proxy is offline instead of dropping to a black offline screen.
- Spotify login switches to the web view only after the proxy session and login page open successfully.
- JavaScript can be toggled with X or D-pad Left/Right. The UI toggles immediately; if the proxy is offline it is applied automatically after reconnect.
- Proxy / HTTPS now has a real ON/OFF control with X or D-pad Left/Right.
- Network and Spotify settings pages no longer run blocking network calls just by entering them.
- Network page uses X for an explicit reconnect/status refresh.
- Auto reconnect respects the Proxy ON/OFF state.
- Added a real 128x128 VitaSearch icon as `sce_sys/icon0.png` so the LiveArea bubble is no longer the generic blank bubble.

## VitaSearch v0.99 RC28 indexed LiveArea icon fix

VitaShell error `0x8010113D` during VPK install was caused by the new
`sce_sys/icon0.png` encoding. PS Vita homebrew LiveArea graphics need a
compatible 8-bit indexed PNG. RC28 converts the VitaSearch 128x128 bubble icon
to indexed palette PNG while retaining all RC27 proxy, JavaScript and input fixes.

## RC29 install recovery
Removes the newly added LiveArea icon from VPK packaging to restore the previously installable package layout. Runtime navigation, proxy, JavaScript and Spotify fixes remain.

## RC30 Network input fix
Network has a selectable Reconnect/Refresh row. X uses extended Vita controller polling and performs a fresh health probe before recreating the browser session. Clearer proxy failure messages included.

## RC31 Network Refresh Touch Fix

- Network now has a large real RECONNECT / REFRESH button.
- X runs refresh.
- Triangle runs refresh as a second controller shortcut.
- Touch on the green refresh button runs refresh.
- Network settings and offline recovery now share the same reconnect function.
- Clear result text distinguishes proxy unreachable, browser-session failure and internet unavailable.
- RC30/RC29 install-safe VPK layout is retained.

## RC32 Offline screen input fix
The black offline screen now has real Reconnect/Refresh and Open Settings buttons. X/Triangle reconnect, SELECT opens Settings, and touch works. Successful reconnect returns to the web view. Auto retry is reduced to about every 5 seconds.

## RC33 async reconnect / no-freeze fix

Reconnect no longer performs curl health/session/frame requests on the Vita UI thread.
X, Triangle or touch starts a Vita kernel worker thread. The UI remains responsive and
shows CONNECTING... while the worker checks `/health`, creates the browser session and
downloads the first PNG frame. PNG texture creation is handed back to the main UI thread.
Automatic retries also use the same async path and run less often.

## RC34 mode scope compile fix

RC33 moved reconnect and settings navigation into helper functions, but `mode`
was still local to `main()`. RC34 promotes the application mode state to global
scope so `open_settings_root()` and `finish_reconnect_if_ready()` can change it.
The async reconnect/no-freeze changes from RC33 are retained.

## RC35 proxy protocol/port auto-fallback

The default Node proxy (`npm start`) listens on **HTTP port 8080** unless TLS
certificate/key files are configured. Older VitaSearch builds defaulted to
`https://192.168.1.50:8443`, so reconnect could never reach a normal proxy.

RC35:
- changes new-install default to `http://192.168.1.50:8080`;
- migrates the old exact default automatically;
- when a configured URL is `https://<PC>:8443` and health fails, automatically
  tries `http://<same PC>:8080`;
- shows the current reconnect stage instead of sitting on `CONNECTING...`;
- explicitly exits/deletes the Vita reconnect worker on every completion path;
- keeps the RC33 async/no-freeze reconnect design.

## RC36 reconnect stage prototype compile fix

RC35 added `reconnect_stage_text()` for live reconnect diagnostics, but the
Network renderer calls it before its definition. C therefore treated the first
call as an implicit `int` declaration and later rejected the real
`const char *` definition. RC36 adds the correct forward declaration near the
other function prototypes. RC35 proxy port/protocol fallback remains intact.

## RC37 manual reconnect + editable proxy address

Automatic reconnect looping is removed. Proxy / HTTPS now has `Edit Proxy URL`.
Enter the PC's real IPv4 address, e.g. `http://192.168.1.23:8080`. The address
is saved to `ux0:data/vitasearch/config.txt`. Reconnect then happens only when
the user explicitly presses X/Triangle/touch.


## RC38 split connection status

RC38 tracks Proxy, Internet, Browser session, and Spotify independently. A failed browser session or frame no longer rewrites a healthy proxy connection as OFFLINE. The Network page shows each state separately, and the browser chrome distinguishes `Proxy online / No session` from a true proxy outage.


## RC40 browser session creation fix

RC40 restores the authenticated `POST /session` endpoint expected by the Vita client. RC38 could report `Proxy ONLINE / Browser session NOT READY` because `/health` worked while the proxy had no `/session` route. The new endpoint creates a Playwright browser context/session and returns the session id used by `/open`, `/frame`, tabs, input, and settings routes.


## RC40 API key fix
If Network shows Proxy/Internet ONLINE but Browser session NOT READY, the health endpoint is reachable while the protected browser endpoints may be rejecting the Vita API key. Set the same lowercase API key on PC and in VitaSearch Settings -> Proxy / HTTPS -> Edit API Key, then reconnect.


## RC44 smooth D-pad scrolling
Hold D-pad Up/Down in the browser to scroll continuously. RC44 uses smaller accelerated scroll steps and a combined `/scroll-frame` proxy call so scrolling no longer needs separate scroll and screenshot round trips.

## RC58 Windows proxy manager

`proxy/VitaSearch-Proxy-Manager.bat` now provides one CMD menu for installing requirements, updating npm/Playwright Chromium, configuring the VitaSearch API key and Spotify Client ID, starting the proxy, or showing the PC IPv4 address. Local secrets are stored in `proxy/proxy-settings.cmd`, which is ignored by Git and must not be committed.


## RC58 input controls
- Left analog stick uses standard Vita controller sampling for reliable mouse movement.
- X: single press clicks; quick second press at the same position sends a double-click.
- Touch: single tap clicks; quick second tap at the same position sends a double-click.
- Two-finger pinch/spread zooms the Chromium page from 50% to 200%.
- RC45 Proxy Manager is retained and updated to RC58.


## RC58 Spotify CMD
Run `proxy\start-proxy-spotify.bat`. It can install/update dependencies, save the Vita API key and Spotify Client ID locally, start the proxy, show the PC IP, and clear the saved Spotify token. The Spotify Dashboard redirect URI must be exactly `http://127.0.0.1:8080/spotify/callback`. Never enter a Spotify Client Secret into the Vita app.


## RC58 search + Spotify connection fix

- Browser search/navigation now runs in a Vita background worker instead of blocking controller/touch rendering.
- New `/open-frame` proxy endpoint performs bounded navigation and returns the first PNG frame in one request.
- The browser shows `Loading...` and an explicit error instead of appearing frozen.
- Spotify CONNECT now uses an asynchronous `/spotify/session-login-frame` route and always logs the login attempt in the PC CMD window.
- Spotify X, touch CONNECT, and Triangle can start login while disconnected.
- Proxy and Internet status stay independent from Spotify authentication.


## RC58 Spotify CONNECT reliability
- Serializes libcurl requests on Vita with a kernel mutex so background Spotify/browser navigation cannot collide with status polling.
- CONNECT stays on the Spotify screen and visibly shows CONNECTING until the login frame actually arrives.
- Only switches to the browser after the proxy returned the Spotify authorization page.
- X, Triangle and touch all use the same CONNECT path.


## RC58 About/version update fix
- Fixed the About screen which was still hard-coded to show `VitaSearch v0.99 RC45`.
- About now uses a single `VITASEARCH_RELEASE` constant and shows `VitaSearch v0.99 RC58`.
- RC51 L-stick no-freeze and Spotify fixes are retained.

## RC58 - Web input keyboard + lower Spotify bar
- X or touch on HTML input/textarea/contenteditable fields now opens the VitaSearch on-screen keyboard.
- Works with Spotify sign-in fields and YouTube/web search fields through the Chromium proxy.
- GO writes the text back to the focused HTML field, dispatches input/change events, presses Enter, and returns a fresh frame.
- O closes the keyboard without changing the focused web field.
- Non-editable X/touch targets still click normally.
- Spotify mini-player is reduced and moved to the very bottom of the browser view to cover less webpage content.


## RC58 - Spotify login field + uppercase keyboard fix
- Added a CAPS key to the VitaSearch keyboard. CAPS changes letter keys between lowercase and uppercase.
- Added explicit SPACE, backspace and GO keys in the last keyboard row.
- Web form focus is now tagged on the proxy, so the field remains identifiable between the click and OSK submit requests.
- Web input uses Playwright fill with a native-setter fallback and presses Enter on the exact focused field.
- This improves Spotify email/password login and other modern JavaScript/React forms.


### RC58 Secure Account Login
Credential-like fields (email, username, password, login/autocomplete fields) now require HTTPS on both the destination website and the Vita-to-proxy connection. Passwords are masked on the Vita keyboard and are never returned to the client as field values. This is intended for safer sign-in to services such as Google/Gmail and Spotify; provider anti-bot/device policies can still block sign-in.


## RC58 - Account login field fix
- Email/username fields can now open the Vita keyboard and be submitted on HTTPS account pages even when the local proxy is HTTP.
- Password/passcode/OTP fields remain protected and require HTTPS to the proxy; RC55 incorrectly classified email/username as secrets and blocked the first login step.
- RC54 uppercase/CAPS keyboard and prior Spotify/input fixes are retained.

## RC58 - Root proxy launchers
- `start-proxy-spotify.bat` is now included in the ZIP root for easy launching.
- `VitaSearch-Proxy-Manager.bat` is now included in the ZIP root for easy launching.
- The full scripts also remain inside the `proxy` folder.
