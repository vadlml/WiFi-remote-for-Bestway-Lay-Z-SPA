#pragma once
/*
 * Firmware and web file update straight from GitHub.
 *
 * Why this is not a plain ESP8266httpUpdate call:
 * GitHub (Fastly) no longer answers the TLS "maximum fragment length"
 * negotiation, so it may send 16 kB TLS records. BearSSL has to buffer a whole
 * record, and a 16 kB receive buffer does not fit in the heap that is left on
 * this firmware - that is why the old implementation stopped working.
 * A response body that stays below ~24 kB is however sent in MTU sized records
 * (~1.4 kB), so every file is fetched with HTTP range requests over a single
 * keep alive connection. A 5 kB receive buffer (needed for the certificate
 * record during the handshake) is then enough.
 */
#include <Arduino.h>

namespace fwupdate
{
    enum State : uint8_t
    {
        STATE_IDLE = 0,
        STATE_CHECKING,
        STATE_FILES,
        STATE_FIRMWARE,
        STATE_DONE,
        STATE_ERROR
    };

    /** where to get the update from (raw.githubusercontent.com/owner/repo/branch/dir) */
    struct Source
    {
        String owner;
        String repo;
        String branch;
        String dir;
        uint16_t chunk;
        bool insecure;
    };

    struct Progress
    {
        State state;
        uint32_t total;
        uint32_t done;
        String message;
        /** version, size and md5 of the firmware.bin on GitHub (from manifest.json) */
        String available;
        String md5;
        uint32_t size;
    };

    void begin();
    /** run the pending request. Call from loop() */
    void loop();
    bool busy();

    const Source& get_source();
    void set_source(const Source& src);
    void load_source();
    bool save_source();

    const Progress& get_progress();
    void get_status_json(String& rtn);
    void get_source_json(String& rtn);

    /** only read manifest.json and report the available version */
    void request_check();
    /** download data files (optional) and firmware, then reboot */
    void request_update(bool with_files);

    /** called often while an update runs. Keep the webserver/watchdog alive here */
    void set_yield_callback(void (*cb)());
    /** called before going online. Free as much heap as possible here */
    void set_prepare_callback(void (*cb)());
    /** called when we are done and normal operation can resume (no reboot) */
    void set_resume_callback(void (*cb)());
}
