# Deploying the web game and the relay (Apache, aarch64)

This guide puts the browser build of Bit Riot on an Apache server and runs the vnet relay that online games go through, on an aarch64 (64-bit ARM) Linux machine. The paths and package names are Arch Linux ARM's; other distributions differ only in where Apache keeps its configuration.

Placeholders used throughout (replace them with your own):

| Placeholder | Meaning |
|---|---|
| `game.example.org` | the public host name of the web server (and, in the simple layout, of the relay) |
| `/srv/http/games/bitriot` | the directory the game's files go in; the URL path is whatever your `DocumentRoot` maps it to (here `https://game.example.org/games/bitriot/`) |
| `relay.example.org` | the relay's host name, only if the relay runs on a different machine from Apache |

Nothing in the game needs to know its own URL path: the page loads its files relatively, so any directory works.

## The layout

The simplest layout, and the one this guide describes first, is **one machine running both Apache and the relay**:

```
browser ──HTTPS (TCP 443)──▶ Apache ──▶ /games/bitriot/       static files
        ──WSS  (TCP 443)──▶ Apache ──▶ /vnet ──▶ relay 127.0.0.1:8080 (WebSocket)
        ──QUIC (UDP 443)───────────────────────▶ relay :443        (WebTransport, optional)
native  ──UDP 7777─────────────────────────────▶ relay :7777
```

A page served from `https://game.example.org/...` uses `wss://game.example.org/vnet` as its relay **by default**, with no configuration: that is why putting the relay behind the same Apache is the easy path. (A page served over plain `http://` defaults to `ws://<host>:8080/vnet`; see [Without TLS](#without-tls).)

### Ports to open

On the machine's firewall, and forwarded from the router if the machine is behind NAT:

| Port | Protocol | For | Needed |
|---|---|---|---|
| 443 | TCP | Apache: the game's page and the browsers' WebSocket (`/vnet`) | yes |
| 80 | TCP | Apache: Let's Encrypt's HTTP challenge and the redirect to HTTPS | yes, if you use certbot's HTTP challenge |
| 7777 | UDP | the relay: native (desktop) players | yes, for native players |
| 443 | UDP | the relay: WebTransport (HTTP/3) for browsers | optional; browsers fall back to the WebSocket without it |

Do **not** open 8080 or 8081: the relay's HTTP and stats listeners stay on `127.0.0.1`, and only Apache talks to them. The relay makes no outbound connections.

## 1. Build the web game

The output is WebAssembly and JavaScript, which runs the same on any machine, so build it on your usual x86_64 PC (building on the aarch64 box itself has not been tried). The setup is the one in the top-level README's "Web build" section:

```bash
git clone --recurse-submodules --branch claude https://github.com/VenKamikaze/BitRiot.git ~/bitriot
cd ~/bitriot
scripts/setup-emsdk.sh                  # once; add --with-ports if GitHub archive downloads are blocked
source .emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm-deploy -DBITRIOT_WEB_TEST_FILES=OFF
cmake --build build-wasm-deploy -j"$(nproc)"
```

`-DBITRIOT_WEB_TEST_FILES=OFF` leaves the test-only golden record out of the download. The four files to deploy are in `build-wasm-deploy/`:

| File | About |
|---|---|
| `index.html` | the page |
| `BitRiot.js` | the loader |
| `BitRiot.wasm` | the game, 3.3 MB |
| `BitRiot.data` | the game's data and menus, 4.3 MB |

(`BitRiot.html` is a copy of `index.html` and is not needed.) Gzip roughly halves the transfer; Apache does it on the fly with the configuration below.

Copy them to the server:

```bash
rsync -av build-wasm-deploy/{index.html,BitRiot.js,BitRiot.wasm,BitRiot.data} \
    you@game.example.org:/srv/http/games/bitriot/
```

### If the relay is on another machine

Tell the page where it is: put one line before the game's script in the **copied** `index.html`.

```bash
sed -i 's|<script async src=BitRiot.js>|<script>window.BITRIOT_RELAY="wss://relay.example.org/vnet"</script>&|' index.html
```

(Repeat it after every rebuild.) A link with `?relay=wss://relay.example.org/vnet` overrides it per visitor, and the "Copy invite link" button under the game includes the relay in use.

## 2. Apache

Arch Linux ARM's `apache` package (2.4.47 or later is needed for the WebSocket proxying below). In `/etc/httpd/conf/httpd.conf`, make sure these modules are loaded (uncomment the lines):

```apache
LoadModule proxy_module modules/mod_proxy.so
LoadModule proxy_http_module modules/mod_proxy_http.so
LoadModule headers_module modules/mod_headers.so
LoadModule deflate_module modules/mod_deflate.so
LoadModule ssl_module modules/mod_ssl.so
LoadModule socache_shmcb_module modules/mod_socache_shmcb.so
LoadModule rewrite_module modules/mod_rewrite.so
```

and add `Include conf/extra/bitriot.conf` at the end. Then create `/etc/httpd/conf/extra/bitriot.conf` (fold the `<Directory>` block and the three proxy lines into your existing `:443` virtual host instead, if you have one):

```apache
Listen 443

<VirtualHost *:80>
    ServerName game.example.org
    DocumentRoot "/srv/http"
    # Everything to HTTPS, except Let's Encrypt's challenge files.
    RewriteEngine on
    RewriteCond %{REQUEST_URI} !^/\.well-known/acme-challenge/
    RewriteRule ^ https://%{HTTP_HOST}%{REQUEST_URI} [R=301,L]
</VirtualHost>

<VirtualHost *:443>
    ServerName game.example.org
    DocumentRoot "/srv/http"
    SSLEngine on
    SSLCertificateFile    /etc/letsencrypt/live/game.example.org/fullchain.pem
    SSLCertificateKeyFile /etc/letsencrypt/live/game.example.org/privkey.pem

    # The game: static files.
    <Directory "/srv/http/games/bitriot">
        Require all granted
        AddType application/wasm .wasm
        AddType application/octet-stream .data
        AddOutputFilterByType DEFLATE text/html text/javascript application/javascript application/wasm application/octet-stream
        # Revalidate on every load, so a redeploy reaches players at once (unchanged files still come back as 304).
        Header set Cache-Control "no-cache"
    </Directory>

    # The relay's WebSocket endpoint. Apache adds X-Forwarded-For, which the relay reads with -trust-forwarded-for.
    ProxyRequests off
    ProxyPass        /vnet http://127.0.0.1:8080/vnet upgrade=websocket
    ProxyPassReverse /vnet http://127.0.0.1:8080/vnet
</VirtualHost>
```

Notes:

- `Listen 443` is only needed if nothing else in your configuration already listens on 443 (Arch's stock `httpd-ssl.conf` does; don't include both).
- No `Cross-Origin-Opener-Policy`/`Cross-Origin-Embedder-Policy` headers are needed (the build uses no threads).
- `mod_proxy_wstunnel` is not needed: `upgrade=websocket` on `ProxyPass` (Apache 2.4.47+) does the job.

### The certificate

For a host reachable from the internet on TCP 80, `certbot` gets a Let's Encrypt certificate (`pacman -S certbot`). Get it first with the 443 virtual host commented out, or with `--standalone` while Apache is stopped, then enable the 443 virtual host:

```bash
sudo certbot certonly --webroot -w /srv/http -d game.example.org
sudo systemctl enable --now certbot-renew.timer      # the timer Arch's certbot package ships; systemctl list-timers shows it
```

Add a deploy hook that reloads Apache after a renewal (`/etc/letsencrypt/renewal-hooks/deploy/httpd.sh`: `#!/bin/sh` and `systemctl reload httpd`, made executable). If TCP 80 is not reachable from the internet, use certbot's DNS challenge instead.

Then:

```bash
sudo apachectl configtest && sudo systemctl enable --now httpd    # or: systemctl reload httpd
```

## 3. The relay on aarch64

The relay is one static Go binary with no runtime dependencies (`ext/vnet/relay` in this repository; its README has every flag).

### Build it

Either cross-compile on the PC (from the same checkout):

```bash
cd ~/bitriot/ext/vnet/relay
GOOS=linux GOARCH=arm64 CGO_ENABLED=0 go build -trimpath -o vnet-relay-arm64 ./cmd/vnet-relay
scp vnet-relay-arm64 you@game.example.org:/tmp/vnet-relay
```

or build it on the aarch64 machine (Go 1.24 or later):

```bash
sudo pacman -S --needed go git
git clone https://github.com/VenKamikaze/vnet.git ~/vnet       # or the BitRiot checkout's ext/vnet
cd ~/vnet/relay && CGO_ENABLED=0 go build -trimpath -o /tmp/vnet-relay ./cmd/vnet-relay
```

Install it and give it its own user:

```bash
sudo install -m 755 /tmp/vnet-relay /usr/local/bin/vnet-relay
sudo useradd --system --no-create-home --shell /usr/bin/nologin vnet-relay
```

Check that it runs: `vnet-relay -udp 127.0.0.1:0 -http 127.0.0.1:0` prints a `LISTEN ...` line (Ctrl+C to stop).

### WebTransport's certificate (optional)

WebTransport needs the relay to hold a real certificate for the page's host name (the same Let's Encrypt one as Apache's). The relay runs as `vnet-relay`, which cannot read `/etc/letsencrypt`, so give it a copy that a certbot hook refreshes. `/etc/letsencrypt/renewal-hooks/deploy/vnet-relay.sh`:

```sh
#!/bin/sh
# Copies the renewed certificate where the relay can read it; the relay notices new files within a minute, no restart.
set -eu
lineage="${RENEWED_LINEAGE:-/etc/letsencrypt/live/game.example.org}"
d=/etc/vnet-relay/certs
install -d -m 750 -o root -g vnet-relay "$d"
install -m 644 -o root -g vnet-relay "$lineage/fullchain.pem" "$d/fullchain.pem"
install -m 640 -o root -g vnet-relay "$lineage/privkey.pem"   "$d/privkey.pem"
```

```bash
sudo chmod +x /etc/letsencrypt/renewal-hooks/deploy/vnet-relay.sh
sudo /etc/letsencrypt/renewal-hooks/deploy/vnet-relay.sh      # once now, for the first copy
```

QUIC wants bigger UDP buffers than the default (it works without, with less headroom):

```bash
printf 'net.core.rmem_max=7500000\nnet.core.wmem_max=7500000\n' | sudo tee /etc/sysctl.d/90-vnet-relay.conf
sudo sysctl --system
```

Skip this section, and the `-wt` flags below, to run without WebTransport.

### The service

`/etc/systemd/system/vnet-relay.service`:

```ini
[Unit]
Description=vnet relay (Bit Riot online games)
After=network-online.target
Wants=network-online.target

[Service]
User=vnet-relay
Group=vnet-relay
ExecStart=/usr/local/bin/vnet-relay \
    -udp :7777 \
    -http 127.0.0.1:8080 -trust-forwarded-for \
    -origins game.example.org \
    -stats 127.0.0.1:8081 \
    -wt :443 -wt-cert /etc/vnet-relay/certs/fullchain.pem -wt-key /etc/vnet-relay/certs/privkey.pem
# Binding UDP 443 (WebTransport) needs this; drop it, and the -wt line, without WebTransport.
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload && sudo systemctl enable --now vnet-relay
journalctl -u vnet-relay -f        # the LISTEN line, then room activity
```

What the flags do here:

- `-udp :7777`: native players, on every interface.
- `-http 127.0.0.1:8080 -trust-forwarded-for`: the WebSocket listener, reachable only by Apache; the relay takes each browser's address from the `X-Forwarded-For` header Apache adds, so its per-address limits count players, not Apache. Never use `-trust-forwarded-for` on a listener the internet can reach directly.
- `-origins game.example.org`: only pages from your site may open sessions. It is matched against the browser's `Origin` exactly, **including a port**: a page served on a non-standard port, say 8443, needs `-origins game.example.org:8443`. List several, comma separated.
- `-stats 127.0.0.1:8081`: counters as JSON, local only (`curl -s 127.0.0.1:8081/stats`). Optional.
- `-wt :443 ...`: WebTransport on UDP 443, so the URL the browser derives from `wss://game.example.org/vnet`, `https://game.example.org/vnet`, needs no port. Apache does not use UDP 443 (it has no HTTP/3), so the two do not clash.

The defaults are fine for a home server: 8 players per room, 15 s before a silent player is dropped. Every limit is per source address, so a LAN party behind one NAT shares one budget (128 players, 16 rooms); the relay's README lists the flags to raise them.

### The relay on a different machine

Run it the same way, with its own certificate for `relay.example.org` and a TLS proxy in front of `/vnet` (Apache as above, or the Caddy and nginx snippets in the relay's README), set `window.BITRIOT_RELAY` in the page as in step 1, and give it `-origins game.example.org` (the *page's* host, not the relay's). Open the ports from the table on the relay's machine instead.

## 4. Check it

From any machine:

```bash
curl -sI https://game.example.org/games/bitriot/BitRiot.wasm -H 'Accept-Encoding: gzip' | grep -iE '^HTTP|content-type|content-encoding'
#   HTTP/1.1 200 OK / Content-Type: application/wasm / Content-Encoding: gzip

curl -si -N --max-time 3 https://game.example.org/vnet \
    -H 'Connection: Upgrade' -H 'Upgrade: websocket' -H 'Sec-WebSocket-Version: 13' \
    -H 'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==' -H 'Origin: https://game.example.org' | head -1
#   HTTP/1.1 101 Switching Protocols     (403: the Origin is not in -origins; 502/503: the relay is not running)
```

On the server, `curl -s 127.0.0.1:8080/healthz` answers `ok`.

Then play:

1. Open `https://game.example.org/games/bitriot/`, click to start, **Online game**, **Host via relay**, **Open room**. A six-letter room code appears in the lobby and, copyable, under the game.
2. In another browser (or a private window), the same page, **Online game**, **Join by room code**: the relay field is already filled in; type the code, **Connect**. Or use the host's "Copy invite link".
3. A native player: **Online game**, **Join by room code** with `game.example.org` as the relay (it uses UDP 7777), or from a terminal `BitRiot --relay game.example.org --room CODE`. A native player can host too: `BitRiot --host --relay game.example.org`. (A native build configured with `-DBITRIOT_DEFAULT_RELAY=game.example.org` offers that relay by default.)
4. With WebTransport on, `journalctl -u vnet-relay` shows the sessions; a browser that cannot reach UDP 443 falls back to the WebSocket after about 3 s, so play works either way.

## Without TLS

Plain HTTP works for a LAN, but browsers allow WebTransport only on HTTPS pages, so it is WebSocket only. A page served from `http://game.example.org/...` defaults to `ws://game.example.org:8080/vnet`, the relay's own port. Either open TCP 8080 and run the relay with `-http :8080` (and without `-trust-forwarded-for`), or keep it on `127.0.0.1:8080`, put the `ProxyPass` lines in the port-80 virtual host, and set `window.BITRIOT_RELAY="ws://game.example.org/vnet"` in the page.

## Updating

- **The game:** rebuild, copy the four files again (and re-apply the `index.html` line if you use one). Players get the new version on their next page load. A browser player and a native player must run the same build to play together: the relay itself does not care, but the game refuses a different build or protocol at the lobby with a message saying so.
- **The relay:** replace the binary, `systemctl restart vnet-relay`. A restart ends every room in progress.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| The page loads but "Host via relay" fails with a network error | the relay is not running, or Apache's `/vnet` proxy is missing (`curl` check above) |
| WebSocket upgrade answers 403 | the page's `Origin` (with its port) is not in `-origins` |
| The page stays on "Loading..." | the `.wasm` is served with the wrong type: check `AddType application/wasm .wasm` |
| Native players cannot connect | UDP 7777 is not open or not forwarded |
| Browsers always use `relay-ws`, never `relay-wt` | UDP 443 is closed, `-wt` is off, or the relay cannot read its certificate copy (see its log) |
| The relay logs a certificate warning | the renewal hook did not run: run it by hand and check `certbot renew --dry-run` |

This setup was checked end to end in a container (Ubuntu's Apache 2.4.58, a self-signed certificate, the game in a subdirectory, the arm64 relay binary under qemu): two browsers joined a room over `wss://` through Apache's `/vnet` proxy, a native player joined over UDP 7777, and the three played a round together. WebTransport could not be checked there (it needs a certificate the browser trusts); it has its own tests in vnet.
