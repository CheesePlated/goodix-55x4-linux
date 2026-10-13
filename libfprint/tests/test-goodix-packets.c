// Test the actual receive buffer parser without opening a USB device.
#include "../libfprint/drivers/goodixtls/goodix.c"
GType fpi_device_goodixtls55x4_get_type(void);

static guint replies;
static void received_reply(FpDevice *dev, guint8 *data, guint16 length,
                            gpointer user_data, GError *error)
{
  g_assert_no_error(error);
  g_assert_cmpmem(data, length, "test", 4);
  replies++;
}

static guint8 *pack(guint8 cmd, guint8 *payload, guint16 length,
                    gboolean padded, guint32 *size)
{
  g_autofree guint8 *protocol = NULL;
  guint32 protocol_size;
  guint8 *result;
  goodix_encode_protocol(cmd, payload, length, TRUE, FALSE, &protocol, &protocol_size);
  goodix_encode_pack(GOODIX_FLAGS_MSG_PROTOCOL, protocol, protocol_size,
                    padded, &result, size);
  return result;
}

static void check_packets(gboolean padded, gboolean fragmented)
{
  g_autoptr(FpDevice) dev = g_object_new(fpi_device_goodixtls55x4_get_type(), NULL);
  FpiDeviceGoodixTlsPrivate *priv =
      fpi_device_goodixtls_get_instance_private(FPI_DEVICE_GOODIXTLS(dev));
  priv->cmd = GOODIX_CMD_FIRMWARE_VERSION;
  priv->ack = priv->reply = TRUE;
  priv->callback = received_reply;
  replies = 0;
  guint8 ack[] = {GOODIX_CMD_FIRMWARE_VERSION, 1};
  guint8 firmware[] = {'t', 'e', 's', 't'};
  guint32 ack_size, reply_size;
  g_autofree guint8 *ack_pack = pack(GOODIX_CMD_ACK, ack, sizeof ack, padded, &ack_size);
  g_autofree guint8 *reply_pack = pack(GOODIX_CMD_FIRMWARE_VERSION, firmware,
                                      sizeof firmware, padded, &reply_size);
  g_autofree guint8 *combined = g_malloc(ack_size + reply_size);
  memcpy(combined, ack_pack, ack_size);
  memcpy(combined + ack_size, reply_pack, reply_size);
  if (fragmented) {
    goodix_receive_pack(dev, combined, ack_size + 2);
    g_assert_cmpuint(replies, ==, 0);
    g_assert_true(priv->reply);
    goodix_receive_pack(dev, combined + ack_size + 2, reply_size - 2);
  } else {
    goodix_receive_pack(dev, combined, ack_size + reply_size);
  }
  g_assert_cmpuint(replies, ==, 1);
  g_assert_false(priv->reply);
  g_assert_cmpuint(priv->length, ==, 0);
  g_assert_null(priv->data);
}

static void coalesced(void) { check_packets(FALSE, FALSE); }
static void partial_reply(void) { check_packets(FALSE, TRUE); }
static void padded(void) { check_packets(TRUE, FALSE); }
static void padded_partial(void) { check_packets(TRUE, TRUE); }

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/goodix-packets/coalesced", coalesced);
  g_test_add_func("/goodix-packets/partial", partial_reply);
  g_test_add_func("/goodix-packets/padded", padded);
  g_test_add_func("/goodix-packets/padded-partial", padded_partial);
  return g_test_run();
}
