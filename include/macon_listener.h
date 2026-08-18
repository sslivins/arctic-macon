#pragma once

// ---------------------------------------------------------------------------
// MaconListener — portable passive-listen frame decoder (Layer 3 core).
//
// This is the host-testable heart of "passive listen" mode: it consumes a raw
// RS485 byte stream, drains complete Tuya frames (tolerating our own echo /
// unrelated frames / junk / the trailing block-tag), ingests each decoded
// response window straight into the consumer's opaque MaconImage, and catalogs
// every observed response window for diagnostics.
//
// It owns NONE of the platform: no FreeRTOS task, no UART driver, no timer, no
// logging. The consumer's thin shim owns those and simply pumps bytes in via
// feed_bytes(). Because ingest happens INSIDE the library, no register address
// ever crosses back to the consumer — feed_bytes() reports only semantic
// MaconCoverage, so the controller carries zero Tuya/Macon wire knowledge.
//
// Keeping the frame-draining/resync state machine here means it is exercised by
// native unit tests with synthetic byte streams instead of only on hardware.
//
// Threading: NOT internally synchronised. The consumer serialises feed_bytes()
// against any other access to the same MaconImage with its own mutex.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>

#include "macon_image.h"   // MaconImage, MaconCoverage
#include "tuya_codec.h"    // framing codec

namespace arctic {

// ---------------------------------------------------------------------------
// Listener statistics (diagnostics / heartbeat logging)
// ---------------------------------------------------------------------------
struct MaconListenerStats {
    uint32_t bytes_rx      = 0;  // total bytes fed
    uint32_t frames_ok     = 0;  // complete frames decoded (req + resp, incl. unknown)
    uint32_t req_frames    = 0;  // controller->unit request frames
    uint32_t resp_frames   = 0;  // unit->controller response frames
    uint32_t checksum_err  = 0;  // framing looked valid but checksum failed
    uint32_t resync        = 0;  // junk/garbage skips (resyncs)
    uint32_t last_frame_ms = 0;  // timestamp of most recent decoded frame
};

// ---------------------------------------------------------------------------
// Observed response-window diagnostic catalog entry.
//
// A distinct (field_a, field_b) response window seen on the bus, with the FULL
// payload (including any window-prefix bytes the register ingest strips). Used
// to hunt for register blocks the codec does not yet map (e.g. compressor
// frequency). This is raw wire metadata and lives ONLY in the library.
// ---------------------------------------------------------------------------
struct MaconObservedWindow {
    uint16_t field_a     = 0;  // wire addr field
    uint16_t field_b     = 0;  // wire count field (payload byte length)
    uint32_t hits        = 0;  // times this window has been seen
    uint32_t last_ms     = 0;  // timestamp of most recent sighting
    uint8_t  known       = 0;  // 1 if the codec maps it to a register base
    uint8_t  payload_len = 0;  // bytes captured (<= sizeof(payload))
    uint8_t  payload[64] = {}; // most-recent full payload (incl. prefix)
};

// ---------------------------------------------------------------------------
// MaconObservedCatalog — de-duplicating store of observed response windows.
//
// Shared by both the passive listener and the active-master image sink so the
// consumer has a single diagnostic view regardless of mode. Fixed capacity, no
// allocation; NOT internally synchronised (the consumer serialises access).
// ---------------------------------------------------------------------------
class MaconObservedCatalog {
public:
    static constexpr size_t kMax = 64;

    // Record one observed response window (merging repeats by (a,b), bumping
    // hits and refreshing the payload/timestamp). Silently drops new windows
    // once at capacity.
    void record(uint16_t field_a, uint16_t field_b, uint8_t known,
                const uint8_t *payload, size_t len, uint32_t now_ms);

    size_t count() const { return count_; }
    const MaconObservedWindow *at(size_t i) const {
        return (i < count_) ? &entries_[i] : nullptr;
    }
    void clear() { count_ = 0; }

private:
    MaconObservedWindow entries_[kMax];
    size_t              count_ = 0;
};

// ---------------------------------------------------------------------------
// Result of a feed_bytes() call — semantic only, no register numbers.
// ---------------------------------------------------------------------------
struct MaconFeedResult {
    bool          any_response = false;              // >=1 response window ingested
    MaconCoverage coverage     = {false, false};     // OR of windows touched
    uint32_t      last_frame_ms = 0;                 // most recent decoded frame ts
};

// ---------------------------------------------------------------------------
// MaconListener
// ---------------------------------------------------------------------------
class MaconListener {
public:
    /// `image` and `catalog` must outlive the listener. The listener ingests
    /// decoded response windows straight into the image and records every
    /// observed response window into the catalog; the consumer must serialise
    /// feed_bytes() with any other access to either.
    MaconListener(MaconImage &image, MaconObservedCatalog &catalog);

    /// Feed `len` bytes read from the wire at time `now_ms`. Drains as many
    /// complete frames as possible; a trailing partial frame is retained for
    /// the next call. Response windows are ingested into the image; the return
    /// value reports whether anything decode-relevant arrived so the consumer
    /// can update freshness/connection state and re-decode the image.
    MaconFeedResult feed_bytes(const uint8_t *data, size_t len, uint32_t now_ms);

    MaconListenerStats stats() const { return stats_; }

    /// Drop any buffered partial frame (safety valve when the stream is junk).
    void reset_buffer();

private:
    // Drain complete frames from the front of acc_; returns bytes consumed and
    // accumulates coverage into the passed result.
    size_t drain(uint32_t now_ms, MaconFeedResult &result);

    static constexpr size_t kAccSize = 512;  // decode accumulator

    MaconImage           &image_;
    MaconObservedCatalog &catalog_;
    MaconListenerStats    stats_;
    uint8_t               acc_[kAccSize];
    size_t                acc_len_ = 0;
};

}  // namespace arctic
