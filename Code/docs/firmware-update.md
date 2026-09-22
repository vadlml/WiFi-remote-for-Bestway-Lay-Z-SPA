# Firmware update over the air

The "Check firmware update" page (`chkupdatefw.html`) can install a new firmware and
new web files without a USB cable. There are two independent paths, plus the
unchanged ArduinoOTA/espota route for developers.

## Why the old implementation stopped working

The previous version asked GitHub for a smaller TLS record size
(`probeMaxFragmentLength`, RFC 6066 *maximum fragment length*) so BearSSL could work
with a 512 byte receive buffer. GitHub is served by Fastly, and Fastly no longer
answers that extension - a `ClientHello` with `max_fragment_length` comes back with a
`ServerHello` that simply leaves the extension out:

```
$ openssl s_client -connect raw.githubusercontent.com:443 -maxfraglen 512 -tls1_2 -trace
...
ServerHello extensions: server_name, extended_master_secret, renegotiate,
                        ec_point_formats, session_ticket     <- no max_fragment_length
```

Without it the server may use the full 16 kB record size, and it does: a plain
`GET /…/firmware.bin` arrives in 16408 byte records. BearSSL has to buffer a whole
record, and a ~16.7 kB buffer does not fit in the heap this firmware has left. That is
the "I couldn't get GitHub to send smaller chunks" problem.

## What makes it work now

Record size is chosen per response, and Fastly only ramps up to 16 kB records once a
single response body gets big. So the firmware never asks for a whole file: it sends
HTTP range requests of 4 kB on one keep-alive connection.

Measured against `raw.githubusercontent.com` (700 kB, 175 range requests, one
connection, no reconnects):

| request style | largest TLS record |
| --- | --- |
| whole file in one GET | 16408 bytes |
| `Range: bytes=0-4095` per request | 4120 bytes |
| handshake (certificate message) | 4145 bytes |

A 5120 byte BearSSL receive buffer therefore covers both the handshake and the
transfer, which brings the whole https session down to roughly 15 kB of heap. The
download resumes at the current offset if the connection breaks, so a dropped
connection costs one chunk, not the whole file.

This is why the chunk size cannot simply be raised: at 4 kB the response still fits in
the receive buffer, above that it does not.

## Path 1: the ESP downloads (`/fwcheck/`, `/fwupdate/`, `/fwstatus/`)

Implemented in [`src/fw_update.cpp`](../src/fw_update.cpp).

1. `manifest.json` is read and compared with `FW_VERSION`.
2. Optionally every file from `filelist.txt` is downloaded to `/dl.tmp` and renamed
   into place, so your settings files are untouched.
3. The binary named by the manifest (`firmware-<version>.bin`) is streamed into
   `Update`, verified against the md5 from the manifest, and the ESP reboots.

While this runs, pump communication and the websocket/MQTT clients are paused to free
heap, but the web server keeps answering, so the page shows live progress. If less than
~15 kB heap is free the update refuses to start and says so - use path 2 then.

The connection is verified against ISRG Root X1, which the GitHub chain is cross
signed by. If that ever changes you can upload your own root certificate as `ca.pem`
with the file uploader, or tick "skip certificate check" (the md5 check from the
manifest still applies).

## Path 2: the browser downloads (`/fwpush/`)

The page fetches `manifest.json` and the binary it names from
`raw.githubusercontent.com` (which sends `access-control-allow-origin: *`) and posts
the binary to `/fwpush/?size=…&md5=…`. The ESP only streams the multipart body into
`Update` - no TLS, no certificates, a few hundred bytes of buffers. The same button
also pushes the web files through the existing `/upload.html` handler.

The file picker on the same page installs a `firmware.bin` from your disk, which is
handy for your own builds.

> Release assets (`github.com/…/releases/download/…`) cannot be used by either path:
> they redirect to `release-assets.githubusercontent.com`, which sends no CORS header
> (so the browser refuses) and requires a second TLS connection. Files on a branch,
> served by `raw.githubusercontent.com`, work for both.

## Where the files come from

```
https://raw.githubusercontent.com/<owner>/<repo>/<branch>/<dir>/
    manifest.json     {"version": "…", "file": "firmware-<version>.bin", "size": …, "md5": "…"}
    firmware-<version>.bin
    firmware.bin      a copy under a fixed name, for manual downloads
    filelist.txt      one web file per line
    data/<file>       the web files, gzipped where useful
```

`raw.githubusercontent.com` answers with `cache-control: max-age=300` and caches
every path independently, and it ignores `no-cache`, `Pragma` and query strings
(all three tested). So for up to five minutes after a publish a device can read a
fresh `manifest.json` while the CDN still holds the previous `firmware.bin`.
Naming the binary after the version removes that: a manifest can only ever point
at the binary built with it. If the CDN has not got that name yet the download
fails with a clean "not found on github" instead of a wrong image, and the md5
from the manifest is still checked on top.

`owner`, `repo`, `branch`, `dir`, chunk size and the certificate check are editable on
the update page and stored in `/fwsource.json`. The defaults come from the git remote
of the checkout the firmware was built from (see
[`tools/fw_source_flags.py`](../tools/fw_source_flags.py)), branch `fw`, so a fork
updates from itself. Override at build time if you want:

```ini
build_flags = -DFW_UPDATE_OWNER='"someone"' -DFW_UPDATE_REPO='"some-repo"' -DFW_UPDATE_BRANCH='"fw"' -DFW_UPDATE_DIR='""'
```

## Publishing a build

The workflow [`.github/workflows/publish-fw.yml`](../../.github/workflows/publish-fw.yml)
builds the firmware on every push and force-pushes the update files to the `fw` branch
(one commit, no history, so the repository does not grow with every binary).

Locally the same folder is produced by:

```sh
cd Code
pio run -e nodemcuv2
python3 tools/make_release.py            # writes Code/fw/
```

`manifest.json` takes its version from `lib/BWC_unified/FW_VERSION.h`. The ESP only
offers an update when that string differs from the one it is running, so bump it when
you publish.
