// ---------------------------------------------------------------------------
// MaconListener implementation — see macon_listener.h.
//
// The frame-draining/resync state machine here is a straight relocation of the
// controller's old passive listener drain loop, minus all platform code. It is
// pure C++ (no ESP-IDF / FreeRTOS) so it is covered by native unit tests.
// ---------------------------------------------------------------------------
#include "macon_listener.h"

#include <cstring>

namespace arctic {

MaconListener::MaconListener(MaconImage &image, MaconObservedCatalog &catalog)
    : image_(image), catalog_(catalog) {}

void MaconListener::reset_buffer() { acc_len_ = 0; }

void MaconObservedCatalog::record(uint16_t a, uint16_t b, uint8_t known,
                                  const uint8_t *payload, size_t len,
                                  uint32_t now_ms) {
    const uint8_t cap = (uint8_t)sizeof(entries_[0].payload);
    const uint8_t n = (uint8_t)((len < cap) ? len : cap);

    MaconObservedWindow *slot = nullptr;
    for (size_t i = 0; i < count_; ++i) {
        if (entries_[i].field_a == a && entries_[i].field_b == b) {
            slot = &entries_[i];
            break;
        }
    }
    if (slot == nullptr && count_ < kMax) {
        slot = &entries_[count_++];
        slot->field_a = a;
        slot->field_b = b;
        slot->hits    = 0;
    }
    if (slot != nullptr) {
        slot->known       = known;
        slot->hits       += 1;
        slot->last_ms     = now_ms;
        slot->payload_len = n;
        for (uint8_t i = 0; i < n && payload != nullptr; ++i) {
            slot->payload[i] = payload[i];
        }
    }
}

// Consumes as many complete frames as possible from the front of acc_.
// Returns the number of bytes consumed; the caller shifts the remainder.
size_t MaconListener::drain(uint32_t now_ms, MaconFeedResult &result) {
    const uint8_t *acc = acc_;
    const size_t   acc_len = acc_len_;
    size_t consumed = 0;

    while (acc_len - consumed >= tuya_codec::HDR_LEN) {
        const uint8_t *p   = acc + consumed;
        size_t         rem = acc_len - consumed;

        // Locate the next plausible frame start (validated header).
        size_t start = tuya_codec::find_frame_start(p, rem);
        if (start == rem) {
            // No frame start in the buffer. Drop everything except a possible
            // partial magic at the tail (keep last HDR_LEN-1 bytes).
            size_t keep = (rem < tuya_codec::HDR_LEN - 1) ? rem
                                                          : tuya_codec::HDR_LEN - 1;
            consumed = acc_len - keep;
            break;
        }
        if (start > 0) {
            // Junk before the frame start -> resync.
            stats_.resync++;
            consumed += start;
            continue;
        }

        tuya_codec::ParsedFrame pf;
        tuya_codec::ParseResult r =
            tuya_codec::parse_frame(acc + consumed, acc_len - consumed, pf);

        if (r == tuya_codec::ParseResult::OK) {
            stats_.frames_ok++;
            if (pf.dir == tuya_codec::DIR_REQUEST) stats_.req_frames++;
            else                                   stats_.resp_frames++;
            stats_.last_frame_ms = now_ms;
            result.last_frame_ms = now_ms;

            // Ingest response payloads (heat pump -> controller) into the image.
            if (pf.dir == tuya_codec::DIR_RESPONSE && pf.window &&
                pf.payload_len > pf.window->prefix_len) {
                const MaconCoverage cov = image_.ingest_bytes(
                    pf.window->reg_base,
                    pf.payload + pf.window->prefix_len,
                    pf.payload_len - pf.window->prefix_len);
                result.any_response = true;
                result.coverage.status_updated    |= cov.status_updated;
                result.coverage.telemetry_updated |= cov.telemetry_updated;
            }
            // Catalog the FULL payload (incl. any window prefix bytes that the
            // ingest strips) for diagnostics.
            if (pf.dir == tuya_codec::DIR_RESPONSE) {
                catalog_.record(pf.field_a, pf.field_b, 1,
                                pf.payload, pf.payload_len, now_ms);
            }
            consumed += pf.frame_len;
        } else if (r == tuya_codec::ParseResult::UNKNOWN_WINDOW) {
            // Valid framing + checksum but a window the codec doesn't map. This
            // is exactly how a not-yet-decoded register block (e.g. compressor
            // frequency) shows up. Catalog it and skip the whole frame.
            stats_.frames_ok++;
            stats_.resp_frames++;
            stats_.last_frame_ms = now_ms;
            result.last_frame_ms = now_ms;
            if (pf.dir == tuya_codec::DIR_RESPONSE) {
                catalog_.record(pf.field_a, pf.field_b, 0,
                                pf.payload, pf.payload_len, now_ms);
            }
            consumed += pf.frame_len;
        } else if (r == tuya_codec::ParseResult::TRUNCATED) {
            // Wait for more bytes before this frame can be parsed.
            break;
        } else {
            // Header looked valid but checksum (or length) failed. Skip past
            // the magic and resync.
            if (r == tuya_codec::ParseResult::BAD_CHECKSUM) {
                stats_.checksum_err++;
            }
            consumed += 2;
        }
    }
    return consumed;
}

MaconFeedResult MaconListener::feed_bytes(const uint8_t *data, size_t len,
                                          uint32_t now_ms) {
    MaconFeedResult result;
    if (data == nullptr || len == 0) return result;

    // Append into the accumulator, draining/compacting to make room as needed.
    size_t off = 0;
    while (off < len) {
        size_t space = kAccSize - acc_len_;
        if (space == 0) {
            // Accumulator full: try to drain, else reset (garbage stream).
            size_t consumed = drain(now_ms, result);
            if (consumed > 0 && consumed <= acc_len_) {
                std::memmove(acc_, acc_ + consumed, acc_len_ - consumed);
                acc_len_ -= consumed;
            } else {
                // Safety valve: nothing drained from a full buffer -> reset.
                acc_len_ = 0;
                stats_.resync++;
            }
            space = kAccSize - acc_len_;
        }
        size_t take = len - off;
        if (take > space) take = space;
        std::memcpy(acc_ + acc_len_, data + off, take);
        acc_len_ += take;
        off += take;
        stats_.bytes_rx += (uint32_t)take;

        size_t consumed = drain(now_ms, result);
        if (consumed > 0 && consumed <= acc_len_) {
            std::memmove(acc_, acc_ + consumed, acc_len_ - consumed);
            acc_len_ -= consumed;
        }
    }
    return result;
}

}  // namespace arctic
