#include "macon_link.h"
#include "macon_registers.h"

#include <cstring>

namespace arctic {

using tuya_codec::ParsedFrame;
using tuya_codec::ParseResult;

const char *macon_result_name(MaconResult r) {
    switch (r) {
        case MaconResult::Ok:          return "Ok";
        case MaconResult::WriteFailed: return "WriteFailed";
        case MaconResult::NoResponse:  return "NoResponse";
        case MaconResult::BadResponse: return "BadResponse";
        case MaconResult::Nack:        return "Nack";
        default:                       return "Unknown";
    }
}

MaconLink::MaconLink(MaconTransport &transport, int response_timeout_ms)
    : tx_(transport), timeout_ms_(response_timeout_ms) {}

// --- public setpoint API ---------------------------------------------------

MaconResult MaconLink::set_cooling_setpoint(int celsius) {
    // Cooling setpoint lives at wire addr 0x0000 (reg2093).
    return write_setpoint(0x0000, celsius);
}

MaconResult MaconLink::set_hot_water_setpoint(int celsius) {
    // Hot-water setpoint lives at wire addr 0x0002 (reg2095).
    return write_setpoint(0x0002, celsius);
}

MaconResult MaconLink::read_cooling_setpoint(int *out_celsius) {
    return read_setpoint(REG_COOLING_SETPOINT - tuya_codec::KNOWN_WINDOWS[0].reg_base, out_celsius);
}

MaconResult MaconLink::read_hot_water_setpoint(int *out_celsius) {
    return read_setpoint(REG_HOT_WATER_SETPOINT - tuya_codec::KNOWN_WINDOWS[0].reg_base, out_celsius);
}

MaconResult MaconLink::read_heating_setpoint(int *out_celsius) {
    // UNVERIFIED register (reg2094). See header / docs/REGISTERS.md.
    return read_setpoint(REG_AUX_HEAT_SETPOINT - tuya_codec::KNOWN_WINDOWS[0].reg_base, out_celsius);
}

// --- private transaction helpers -------------------------------------------

MaconResult MaconLink::write_setpoint(uint16_t wire_addr, int celsius) {
    if (celsius < -128) celsius = -128;
    if (celsius >  127) celsius =  127;
    const uint8_t data = static_cast<uint8_t>(static_cast<int8_t>(celsius));

    uint8_t req[16];
    const size_t n = tuya_codec::encode_command(req, sizeof(req), wire_addr, 1, &data);
    if (n == 0) {
        return MaconResult::WriteFailed;
    }
    if (tx_.write(req, n) < static_cast<int>(n)) {
        return MaconResult::WriteFailed;
    }

    // Expect the mainboard's 9-byte ACK: dir=0x0F fc=0x06 echoing addr + count.
    uint8_t store[tuya_codec::MAX_FRAME_LEN];
    ParsedFrame pf;
    return read_matching_frame(pf, store, sizeof(store),
                               tuya_codec::DIR_RESPONSE, tuya_codec::FC_CMD,
                               wire_addr, 1);
}

MaconResult MaconLink::read_setpoint(uint16_t byte_offset, int *out_celsius) {
    if (out_celsius == nullptr) {
        return MaconResult::BadResponse;
    }

    // Read the whole telemetry window (wire addr 0, count 50). Every setpoint is
    // one byte at offset == its wire address inside this block.
    const tuya_codec::RegWindow &win = tuya_codec::KNOWN_WINDOWS[0];  // {0,50,2093,0}
    uint8_t req[16];
    const size_t n = tuya_codec::encode_request(req, sizeof(req),
                                                tuya_codec::FC_READ,
                                                win.field_a, win.field_b);
    if (n == 0) {
        return MaconResult::WriteFailed;
    }
    if (tx_.write(req, n) < static_cast<int>(n)) {
        return MaconResult::WriteFailed;
    }

    uint8_t store[tuya_codec::MAX_FRAME_LEN];
    ParsedFrame pf;
    const MaconResult r = read_matching_frame(pf, store, sizeof(store),
                                              tuya_codec::DIR_RESPONSE,
                                              tuya_codec::FC_READ,
                                              win.field_a, win.field_b);
    if (r != MaconResult::Ok) {
        return r;
    }
    if (pf.payload == nullptr || pf.payload_len <= byte_offset) {
        return MaconResult::BadResponse;
    }
    *out_celsius = static_cast<int>(static_cast<int8_t>(pf.payload[byte_offset]));
    return MaconResult::Ok;
}

MaconResult MaconLink::read_matching_frame(ParsedFrame &out,
                                           uint8_t *store, size_t cap,
                                           uint8_t want_dir, uint8_t want_fc,
                                           uint16_t want_a, uint16_t want_b) {
    size_t len = 0;
    // Bound the number of transport reads so a silent bus can't hang forever;
    // the transport's own per-call timeout paces each read.
    const int MAX_READS = 16;

    for (int reads = 0; reads <= MAX_READS; ) {
        // Drain everything currently buffered before asking for more bytes.
        while (len >= tuya_codec::HDR_LEN) {
            // Speculative NACK: an fc-exception frame (fc | 0x80) at the head,
            // which find_frame_start would otherwise discard as junk. Never
            // observed on this unit; shape unconfirmed.
            if (store[0] == tuya_codec::HDR0 && store[1] == tuya_codec::HDR1 &&
                store[2] == want_dir && (store[3] & 0x80)) {
                return MaconResult::Nack;
            }

            const size_t start = tuya_codec::find_frame_start(store, len);
            if (start == len) {
                // No plausible frame start anywhere -> drop everything, but keep
                // the last byte in case it is a partial 0x55 header.
                store[0] = store[len - 1];
                len = (store[0] == tuya_codec::HDR0) ? 1 : 0;
                break;
            }
            if (start > 0) {
                std::memmove(store, store + start, len - start);
                len -= start;
                continue;
            }

            // Candidate frame at the buffer head.
            const ParseResult pr = tuya_codec::parse_frame(store, len, out);
            if (pr == ParseResult::TRUNCATED) {
                break;  // need more bytes
            }
            if (pr == ParseResult::OK) {
                if (out.dir == want_dir && out.fc == want_fc &&
                    out.field_a == want_a && out.field_b == want_b) {
                    return MaconResult::Ok;
                }
                // A valid but unrelated frame (e.g. a telemetry response the
                // controller polled). Skip past it and keep scanning.
                std::memmove(store, store + out.frame_len, len - out.frame_len);
                len -= out.frame_len;
                continue;
            }
            // Bad frame at head (checksum/etc.) -> drop one byte and resync.
            std::memmove(store, store + 1, len - 1);
            len -= 1;
        }

        if (reads >= MAX_READS) {
            break;
        }
        if (len >= cap) {
            return MaconResult::BadResponse;  // overflow guard
        }
        const int got = tx_.read(store + len, cap - len, timeout_ms_);
        ++reads;
        if (got < 0) {
            return MaconResult::NoResponse;   // transport error
        }
        if (got == 0) {
            return MaconResult::NoResponse;   // timeout, no more data coming
        }
        len += static_cast<size_t>(got);
    }

    return MaconResult::NoResponse;
}

}  // namespace arctic
