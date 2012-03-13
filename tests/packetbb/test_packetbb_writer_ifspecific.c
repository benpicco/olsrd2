/*
 * PacketBB handler library (see RFC 5444)
 * Copyright (c) 2010 Henning Rogge <hrogge@googlemail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org/git for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "packetbb/pbb_context.h"
#include "packetbb/pbb_writer.h"
#include "../cunit.h"

static struct pbb_writer writer;
static struct pbb_writer_interface interf[2];

static int unique_messages;

static void addMessageHeader(struct pbb_writer *wr, struct pbb_writer_message *msg) {
  pbb_writer_set_msg_header(wr, msg, false, false, false, false);
  printf("Begin message\n");
  unique_messages++;
}

static void finishMessageHeader(struct pbb_writer *wr  __attribute__ ((unused)),
    struct pbb_writer_message *msg __attribute__ ((unused)),
    struct pbb_writer_address *first_addr __attribute__ ((unused)),
    struct pbb_writer_address *last_addr __attribute__ ((unused)),
    bool not_fragmented __attribute__ ((unused))) {
  printf("End message\n");
}


static void write_packet(struct pbb_writer *wr __attribute__ ((unused)),
    struct pbb_writer_interface *iface,
    void *buffer, size_t length) {
  size_t i, j;
  uint8_t *buf = buffer;

  if (iface == &interf[0]) {
    printf("Interface 1:\n");
  }
  else {
    printf("Interface 2:\n");
  }

  for (j=0; j<length; j+=32) {
    printf("%04zx:", j);

    for (i=j; i<length && i < j+31; i++) {
      printf("%s%02x", ((i&3) == 0) ? " " : "", (int)(buf[i]));
    }
    printf("\n");
  }
  printf("\n");
}

static void clear_elements(void) {
  unique_messages = 0;
}

static void test_ip_specific(void) {
  START_TEST();

  CHECK_TRUE(0 == pbb_writer_create_message_allif(&writer, 1), "Parser should return 0");
  pbb_writer_flush(&writer, &interf[0], false);
  pbb_writer_flush(&writer, &interf[1], false);

  CHECK_TRUE(unique_messages == 2, "bad number of messages: %d\n", unique_messages);

  END_TEST();
}

static void test_not_ip_specific(void) {
  START_TEST();

  CHECK_TRUE(0 == pbb_writer_create_message_allif(&writer, 2), "Parser should return 0");
  pbb_writer_flush(&writer, &interf[0], false);
  pbb_writer_flush(&writer, &interf[1], false);

  CHECK_TRUE(unique_messages == 1, "bad number of messages: %d\n", unique_messages);

  END_TEST();
}


int main(int argc __attribute__ ((unused)), char **argv __attribute__ ((unused))) {
  struct pbb_writer_message *msg[2];

  if (pbb_writer_init(&writer, 128, 1000))
    return -1;

  pbb_writer_register_interface(&writer, &interf[0], 128);
  interf[0].sendPacket = write_packet;

  pbb_writer_register_interface(&writer, &interf[1], 256);
  interf[1].sendPacket = write_packet;

  msg[0] = pbb_writer_register_message(&writer, 1, true, 4);
  msg[0]->addMessageHeader = addMessageHeader;
  msg[0]->finishMessageHeader = finishMessageHeader;

  msg[1] = pbb_writer_register_message(&writer, 2, false, 4);
  msg[1]->addMessageHeader = addMessageHeader;
  msg[1]->finishMessageHeader = finishMessageHeader;

  BEGIN_TESTING();

  test_ip_specific();
  test_not_ip_specific();

  FINISH_TESTING();

  pbb_writer_cleanup(&writer);

  return total_fail;
}
