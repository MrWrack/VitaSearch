# VitaSearch security

VitaSearch is designed to keep the Chromium proxy on a trusted LAN. Do not expose port 8080 directly to the public Internet.

## Defaults

- Set `VITASEARCH_API_KEY` to a random value of at least 24 characters.
- Put the same key on line 2 of `ux0:data/vitasearch/config.txt`.
- Keep `ALLOW_PRIVATE_TARGETS=0` unless you intentionally need intranet browsing.
- Keep `.env` and `spotify-token.json` private; both are ignored by Git.
- Use a VPN or TLS reverse proxy on untrusted networks.

## Privacy

`POST /privacy/clear` accepts `what`: `cookies`, `cache`, `site-data`, `history`, or `all`.
`POST /privacy/clear-spotify-token` removes the saved Spotify token.
Both endpoints require the VitaSearch API key.

## Reporting

Do not post API keys, Spotify tokens, cookies or private URLs in public GitHub issues/build logs.


## HTTPS
VitaSearch 0.7 supports TLS 1.2+ between the Vita and proxy. Certificate and hostname verification stay enabled. Custom CA certificates can be supplied via line 3 of the Vita config. Never commit private TLS keys or real API keys.
