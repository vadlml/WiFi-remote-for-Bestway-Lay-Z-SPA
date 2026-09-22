#include "fw_update.h"

#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <LittleFS.h>
#include <Updater.h>
#include <ArduinoJson.h>
#include <time.h>

#include "bwc_debug.h"
#include "FW_VERSION.h"

namespace fwupdate
{
/*
 * Defaults. owner/repo/branch can be changed from the web ui and are stored
 * in /fwsource.json. The files are expected at
 * raw.githubusercontent.com/<owner>/<repo>/<branch>/<dir>/
 *   manifest.json   {"version":"...","file":"firmware-<version>.bin","size":123456,"md5":"32 hex chars"}
 *   firmware-<version>.bin (and a firmware.bin copy for manual downloads)
 *   filelist.txt    one file name per line (as produced by gzip_littlefs.py)
 *   data/<file>     the web files listed in filelist.txt
 */
#ifndef FW_UPDATE_OWNER
    #define FW_UPDATE_OWNER "visualapproach"
#endif
#ifndef FW_UPDATE_REPO
    #define FW_UPDATE_REPO "WiFi-remote-for-Bestway-Lay-Z-SPA"
#endif
#ifndef FW_UPDATE_BRANCH
    #define FW_UPDATE_BRANCH "fw"
#endif
#ifndef FW_UPDATE_DIR
    /* empty = the files sit in the root of the branch, as published by the
       publish-fw workflow. Use "Code/fw" if you commit them into the tree. */
    #define FW_UPDATE_DIR ""
#endif

static const char GH_HOST[] PROGMEM = "raw.githubusercontent.com";
static const uint16_t GH_PORT = 443;
static const char CFG_FILE[] PROGMEM = "/fwsource.json";
static const char CA_FILE[] PROGMEM = "/ca.pem";
static const char TMP_FILE[] PROGMEM = "/dl.tmp";

/* GitHub packs the answer to one range request into as few TLS records as it
   can (measured: a 4096 byte body arrives as one 4120 byte record), so the
   chunk size has to stay below what our receive buffer can hold. Ask for the
   whole file at once and the records grow to 16 kB, which is exactly what broke
   the old implementation. */
static const uint16_t CHUNK_DEFAULT = 4096;
static const uint16_t CHUNK_MAX = 4096;
/* holds the biggest record: the 4.2 kB certificate one during the handshake */
static const uint16_t TLS_RX_BUFFER = 5120;
static const uint16_t TLS_TX_BUFFER = 512;
static const uint16_t READ_BUFFER = 512;
static const uint32_t NET_TIMEOUT = 10000;
static const uint8_t MAX_RETRIES = 6;
/* heap needed for the TLS session (buffers + BearSSL stack + contexts) */
static const uint32_t HEAP_NEEDED = TLS_RX_BUFFER + 10000;

/* ISRG Root X1. The GitHub certificate chain ends in an intermediate that is
   cross signed by this root. Upload your own /ca.pem to override it. */
static const char TRUST_ROOT[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)CERT";

enum Request : uint8_t
{
    REQ_NONE = 0,
    REQ_CHECK,
    REQ_UPDATE
};

static Source _source = {
    FW_UPDATE_OWNER, FW_UPDATE_REPO, FW_UPDATE_BRANCH, FW_UPDATE_DIR,
    CHUNK_DEFAULT, false
};
static Progress _progress = {STATE_IDLE, 0, 0, String(), String(), String(), String(), 0};
static Request _request = REQ_NONE;
static bool _with_files = false;
static bool _running = false;
static uint32_t _reboot_at = 0;

static void (*_yield_cb)() = nullptr;
static void (*_prepare_cb)() = nullptr;
static void (*_resume_cb)() = nullptr;

static BearSSL::WiFiClientSecure* _client = nullptr;
static BearSSL::X509List* _anchors = nullptr;
static BearSSL::Session* _session = nullptr;

struct Response
{
    int status;
    uint32_t length;
    uint32_t total;
    bool close;
};

/** data sink for one download. Return false to abort the download */
typedef bool (*sink_fn)(const uint8_t* data, size_t len, void* ctx);

static void spin()
{
    ESP.wdtFeed();
    if(_yield_cb) _yield_cb();
    else yield();
}

/*******************************************************************************
 * configuration
 ******************************************************************************/
void load_source()
{
    StaticJsonDocument<512> doc;
    File file = LittleFS.open(FPSTR(CFG_FILE), "r");
    if(!file) return;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if(error)
    {
        BWC_LOG_P(PSTR("FW > %s is not valid json\n"), String(FPSTR(CFG_FILE)).c_str());
        return;
    }
    if(doc.containsKey(F("owner"))) _source.owner = doc[F("owner")].as<const char*>();
    if(doc.containsKey(F("repo"))) _source.repo = doc[F("repo")].as<const char*>();
    if(doc.containsKey(F("branch"))) _source.branch = doc[F("branch")].as<const char*>();
    if(doc.containsKey(F("dir"))) _source.dir = doc[F("dir")].as<const char*>();
    if(doc.containsKey(F("chunk"))) _source.chunk = doc[F("chunk")];
    if(doc.containsKey(F("insecure"))) _source.insecure = doc[F("insecure")];
    if(_source.chunk < 512 || _source.chunk > CHUNK_MAX) _source.chunk = CHUNK_DEFAULT;
}

bool save_source()
{
    StaticJsonDocument<512> doc;
    doc[F("owner")] = _source.owner;
    doc[F("repo")] = _source.repo;
    doc[F("branch")] = _source.branch;
    doc[F("dir")] = _source.dir;
    doc[F("chunk")] = _source.chunk;
    doc[F("insecure")] = _source.insecure;
    File file = LittleFS.open(FPSTR(CFG_FILE), "w");
    if(!file) return false;
    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}

const Source& get_source()
{
    return _source;
}

void set_source(const Source& src)
{
    _source = src;
    if(_source.owner.length() == 0) _source.owner = F(FW_UPDATE_OWNER);
    if(_source.repo.length() == 0) _source.repo = F(FW_UPDATE_REPO);
    if(_source.branch.length() == 0) _source.branch = F(FW_UPDATE_BRANCH);
    if(_source.chunk < 512 || _source.chunk > CHUNK_MAX) _source.chunk = CHUNK_DEFAULT;
    /* the anchors may have to be dropped when switching to/from insecure */
    if(_anchors)
    {
        delete _anchors;
        _anchors = nullptr;
    }
}

void begin()
{
    load_source();
}

void set_yield_callback(void (*cb)())
{
    _yield_cb = cb;
}

void set_prepare_callback(void (*cb)())
{
    _prepare_cb = cb;
}

void set_resume_callback(void (*cb)())
{
    _resume_cb = cb;
}

const Progress& get_progress()
{
    return _progress;
}

bool busy()
{
    return _running || _request != REQ_NONE;
}

void get_status_json(String& rtn)
{
    StaticJsonDocument<640> doc;
    doc[F("state")] = (uint8_t)_progress.state;
    doc[F("busy")] = busy();
    doc[F("total")] = _progress.total;
    doc[F("done")] = _progress.done;
    doc[F("message")] = _progress.message;
    doc[F("available")] = _progress.available;
    doc[F("size")] = _progress.size;
    doc[F("md5")] = _progress.md5;
    doc[F("file")] = _progress.file;
    doc[F("current")] = FW_VERSION;
    doc[F("space")] = ESP.getFreeSketchSpace();
    doc[F("heap")] = ESP.getFreeHeap();
    doc[F("reboot")] = _reboot_at != 0;
    if(serializeJson(doc, rtn) == 0) rtn = F("{\"error\":\"serialize\"}");
}

void get_source_json(String& rtn)
{
    StaticJsonDocument<512> doc;
    doc[F("owner")] = _source.owner;
    doc[F("repo")] = _source.repo;
    doc[F("branch")] = _source.branch;
    doc[F("dir")] = _source.dir;
    doc[F("chunk")] = _source.chunk;
    doc[F("insecure")] = _source.insecure;
    doc[F("host")] = FPSTR(GH_HOST);
    if(serializeJson(doc, rtn) == 0) rtn = F("{\"error\":\"serialize\"}");
}

/** /owner/repo/branch/dir/ */
static String base_path()
{
    String path;
    path.reserve(96);
    path = '/';
    path += _source.owner;
    path += '/';
    path += _source.repo;
    path += '/';
    path += _source.branch;
    path += '/';
    if(_source.dir.length())
    {
        path += _source.dir;
        path += '/';
    }
    return path;
}

/*******************************************************************************
 * TLS
 ******************************************************************************/
static BearSSL::X509List* load_anchors()
{
    File file = LittleFS.open(FPSTR(CA_FILE), "r");
    if(file)
    {
        String pem;
        pem.reserve(file.size() + 1);
        while(file.available()) pem += (char)file.read();
        file.close();
        BearSSL::X509List* list = new BearSSL::X509List(pem.c_str());
        if(list && list->getCount()) return list;
        BWC_LOG_P(PSTR("FW > %s unusable, using built in root\n"), String(FPSTR(CA_FILE)).c_str());
        if(list) delete list;
    }
    String pem = FPSTR(TRUST_ROOT);
    BearSSL::X509List* list = new BearSSL::X509List(pem.c_str());
    if(list && !list->getCount())
    {
        delete list;
        return nullptr;
    }
    return list;
}

static void tls_end()
{
    if(!_client) return;
    _client->stop();
    delete _client;
    _client = nullptr;
}

static bool tls_connect(String& err)
{
    if(_client && _client->connected()) return true;
    tls_end();

    /* BearSSL wants the receive buffer in one piece, and the session as a whole
       needs roughly 15 kB */
    if(ESP.getFreeHeap() < HEAP_NEEDED || ESP.getMaxFreeBlockSize() < TLS_RX_BUFFER + 1024)
    {
        err = F("not enough free memory for https (");
        err += ESP.getFreeHeap();
        err += F(" bytes free). Use the browser assisted update instead");
        return false;
    }

    _client = new BearSSL::WiFiClientSecure;
    if(!_client)
    {
        err = F("out of memory");
        return false;
    }
    _client->setBufferSizes(TLS_RX_BUFFER, TLS_TX_BUFFER);
    _client->setTimeout(NET_TIMEOUT);
    if(_source.insecure)
    {
        _client->setInsecure();
    }
    else
    {
        /* the certificate is only valid in a time window, so we need the clock */
        if(time(nullptr) < 1600000000)
        {
            err = F("clock not set (no NTP yet)");
            tls_end();
            return false;
        }
        if(!_anchors) _anchors = load_anchors();
        if(!_anchors)
        {
            err = F("no usable CA certificate");
            tls_end();
            return false;
        }
        _client->setTrustAnchors(_anchors);
    }
    if(!_session) _session = new BearSSL::Session;
    if(_session) _client->setSession(_session);

    ESP.wdtFeed();
    if(!_client->connect(FPSTR(GH_HOST), GH_PORT))
    {
        char msg[64] = {0};
        int sslerr = _client->getLastSSLError(msg, sizeof(msg));
        err = F("https connect failed: ");
        err += sslerr;
        err += ' ';
        err += msg;
        tls_end();
        return false;
    }
    BWC_LOG_P(PSTR("FW > connected to github. Heap: %d\n"), ESP.getFreeHeap());
    return true;
}

/*******************************************************************************
 * ranged http get
 ******************************************************************************/
static bool send_range_request(const String& path, uint32_t from, uint32_t to)
{
    String req;
    req.reserve(160 + path.length());
    req = F("GET ");
    req += path;
    req += F(" HTTP/1.1\r\nHost: ");
    req += FPSTR(GH_HOST);
    req += F("\r\nUser-Agent: BWC-ESP8266\r\nAccept: */*\r\nRange: bytes=");
    req += from;
    req += '-';
    req += to;
    req += F("\r\nConnection: keep-alive\r\n\r\n");
    return _client->print(req) == (int)req.length();
}

static bool read_headers(Response& rsp, String& err)
{
    rsp = {0, 0, 0, false};
    uint32_t deadline = millis() + NET_TIMEOUT;
    bool first = true;
    while((int32_t)(millis() - deadline) < 0)
    {
        if(!_client->available())
        {
            if(!_client->connected()) break;
            spin();
            continue;
        }
        String line = _client->readStringUntil('\n');
        line.trim();
        deadline = millis() + NET_TIMEOUT;
        if(first)
        {
            /* HTTP/1.1 206 Partial Content */
            int sp = line.indexOf(' ');
            if(sp < 0) break;
            rsp.status = line.substring(sp + 1, sp + 4).toInt();
            first = false;
            continue;
        }
        if(line.length() == 0) return rsp.status > 0;    /* end of headers */

        String name = line.substring(0, line.indexOf(':'));
        name.toLowerCase();
        String value = line.substring(line.indexOf(':') + 1);
        value.trim();
        if(name == F("content-length"))
        {
            rsp.length = value.toInt();
        }
        else if(name == F("content-range"))
        {
            /* bytes 0-4095/571360 */
            int slash = value.lastIndexOf('/');
            if(slash >= 0) rsp.total = value.substring(slash + 1).toInt();
        }
        else if(name == F("connection"))
        {
            value.toLowerCase();
            rsp.close = value.indexOf(F("close")) >= 0;
        }
    }
    err = F("timeout while reading headers");
    return false;
}

/**
 * Download path into sink, using range requests small enough to keep the TLS
 * records within our receive buffer. Resumes at the current offset when the
 * connection breaks.
 */
static bool fetch(const String& path, sink_fn sink, void* ctx, uint32_t max_size,
                  uint32_t* size_out, String& err)
{
    err.clear();
    uint8_t* buf = (uint8_t*)malloc(READ_BUFFER);
    if(!buf)
    {
        err = F("out of memory");
        return false;
    }

    uint32_t offset = 0;
    uint32_t total = 0;
    bool have_total = false;
    uint8_t retries = 0;
    bool ok = true;

    _progress.done = 0;
    _progress.total = 0;

    while(ok && (!have_total || offset < total))
    {
        if(!tls_connect(err))
        {
            if(++retries > MAX_RETRIES) ok = false;
            else delay(500);
            continue;
        }

        uint32_t to = offset + _source.chunk - 1;
        if(have_total && to > total - 1) to = total - 1;
        if(!send_range_request(path, offset, to))
        {
            tls_end();
            if(++retries > MAX_RETRIES)
            {
                err = F("could not send request");
                ok = false;
            }
            continue;
        }

        Response rsp;
        if(!read_headers(rsp, err))
        {
            tls_end();
            if(++retries > MAX_RETRIES) ok = false;
            continue;
        }

        if(rsp.status == 404)
        {
            err = F("not found on github: ");
            err += path;
            ok = false;
            break;
        }
        if(rsp.status == 200)
        {
            /* Range ignored. The whole body arrives now and would be sent in
               16 kB TLS records as soon as it gets big, so only accept it when
               it is small enough to be harmless. */
            if(rsp.length == 0 || rsp.length > _source.chunk)
            {
                err = F("server ignored the range request");
                ok = false;
                break;
            }
            total = rsp.length;
            have_total = true;
            _progress.total = total;
        }
        else if(rsp.status == 206)
        {
            if(rsp.length == 0)
            {
                err = F("empty reply from github");
                ok = false;
                break;
            }
            if(!have_total)
            {
                if(rsp.total == 0)
                {
                    err = F("no content-range in reply");
                    ok = false;
                    break;
                }
                total = rsp.total;
                have_total = true;
                _progress.total = total;
                if(max_size && total > max_size)
                {
                    err = F("file does not fit (");
                    err += total;
                    err += F(" bytes)");
                    ok = false;
                    break;
                }
            }
        }
        else
        {
            err = F("unexpected http status ");
            err += rsp.status;
            ok = false;
            break;
        }

        uint32_t remaining = rsp.length;
        uint32_t deadline = millis() + NET_TIMEOUT;
        while(remaining)
        {
            size_t want = remaining > READ_BUFFER ? READ_BUFFER : remaining;
            int len = _client->read(buf, want);
            if(len > 0)
            {
                if(!sink(buf, len, ctx))
                {
                    if(err.length() == 0) err = F("could not store data");
                    ok = false;
                    break;
                }
                remaining -= len;
                offset += len;
                _progress.done = offset;
                retries = 0;
                deadline = millis() + NET_TIMEOUT;
            }
            else
            {
                if(!_client->available() && !_client->connected()) break;
                if((int32_t)(millis() - deadline) >= 0) break;
            }
            spin();
        }
        if(!ok) break;

        if(remaining)
        {
            /* connection died or stalled - reconnect and resume at offset */
            BWC_LOG_P(PSTR("FW > stalled at %d, resuming\n"), offset);
            tls_end();
            if(++retries > MAX_RETRIES)
            {
                err = F("download stalled");
                ok = false;
                break;
            }
            delay(200 * retries);
            continue;
        }
        if(rsp.close) tls_end();
        spin();
    }

    free(buf);
    if(size_out) *size_out = total;
    if(ok && have_total && offset != total)
    {
        err = F("incomplete download");
        ok = false;
    }
    return ok;
}

/*******************************************************************************
 * sinks
 ******************************************************************************/
struct StringSink
{
    String* text;
    uint32_t limit;
};

static bool sink_string(const uint8_t* data, size_t len, void* ctx)
{
    StringSink* s = (StringSink*)ctx;
    if(s->text->length() + len > s->limit) return false;
    for(size_t i = 0; i < len; i++) *(s->text) += (char)data[i];
    return true;
}

static bool sink_update(const uint8_t* data, size_t len, void* ctx)
{
    (void)ctx;
    return Update.write((uint8_t*)data, len) == len;
}

static bool sink_file(const uint8_t* data, size_t len, void* ctx)
{
    File* file = (File*)ctx;
    return file->write(data, len) == len;
}

/*******************************************************************************
 * the update itself
 ******************************************************************************/
static bool read_manifest(String& err)
{
    String body;
    body.reserve(256);
    StringSink sink = {&body, 1024};
    _progress.state = STATE_CHECKING;
    _progress.message = F("reading manifest.json");
    if(!fetch(base_path() + F("manifest.json"), sink_string, &sink, 2048, nullptr, err)) return false;

    StaticJsonDocument<384> doc;
    DeserializationError error = deserializeJson(doc, body);
    if(error)
    {
        err = F("manifest.json is not valid json");
        return false;
    }
    _progress.available = doc[F("version")] | "";
    _progress.size = doc[F("size")] | 0;
    _progress.md5 = doc[F("md5")] | "";
    /* the publisher names the binary after the version, so a manifest that is
       still in the github cache can only point at the matching binary */
    _progress.file = doc[F("file")] | "";
    if(_progress.file.length() == 0) _progress.file = F("firmware.bin");
    if(_progress.file.indexOf('/') >= 0)
    {
        err = F("manifest.json has a bad file name");
        return false;
    }
    if(_progress.available.length() == 0 || _progress.size == 0)
    {
        err = F("manifest.json is missing version or size");
        return false;
    }
    BWC_LOG_P(PSTR("FW > available: %s, %d bytes\n"), _progress.available.c_str(), _progress.size);
    return true;
}

static bool update_files(String& err)
{
    String list;
    list.reserve(512);
    StringSink sink = {&list, 4096};
    _progress.state = STATE_FILES;
    _progress.message = F("reading filelist.txt");
    if(!fetch(base_path() + F("filelist.txt"), sink_string, &sink, 8192, nullptr, err)) return false;

    uint16_t count = 0;
    for(uint16_t i = 0; i < list.length(); i++) if(list[i] == '\n') count++;
    uint16_t done = 0;
    int from = 0;
    while(from < (int)list.length())
    {
        int nl = list.indexOf('\n', from);
        String name = (nl < 0) ? list.substring(from) : list.substring(from, nl);
        from = (nl < 0) ? list.length() : nl + 1;
        name.trim();
        if(name.length() == 0 || name.startsWith("#")) continue;
        if(name.indexOf('/') >= 0)
        {
            BWC_LOG_P(PSTR("FW > skipping %s\n"), name.c_str());
            continue;
        }

        _progress.message = String(done + 1) + '/' + String(count) + ' ' + name;
        LittleFS.remove(FPSTR(TMP_FILE));
        File file = LittleFS.open(FPSTR(TMP_FILE), "w");
        if(!file)
        {
            err = F("could not open a temp file");
            return false;
        }
        bool ok = fetch(base_path() + F("data/") + name, sink_file, &file, 262144, nullptr, err);
        file.close();
        if(!ok)
        {
            LittleFS.remove(FPSTR(TMP_FILE));
            return false;
        }
        String target = "/" + name;
        /* the file server prefers a .gz, so a stale plain file has to go */
        if(name.endsWith(".gz")) LittleFS.remove(target.substring(0, target.length() - 3));
        else LittleFS.remove(target + ".gz");
        LittleFS.remove(target);
        if(!LittleFS.rename(FPSTR(TMP_FILE), target))
        {
            err = F("could not rename ");
            err += target;
            return false;
        }
        BWC_LOG_P(PSTR("FW > stored %s\n"), target.c_str());
        done++;
        spin();
    }
    _progress.message = F("web files updated");
    return true;
}

static bool update_firmware(String& err)
{
    uint32_t space = ESP.getFreeSketchSpace();
    if(_progress.size > space)
    {
        err = F("firmware does not fit (");
        err += _progress.size;
        err += F(" > ");
        err += space;
        err += ')';
        return false;
    }
    _progress.state = STATE_FIRMWARE;
    _progress.message = F("writing firmware");
    if(!Update.begin(_progress.size, U_FLASH))
    {
        err = Update.getErrorString();
        Update.end();
        return false;
    }
    if(_progress.md5.length() == 32) Update.setMD5(_progress.md5.c_str());

    if(!fetch(base_path() + _progress.file, sink_update, nullptr, space, nullptr, err))
    {
        if(Update.getError() != UPDATE_ERROR_OK)
        {
            err += F(" (");
            err += Update.getErrorString();
            err += ')';
        }
        Update.end();
        Update.clearError();
        return false;
    }
    if(!Update.end(true))
    {
        err = Update.getErrorString();
        Update.clearError();
        return false;
    }
    _progress.message = F("firmware written, rebooting");
    return true;
}

static void run(Request request, bool with_files)
{
    String err;
    bool ok = true;
    bool flashed = false;
    _progress.done = 0;
    _progress.total = 0;
    _progress.message.clear();

    if(WiFi.status() != WL_CONNECTED)
    {
        _progress.state = STATE_ERROR;
        _progress.message = F("no wifi connection");
        return;
    }

    if(_prepare_cb) _prepare_cb();
    BWC_LOG_P(PSTR("FW > start. Heap: %d, max block: %d\n"), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());

    ok = read_manifest(err);
    if(ok && request == REQ_UPDATE)
    {
        if(_progress.available.equals(FW_VERSION) && !with_files)
        {
            _progress.message = F("already up to date");
        }
        else
        {
            if(with_files) ok = update_files(err);
            if(ok && !_progress.available.equals(FW_VERSION))
            {
                ok = update_firmware(err);
                flashed = ok;
            }
            else if(ok) _progress.message = F("firmware already up to date");
        }
    }
    tls_end();

    if(!ok)
    {
        _progress.state = STATE_ERROR;
        _progress.message = err;
        BWC_LOG_P(PSTR("FW > failed: %s\n"), err.c_str());
        if(_resume_cb) _resume_cb();
        return;
    }

    _progress.state = STATE_DONE;
    if(_progress.message.length() == 0) _progress.message = F("done");
    BWC_LOG_P(PSTR("FW > %s\n"), _progress.message.c_str());
    if(flashed)
    {
        /* give the web ui a few seconds to show the result */
        _reboot_at = millis() + 4000;
    }
    else if(_resume_cb)
    {
        _resume_cb();
    }
}

void request_check()
{
    if(busy()) return;
    _request = REQ_CHECK;
}

void request_update(bool with_files)
{
    if(busy()) return;
    _with_files = with_files;
    _request = REQ_UPDATE;
}

void loop()
{
    if(_reboot_at && (int32_t)(millis() - _reboot_at) >= 0)
    {
        BWC_LOG_P(PSTR("FW > rebooting into new firmware\n"), 0);
        delay(100);
        ESP.restart();
        delay(3000);
    }
    if(_request == REQ_NONE || _running) return;

    Request request = _request;
    _running = true;
    run(request, _with_files);
    _running = false;
    _request = REQ_NONE;
}

}   // namespace fwupdate
