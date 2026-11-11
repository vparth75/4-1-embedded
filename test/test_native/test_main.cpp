#include <unity.h>
#include <string.h>

#include "crc8.h"
#include "packet.h"
#include "framer.h"
#include "link_engine.h"
#include "host_io.h"
#include <ArduinoJson.h>
#include <string>

void setUp() {}
void tearDown() {}

// ---------- helpers ----------

static PacketParser::Result feedAll(PacketParser& p, const uint8_t* b, size_t n, uint32_t t0 = 0)
{
    PacketParser::Result r = PacketParser::NONE;
    for (size_t i = 0; i < n; i++) {
        r = p.feed(b[i], t0 + (uint32_t)i);
    }
    return r;
}

// ---------- Task 1: packet layer ----------

void test_crc8_known_vector()
{
    const uint8_t s[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX8(0xF4, crc8(s, 9));
}

void test_build_packet_layout()
{
    uint8_t out[MAX_PACKET];
    const uint8_t p[] = {'h', 'i'};
    TEST_ASSERT_EQUAL(5, buildPacket(p, 2, out));
    TEST_ASSERT_EQUAL_HEX8(0x7E, out[0]);
    TEST_ASSERT_EQUAL(2, out[1]);
    TEST_ASSERT_EQUAL_HEX8(crc8(out + 1, 3), out[4]);
}

void test_build_packet_rejects_65()
{
    uint8_t in[65] = {0};
    uint8_t out[80];
    TEST_ASSERT_EQUAL(0, buildPacket(in, 65, out));
}

void test_parser_roundtrip_ok()
{
    uint8_t pkt[MAX_PACKET];
    size_t n = buildPacket((const uint8_t*)"hello", 5, pkt);
    PacketParser p;
    TEST_ASSERT_EQUAL(PacketParser::DONE, feedAll(p, pkt, n));
    TEST_ASSERT_TRUE(p.ok());
    TEST_ASSERT_EQUAL(5, p.length());
    TEST_ASSERT_EQUAL_MEMORY("hello", p.payload(), 5);
}

void test_parser_bad_crc()
{
    uint8_t pkt[MAX_PACKET];
    size_t n = buildPacket((const uint8_t*)"hello", 5, pkt);
    pkt[3] ^= 0x04;
    PacketParser p;
    TEST_ASSERT_EQUAL(PacketParser::DONE, feedAll(p, pkt, n));
    TEST_ASSERT_FALSE(p.ok());
    TEST_ASSERT_EQUAL(5, p.length());
}

void test_parser_skips_noise_before_sync()
{
    const uint8_t noise[] = {0x00, 0xFF, 0x13};
    uint8_t pkt[MAX_PACKET];
    size_t n = buildPacket((const uint8_t*)"hey", 3, pkt);
    PacketParser p;
    TEST_ASSERT_EQUAL(PacketParser::NONE, feedAll(p, noise, 3));
    TEST_ASSERT_EQUAL(PacketParser::DONE, feedAll(p, pkt, n, 10));
    TEST_ASSERT_TRUE(p.ok());
    TEST_ASSERT_EQUAL_MEMORY("hey", p.payload(), 3);
}

void test_parser_timeout_resets()
{
    const uint8_t partial[] = {SYNC_BYTE, 5, 'h', 'e'};
    PacketParser p;
    TEST_ASSERT_EQUAL(PacketParser::NONE, feedAll(p, partial, 4, 0));
    p.tick(100);
    uint8_t pkt[MAX_PACKET];
    size_t n = buildPacket((const uint8_t*)"hello", 5, pkt);
    TEST_ASSERT_EQUAL(PacketParser::DONE, feedAll(p, pkt, n, 200));
    TEST_ASSERT_TRUE(p.ok());
    TEST_ASSERT_EQUAL_MEMORY("hello", p.payload(), 5);
}

void test_parser_timeout_detected_on_feed()
{
    // No tick() call: a late byte must still count as a fresh start.
    const uint8_t partial[] = {SYNC_BYTE, 5, 'h', 'e'};
    PacketParser p;
    feedAll(p, partial, 4, 0);
    uint8_t pkt[MAX_PACKET];
    size_t n = buildPacket((const uint8_t*)"hello", 5, pkt);
    TEST_ASSERT_EQUAL(PacketParser::DONE, feedAll(p, pkt, n, 500));
    TEST_ASSERT_TRUE(p.ok());
}

void test_parser_len_over_64_resets()
{
    const uint8_t bad[] = {SYNC_BYTE, 0x41};
    PacketParser p;
    TEST_ASSERT_EQUAL(PacketParser::NONE, feedAll(p, bad, 2));
    uint8_t pkt[MAX_PACKET];
    size_t n = buildPacket((const uint8_t*)"ok", 2, pkt);
    TEST_ASSERT_EQUAL(PacketParser::DONE, feedAll(p, pkt, n, 5));
    TEST_ASSERT_TRUE(p.ok());
}

void test_parser_empty_payload()
{
    uint8_t pkt[MAX_PACKET];
    size_t n = buildPacket(nullptr, 0, pkt);
    TEST_ASSERT_EQUAL(3, n);
    PacketParser p;
    TEST_ASSERT_EQUAL(PacketParser::DONE, feedAll(p, pkt, n));
    TEST_ASSERT_TRUE(p.ok());
    TEST_ASSERT_EQUAL(0, p.length());
}


// ---------- Task 2: framing + link engine ----------

static const uint16_t ADC_LIGHT = 300;   // laser ON -> low reading
static const uint16_t ADC_DARK = 3900;   // laser OFF -> high reading

static uint16_t adcFor(bool level) { return level ? ADC_LIGHT : ADC_DARK; }

// Runs TX straight into RX for n ticks, collecting decoded bytes.
static int pipe(TxFramer& tx, RxDeframer& rx, int n, RxByte* out, int maxOut)
{
    int count = 0;
    RxByte idle;
    for (int i = 0; i < 10; i++) rx.tick(false, ADC_DARK, idle);   // line starts dark
    for (int i = 0; i < n; i++) {
        bool lvl = tx.tick();
        RxByte b;
        if (rx.tick(lvl, adcFor(lvl), b) && count < maxOut) {
            out[count++] = b;
        }
    }
    return count;
}

void test_tx_frame_shape()
{
    TxFramer tx;
    const uint8_t b = 0xA5;
    TEST_ASSERT_TRUE(tx.load(&b, 1));
    const int expect[10] = {1, 1, 0, 1, 0, 0, 1, 0, 1, 0};
    for (int bit = 0; bit < 10; bit++) {
        for (int t = 0; t < TICKS_PER_BIT; t++) {
            TEST_ASSERT_EQUAL_MESSAGE(expect[bit], tx.tick() ? 1 : 0, "bit level");
        }
    }
    TEST_ASSERT_FALSE(tx.tick());
    TEST_ASSERT_FALSE(tx.busy());
}

void test_tx_idle_is_off()
{
    TxFramer tx;
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_FALSE(tx.tick());
    }
}

void test_tx_load_rejects_while_busy()
{
    TxFramer tx;
    const uint8_t b = 0x01;
    TEST_ASSERT_TRUE(tx.load(&b, 1));
    TEST_ASSERT_TRUE(tx.busy());
    TEST_ASSERT_FALSE(tx.load(&b, 1));
}

void test_rx_decodes_byte()
{
    TxFramer tx;
    RxDeframer rx;
    const uint8_t b = 0x48;
    tx.load(&b, 1);
    RxByte got[4];
    TEST_ASSERT_EQUAL(1, pipe(tx, rx, 200, got, 4));
    TEST_ASSERT_EQUAL_HEX8(0x48, got[0].value);
    TEST_ASSERT_TRUE(got[0].framingOk);
    TEST_ASSERT_EQUAL(ADC_LIGHT, got[0].adc[0]);
    TEST_ASSERT_EQUAL(ADC_DARK, got[0].adc[9]);
}

void test_rx_two_bytes_back_to_back()
{
    TxFramer tx;
    RxDeframer rx;
    const uint8_t b[] = {0x00, 0xFF};
    tx.load(b, 2);
    RxByte got[4];
    TEST_ASSERT_EQUAL(2, pipe(tx, rx, 300, got, 4));
    TEST_ASSERT_EQUAL_HEX8(0x00, got[0].value);
    TEST_ASSERT_EQUAL_HEX8(0xFF, got[1].value);
    TEST_ASSERT_TRUE(got[0].framingOk);
    TEST_ASSERT_TRUE(got[1].framingOk);
}

void test_rx_ignores_short_glitch()
{
    RxDeframer rx;
    RxByte b;
    int count = 0;
    for (int i = 0; i < 10; i++) count += rx.tick(false, ADC_DARK, b);
    for (int i = 0; i < 2; i++) count += rx.tick(true, ADC_LIGHT, b);
    for (int i = 0; i < 200; i++) count += rx.tick(false, ADC_DARK, b);
    TEST_ASSERT_EQUAL(0, count);
}

void test_rx_tolerates_clock_drift()
{
    // Sender is ~5% slower than the receiver expects: 8.4 RX ticks per bit.
    TxFramer tx;
    const uint8_t b[] = {'O', 'K', 0x55};
    tx.load(b, 3);
    bool levels[400];
    int nbits = 0;
    bool bits[64];
    // Record one level per bit from the TX framer.
    while (nbits < 40) {
        bool lvl = false;
        for (int t = 0; t < TICKS_PER_BIT; t++) lvl = tx.tick();
        bits[nbits++] = lvl;
    }
    for (int j = 0; j < 400; j++) {
        int idx = (int)((j - 10) / 8.4);   // 10 dark ticks first
        levels[j] = (j >= 10 && idx < nbits) ? bits[idx] : false;
    }
    RxDeframer rx;
    RxByte got[4];
    int count = 0;
    for (int j = 0; j < 400; j++) {
        RxByte r;
        if (rx.tick(levels[j], adcFor(levels[j]), r) && count < 4) got[count++] = r;
    }
    TEST_ASSERT_EQUAL(3, count);
    TEST_ASSERT_EQUAL_HEX8('O', got[0].value);
    TEST_ASSERT_EQUAL_HEX8('K', got[1].value);
    TEST_ASSERT_EQUAL_HEX8(0x55, got[2].value);
}

void test_constant_light_yields_no_flood()
{
    RxDeframer rx;
    RxByte b;
    int count = 0;
    bool anyOk = false;
    for (int i = 0; i < 20; i++) rx.tick(false, ADC_DARK, b);
    for (int i = 0; i < 2000; i++) {
        if (rx.tick(true, ADC_LIGHT, b)) {
            count++;
            anyOk |= b.framingOk;
        }
    }
    TEST_ASSERT_LESS_OR_EQUAL(1, count);
    TEST_ASSERT_FALSE(anyOk);
}

// Runs two engines; a's laser feeds b's sensor and the reverse.
static void runPair(LinkEngine& a, LinkEngine& b, int ticks)
{
    bool la = false, lb = false;
    for (int i = 0; i < ticks; i++) {
        uint32_t ms = (uint32_t)(i / TICKS_PER_BIT);
        bool na = a.tick(adcFor(lb), ms);
        bool nb = b.tick(adcFor(la), ms);
        la = na;
        lb = nb;
    }
}

void test_engine_loopback_packet()
{
    LinkEngine a, b;
    TEST_ASSERT_TRUE(a.sendPayload((const uint8_t*)"hello", 5));
    runPair(a, b, 8 * 10 * TICKS_PER_BIT + 50);
    RxPacket pkt;
    TEST_ASSERT_TRUE(b.takeReceived(pkt));
    TEST_ASSERT_TRUE(pkt.ok);
    TEST_ASSERT_EQUAL(5, pkt.len);
    TEST_ASSERT_EQUAL_MEMORY("hello", pkt.payload, 5);
    TEST_ASSERT_EQUAL(8 * 10, pkt.adcCount);
    TEST_ASSERT_FALSE(b.takeReceived(pkt));
    TEST_ASSERT_FALSE(a.takeReceived(pkt));
}

void test_engine_full_duplex()
{
    LinkEngine a, b;
    TEST_ASSERT_TRUE(a.sendPayload((const uint8_t*)"ping", 4));
    TEST_ASSERT_TRUE(b.sendPayload((const uint8_t*)"pong", 4));
    runPair(a, b, 7 * 10 * TICKS_PER_BIT + 50);
    RxPacket pa, pb;
    TEST_ASSERT_TRUE(a.takeReceived(pa));
    TEST_ASSERT_TRUE(b.takeReceived(pb));
    TEST_ASSERT_TRUE(pa.ok);
    TEST_ASSERT_TRUE(pb.ok);
    TEST_ASSERT_EQUAL_MEMORY("pong", pa.payload, 4);
    TEST_ASSERT_EQUAL_MEMORY("ping", pb.payload, 4);
}

void test_engine_threshold_inverted_logic()
{
    LinkEngine e(1000);
    TEST_ASSERT_EQUAL(1000, e.threshold());
    // Start bit needs idle first; feed dark (1100 => level 0) then light (900 => level 1).
    // A whole byte of 0xFF framed: start 1, eight 1s, stop 0 -> with level mapping must decode.
    RxPacket pkt;
    uint8_t wire[MAX_PACKET];
    size_t n = buildPacket((const uint8_t*)"A", 1, wire);
    TxFramer tx;
    tx.load(wire, n);
    uint32_t ms = 0;
    for (int i = 0; i < 20; i++) e.tick(1100, 0);
    for (int i = 0; i < (int)n * 10 * TICKS_PER_BIT + 20; i++) {
        bool lvl = tx.tick();
        e.tick(lvl ? 900 : 1100, ms + i / TICKS_PER_BIT);
    }
    TEST_ASSERT_TRUE(e.takeReceived(pkt));
    TEST_ASSERT_TRUE(pkt.ok);
    TEST_ASSERT_EQUAL_HEX8('A', pkt.payload[0]);
}

void test_engine_noise_before_packet_not_captured()
{
    // A stray byte before SYNC must not end up in the packet's adc capture.
    LinkEngine e;
    TxFramer tx;
    uint8_t wire[MAX_PACKET + 1];
    wire[0] = 0x11;
    size_t n = buildPacket((const uint8_t*)"hi", 2, wire + 1) + 1;
    tx.load(wire, n);
    for (int i = 0; i < 10; i++) e.tick(ADC_DARK, 0);
    for (int i = 0; i < (int)n * 10 * TICKS_PER_BIT + 20; i++) {
        bool lvl = tx.tick();
        e.tick(adcFor(lvl), i / TICKS_PER_BIT);
    }
    RxPacket pkt;
    TEST_ASSERT_TRUE(e.takeReceived(pkt));
    TEST_ASSERT_TRUE(pkt.ok);
    TEST_ASSERT_EQUAL(5 * 10, pkt.adcCount);
}


// ---------- Task 3: host JSON protocol ----------

static JsonDocument parseJson(const std::string& s)
{
    JsonDocument d;
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)deserializeJson(d, s).code(), s.c_str());
    return d;
}

void test_bits_string()
{
    TEST_ASSERT_EQUAL_STRING("01101000 01101001", bitsString((const uint8_t*)"hi", 2).c_str());
    TEST_ASSERT_EQUAL_STRING("", bitsString(nullptr, 0).c_str());
}

void test_hello_event_fields()
{
    JsonDocument d = parseJson(helloEvent(1500));
    TEST_ASSERT_EQUAL_STRING("hello", d["evt"]);
    TEST_ASSERT_EQUAL(1500, d["threshold"].as<int>());
    TEST_ASSERT_EQUAL(1000, d["bit_us"].as<int>());
    TEST_ASSERT_EQUAL_STRING("laser-chat 1.0", d["fw"]);
}

void test_handle_send_ok()
{
    LinkEngine eng;
    HostActions act = {};
    JsonDocument d = parseJson(handleCommand("{\"cmd\":\"send\",\"text\":\"hi\"}", eng, act));
    TEST_ASSERT_EQUAL_STRING("sent", d["evt"]);
    TEST_ASSERT_EQUAL_STRING("hi", d["text"]);
    TEST_ASSERT_EQUAL_STRING("01101000 01101001", d["bits"]);
    TEST_ASSERT_TRUE(eng.txBusy());
}

void test_handle_send_too_long()
{
    LinkEngine eng;
    HostActions act = {};
    std::string line = "{\"cmd\":\"send\",\"text\":\"" + std::string(65, 'a') + "\"}";
    JsonDocument d = parseJson(handleCommand(line, eng, act));
    TEST_ASSERT_EQUAL_STRING("error", d["evt"]);
    TEST_ASSERT_EQUAL_STRING("message too long", d["msg"]);
    TEST_ASSERT_FALSE(eng.txBusy());
}

void test_handle_send_64_ok()
{
    LinkEngine eng;
    HostActions act = {};
    std::string line = "{\"cmd\":\"send\",\"text\":\"" + std::string(64, 'a') + "\"}";
    JsonDocument d = parseJson(handleCommand(line, eng, act));
    TEST_ASSERT_EQUAL_STRING("sent", d["evt"]);
}

void test_handle_send_busy()
{
    LinkEngine eng;
    HostActions act = {};
    handleCommand("{\"cmd\":\"send\",\"text\":\"a\"}", eng, act);
    JsonDocument d = parseJson(handleCommand("{\"cmd\":\"send\",\"text\":\"b\"}", eng, act));
    TEST_ASSERT_EQUAL_STRING("error", d["evt"]);
    TEST_ASSERT_EQUAL_STRING("busy", d["msg"]);
}

void test_handle_send_empty_rejected()
{
    LinkEngine eng;
    HostActions act = {};
    JsonDocument d = parseJson(handleCommand("{\"cmd\":\"send\",\"text\":\"\"}", eng, act));
    TEST_ASSERT_EQUAL_STRING("error", d["evt"]);
    TEST_ASSERT_FALSE(eng.txBusy());
}

void test_handle_hello()
{
    LinkEngine eng(1234);
    HostActions act = {};
    JsonDocument d = parseJson(handleCommand("{\"cmd\":\"hello\"}", eng, act));
    TEST_ASSERT_EQUAL_STRING("hello", d["evt"]);
    TEST_ASSERT_EQUAL(1234, d["threshold"].as<int>());
    TEST_ASSERT_TRUE(act.sendHello);
}

void test_handle_set_threshold()
{
    LinkEngine eng;
    HostActions act = {};
    JsonDocument d = parseJson(handleCommand("{\"cmd\":\"set_threshold\",\"value\":1200}", eng, act));
    TEST_ASSERT_EQUAL(1200, eng.threshold());
    TEST_ASSERT_TRUE(act.thresholdChanged);
    TEST_ASSERT_EQUAL(1200, act.newThreshold);
    TEST_ASSERT_EQUAL_STRING("hello", d["evt"]);
    TEST_ASSERT_EQUAL(1200, d["threshold"].as<int>());
}

void test_handle_set_threshold_out_of_range()
{
    const char* bad[] = {
        "{\"cmd\":\"set_threshold\",\"value\":5000}",
        "{\"cmd\":\"set_threshold\",\"value\":-1}",
        "{\"cmd\":\"set_threshold\"}",
        "{\"cmd\":\"set_threshold\",\"value\":\"12\"}",
    };
    for (const char* line : bad) {
        LinkEngine eng;
        HostActions act = {};
        JsonDocument d = parseJson(handleCommand(line, eng, act));
        TEST_ASSERT_EQUAL_STRING("error", d["evt"]);
        TEST_ASSERT_EQUAL_STRING("bad threshold", d["msg"]);
        TEST_ASSERT_EQUAL(2048, eng.threshold());
        TEST_ASSERT_FALSE(act.thresholdChanged);
    }
}

void test_handle_garbage()
{
    LinkEngine eng;
    HostActions act = {};
    const char* bad[] = {"abc{", "{\"cmd\":\"fly\"}", "[1,2]", ""};
    for (const char* line : bad) {
        JsonDocument d = parseJson(handleCommand(line, eng, act));
        TEST_ASSERT_EQUAL_STRING("error", d["evt"]);
        TEST_ASSERT_EQUAL_STRING("bad command", d["msg"]);
    }
}

void test_recv_event_adc_array()
{
    RxPacket pkt = {};
    memcpy(pkt.payload, "hi", 2);
    pkt.len = 2;
    pkt.ok = false;
    pkt.adc[0] = 1; pkt.adc[1] = 2; pkt.adc[2] = 3;
    pkt.adcCount = 3;
    JsonDocument d = parseJson(recvEvent(pkt));
    TEST_ASSERT_EQUAL_STRING("recv", d["evt"]);
    TEST_ASSERT_FALSE(d["ok"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("hi", d["text"]);
    TEST_ASSERT_EQUAL_STRING("01101000 01101001", d["bits"]);
    JsonArray a = d["adc"];
    TEST_ASSERT_EQUAL(3, a.size());
    TEST_ASSERT_EQUAL(1, a[0].as<int>());
    TEST_ASSERT_EQUAL(3, a[2].as<int>());
}

void test_events_are_single_line()
{
    RxPacket pkt = {};
    memcpy(pkt.payload, "a\nb", 3);
    pkt.len = 3;
    pkt.ok = true;
    std::string s = recvEvent(pkt);
    TEST_ASSERT_EQUAL(std::string::npos, s.find('\n'));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc8_known_vector);
    RUN_TEST(test_build_packet_layout);
    RUN_TEST(test_build_packet_rejects_65);
    RUN_TEST(test_parser_roundtrip_ok);
    RUN_TEST(test_parser_bad_crc);
    RUN_TEST(test_parser_skips_noise_before_sync);
    RUN_TEST(test_parser_timeout_resets);
    RUN_TEST(test_parser_timeout_detected_on_feed);
    RUN_TEST(test_parser_len_over_64_resets);
    RUN_TEST(test_parser_empty_payload);
    RUN_TEST(test_tx_frame_shape);
    RUN_TEST(test_tx_idle_is_off);
    RUN_TEST(test_tx_load_rejects_while_busy);
    RUN_TEST(test_rx_decodes_byte);
    RUN_TEST(test_rx_two_bytes_back_to_back);
    RUN_TEST(test_rx_ignores_short_glitch);
    RUN_TEST(test_rx_tolerates_clock_drift);
    RUN_TEST(test_constant_light_yields_no_flood);
    RUN_TEST(test_engine_loopback_packet);
    RUN_TEST(test_engine_full_duplex);
    RUN_TEST(test_engine_threshold_inverted_logic);
    RUN_TEST(test_engine_noise_before_packet_not_captured);
    RUN_TEST(test_bits_string);
    RUN_TEST(test_hello_event_fields);
    RUN_TEST(test_handle_send_ok);
    RUN_TEST(test_handle_send_too_long);
    RUN_TEST(test_handle_send_64_ok);
    RUN_TEST(test_handle_send_busy);
    RUN_TEST(test_handle_send_empty_rejected);
    RUN_TEST(test_handle_hello);
    RUN_TEST(test_handle_set_threshold);
    RUN_TEST(test_handle_set_threshold_out_of_range);
    RUN_TEST(test_handle_garbage);
    RUN_TEST(test_recv_event_adc_array);
    RUN_TEST(test_events_are_single_line);
    return UNITY_END();
}
