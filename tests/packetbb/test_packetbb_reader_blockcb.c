/*
 * test_reader_blockcb.c
 *
 *  Created on: 18.07.2010
 *      Author: henning
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "common/common_types.h"
#include "packetbb/pbb_reader.h"
#include "../cunit.h"

/*
 * consumer definition 1
 * TLV type 1 (mandatory)
 * TLV type 2 (copy data into variable value)
 */
static struct pbb_reader_tlvblock_consumer_entry consumer_entries[] = {
  { .type = 1 },
  { .type = 2, .mandatory = true }
};

/* packetbb test messages */
static uint8_t testpacket1[] = {
/* packet with tlvblock, but without sequence number */
    0x04,
/* tlvblock, tlv type 1 */
    0, 2, 1, 0
};
static uint8_t testpacket12[] = {
/* packet with tlvblock, but without sequence number */
    0x04,
/* tlvblock, tlv type 1, tlv type 2 */
    0, 4, 1, 0, 2, 0,
};
static uint8_t testpacket121[] = {
/* packet with tlvblock, but without sequence number */
    0x04,
/* tlvblock, tlv type 1, tlv type 2, tlv type 1 */
    0, 6, 1, 0, 2, 0, 1, 0
};

static uint8_t testpacket212[] = {
/* packet with tlvblock, but without sequence number */
    0x04,
/* tlvblock, tlv type 2, tlv type 1, tlv type 2 */
    0, 6, 2, 0, 1, 0, 2, 0
};

static struct pbb_reader reader;
struct pbb_reader_tlvblock_consumer consumer;
static bool got_tlv[2];
static bool got_multiple_times[2];
static bool got_failed_constraints;

static enum pbb_result
cb_blocktlv_packet(struct pbb_reader_tlvblock_consumer *cons __attribute__ ((unused)),
      struct pbb_reader_tlvblock_context *cont __attribute__ ((unused)),
      bool mandatory_missing) {
  got_tlv[0] = consumer_entries[0].tlv != NULL;
  got_multiple_times[0] = consumer_entries[0].duplicate_tlv;

  got_tlv[1] = consumer_entries[1].tlv != NULL;
  got_multiple_times[1] = consumer_entries[1].duplicate_tlv;

  got_failed_constraints = mandatory_missing;
  return PBB_OKAY;
}

static enum pbb_result
cb_blocktlv_packet_okay(struct pbb_reader_tlvblock_consumer *cons,
      struct pbb_reader_tlvblock_context *cont) {
  return cb_blocktlv_packet(cons, cont, false);
}

static enum pbb_result
cb_blocktlv_packet_failed(struct pbb_reader_tlvblock_consumer *cons,
      struct pbb_reader_tlvblock_context *cont) {
  return cb_blocktlv_packet(cons, cont, true);
}

static void clear_elements(void) {
  got_tlv[0] = false;
  got_multiple_times[0] = false;
  got_tlv[1] = false;
  got_multiple_times[1] = false;
  got_failed_constraints = false;
}

static void test_packet1(void) {
  START_TEST();

  pbb_reader_handle_packet(&reader, testpacket1, sizeof(testpacket1));

  CHECK_TRUE(got_tlv[0], "TLV 1");
  CHECK_TRUE(!got_tlv[1], "TLV 2");

  CHECK_TRUE(!got_multiple_times[0], "TLV 1 (duplicate)");
  CHECK_TRUE(!got_multiple_times[1], "TLV 2 (duplicate)");

  CHECK_TRUE(got_failed_constraints, "mandatory missing");
  END_TEST();
}

static void test_packet12(void) {
  START_TEST();

  pbb_reader_handle_packet(&reader, testpacket12, sizeof(testpacket12));

  CHECK_TRUE(got_tlv[0], "TLV 1");
  CHECK_TRUE(got_tlv[1], "TLV 2");

  CHECK_TRUE(!got_multiple_times[0], "TLV 1 (duplicate)");
  CHECK_TRUE(!got_multiple_times[1], "TLV 2 (duplicate)");

  CHECK_TRUE(!got_failed_constraints, "mandatory missing");
  END_TEST();
}

static void test_packet121(void) {
  START_TEST();

  pbb_reader_handle_packet(&reader, testpacket121, sizeof(testpacket121));

  CHECK_TRUE(got_tlv[0], "TLV 1");
  CHECK_TRUE(got_tlv[1], "TLV 2");

  CHECK_TRUE(got_multiple_times[0], "TLV 1 (duplicate)");
  CHECK_TRUE(!got_multiple_times[1], "TLV 2 (duplicate)");

  CHECK_TRUE(!got_failed_constraints, "mandatory missing");
  END_TEST();
}

static void test_packet212(void) {
  START_TEST();

  pbb_reader_handle_packet(&reader, testpacket212, sizeof(testpacket212));

  CHECK_TRUE(got_tlv[0], "TLV 1");
  CHECK_TRUE(got_tlv[1], "TLV 2");

  CHECK_TRUE(!got_multiple_times[0], "TLV 1 (duplicate)");
  CHECK_TRUE(got_multiple_times[1], "TLV 2 (duplicate)");

  CHECK_TRUE(!got_failed_constraints, "mandatory missing");
  END_TEST();
}

int main(int argc __attribute__ ((unused)), char **argv __attribute__ ((unused))) {
  pbb_reader_init(&reader);
  pbb_reader_add_packet_consumer(&reader, &consumer, consumer_entries, ARRAYSIZE(consumer_entries), 1);
  consumer.block_callback = cb_blocktlv_packet_okay;
  consumer.block_callback_failed_constraints = cb_blocktlv_packet_failed;

  BEGIN_TESTING();

  test_packet1();
  test_packet12();
  test_packet121();
  test_packet212();

  FINISH_TESTING();

  pbb_reader_cleanup(&reader);
  return total_fail;
}
