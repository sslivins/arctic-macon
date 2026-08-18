// Native unit test for MaconListener (portable passive-listen frame decoder).
// Framework-free: prints failures, returns non-zero.
//
// Exercises the drain/resync state machine over synthetic Tuya byte streams,
// verifying that response windows are ingested into a MaconImage, that the
// observed-window catalog records both known and unknown windows, that junk /
// echo / trailing block-tags resync cleanly, and that frames split across
// feed_bytes() calls (streaming) are reassembled.

#include "macon_listener.h"
#include "macon_image.h"
#include "macon_state.h"
#include "tuya_codec.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace arctic;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

static size_t build_response(uint8_t *buf, size_t cap, uint16_t field_a,
                             uint16_t field_b, const uint8_t *payload) {
    return tuya_codec::encode_response(buf, cap, tuya_codec::FC_READ,
                                       field_a, field_b, payload);
}

int main() {
    // ----------------------------------------------------------------------
    // 1) A clean telemetry response is ingested; coverage + observed catalog
    //    reflect it, and the image decodes the setpoint byte.
    // ----------------------------------------------------------------------
    {
        MaconImage img;
        MaconObservedCatalog cat;
        MaconListener lis(img, cat);

        uint8_t payload[50];
        std::memset(payload, 0, sizeof(payload));
        payload[0] = 24;  // reg2093 cooling setpoint (whole degC)
        uint8_t resp[80];
        size_t rn = build_response(resp, sizeof(resp), 0, 50, payload);

        MaconFeedResult r = lis.feed_bytes(resp, rn, 1000);
        CHECK(r.any_response == true);
        CHECK(r.coverage.telemetry_updated == true);
        CHECK(r.last_frame_ms == 1000);

        CHECK(lis.stats().frames_ok == 1);
        CHECK(lis.stats().resp_frames == 1);
        CHECK(lis.stats().bytes_rx == rn);

        CHECK(cat.count() == 1);
        const MaconObservedWindow *w = cat.at(0);
        CHECK(w != nullptr);
        CHECK(w->field_a == 0);
        CHECK(w->field_b == 50);
        CHECK(w->known == 1);
        CHECK(w->hits == 1);

        MaconState ms;
        img.decode(&ms);
        CHECK(ms.cooling_setpoint_valid == true);
        CHECK(ms.cooling_setpoint == 24);
    }

    // ----------------------------------------------------------------------
    // 2) Junk prefix + trailing block-tag byte resync cleanly around a frame.
    // ----------------------------------------------------------------------
    {
        MaconImage img;
        MaconObservedCatalog cat;
        MaconListener lis(img, cat);

        uint8_t payload[50];
        std::memset(payload, 0, sizeof(payload));
        payload[0] = 12;
        uint8_t resp[80];
        size_t rn = build_response(resp, sizeof(resp), 0, 50, payload);

        std::vector<uint8_t> stream;
        stream.push_back(0xAB);            // junk
        stream.push_back(0x55);            // partial magic (not followed by AA 0F..)
        stream.insert(stream.end(), resp, resp + rn);
        stream.push_back(0x14);            // trailing telemetry block-tag

        MaconFeedResult r = lis.feed_bytes(stream.data(), stream.size(), 2000);
        CHECK(r.any_response == true);
        CHECK(lis.stats().resp_frames == 1);
        CHECK(lis.stats().resync >= 1);    // junk before the frame caused a resync

        MaconState ms;
        img.decode(&ms);
        CHECK(ms.cooling_setpoint == 12);
    }

    // ----------------------------------------------------------------------
    // 3) A frame split across two feed_bytes() calls is reassembled.
    // ----------------------------------------------------------------------
    {
        MaconImage img;
        MaconObservedCatalog cat;
        MaconListener lis(img, cat);

        uint8_t payload[50];
        std::memset(payload, 0, sizeof(payload));
        payload[0] = 18;
        uint8_t resp[80];
        size_t rn = build_response(resp, sizeof(resp), 0, 50, payload);

        size_t split = rn / 2;
        MaconFeedResult r1 = lis.feed_bytes(resp, split, 3000);
        CHECK(r1.any_response == false);   // incomplete: nothing decoded yet
        MaconFeedResult r2 = lis.feed_bytes(resp + split, rn - split, 3001);
        CHECK(r2.any_response == true);
        CHECK(lis.stats().resp_frames == 1);

        MaconState ms;
        img.decode(&ms);
        CHECK(ms.cooling_setpoint == 18);
    }

    // ----------------------------------------------------------------------
    // 4) A request frame is counted but ingests nothing (no payload).
    // ----------------------------------------------------------------------
    {
        MaconImage img;
        MaconObservedCatalog cat;
        MaconListener lis(img, cat);

        uint8_t req[16];
        size_t qn = tuya_codec::encode_request(req, sizeof(req),
                                               tuya_codec::FC_READ, 0, 50);
        MaconFeedResult r = lis.feed_bytes(req, qn, 4000);
        CHECK(r.any_response == false);
        CHECK(lis.stats().req_frames == 1);
        CHECK(lis.stats().resp_frames == 0);
        CHECK(cat.count() == 0);  // requests are not cataloged
    }

    // ----------------------------------------------------------------------
    // 5) Repeated sightings of the same window increment hits, not the count.
    // ----------------------------------------------------------------------
    {
        MaconImage img;
        MaconObservedCatalog cat;
        MaconListener lis(img, cat);

        uint8_t payload[50] = {};
        uint8_t resp[80];
        size_t rn = build_response(resp, sizeof(resp), 0, 50, payload);

        lis.feed_bytes(resp, rn, 5000);
        lis.feed_bytes(resp, rn, 5100);
        lis.feed_bytes(resp, rn, 5200);

        CHECK(cat.count() == 1);
        const MaconObservedWindow *w = cat.at(0);
        CHECK(w != nullptr);
        CHECK(w->hits == 3);
        CHECK(w->last_ms == 5200);
    }

    if (g_failures == 0) {
        std::printf("all macon-listener tests passed\n");
        return 0;
    }
    std::printf("%d macon-listener test(s) failed\n", g_failures);
    return 1;
}
