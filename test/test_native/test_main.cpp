#include <unity.h>
#include <string.h>

#include "crc8.h"
#include "packet.h"

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
    return UNITY_END();
}
