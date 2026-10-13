// Exercise the driver callbacks without USB; only completion and I/O are mocked.
#define fpi_device_action_is_cancelled test_action_cancelled
#define fpi_image_device_deactivate_complete test_deactivate_complete
#define goodix_receive_done test_receive_done
#define goodix_send_mcu_switch_to_idle_mode test_send_idle
#include "../libfprint/drivers/goodixtls/goodix55x4.c"

static gboolean cancelled;
static gboolean scan_failure;
static unsigned int completed, idle_commands, aborted;

gboolean test_action_cancelled(FpDevice *dev) { return cancelled; }
void test_deactivate_complete(FpImageDevice *dev, GError *error)
{
  if (scan_failure) {
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
    g_clear_error(&error);
  } else {
    g_assert_no_error(error);
  }
  completed++;
}
void test_receive_done(FpDevice *dev, guint8 *data, guint16 length, GError *error)
{
  aborted++;
  fpi_ssm_mark_failed(FPI_DEVICE_GOODIXTLS55X4(dev)->scan_ssm, error);
}
void test_send_idle(FpDevice *dev, guint8 sleep_time,
                    GoodixNoneCallback callback, gpointer data)
{
  idle_commands++;
  callback(dev, data, NULL);
}
static void wait_for_scan(FpiSsm *ssm, FpDevice *dev) {}

static void run_deactivation(gboolean scan_pending, gboolean user_cancelled,
                              gboolean fail_scan)
{
  cancelled = user_cancelled;
  scan_failure = fail_scan;
  completed = idle_commands = aborted = 0;
  FpiDeviceGoodixTls55X4 *self = g_object_new(fpi_device_goodixtls55x4_get_type(), NULL);
  self->active = TRUE;
  if (scan_pending) {
    self->scan_ssm = fpi_ssm_new(FP_DEVICE(self), wait_for_scan, 1);
    fpi_ssm_start(self->scan_ssm, scan_complete);
  }
  dev_deactivate(FP_IMAGE_DEVICE(self));
  if (scan_pending && !user_cancelled) {
    // Last enrollment image processed while FDT_UP is still in progress.
    g_assert_cmpuint(completed, ==, 0);
    g_assert_cmpuint(idle_commands, ==, 0);
    g_assert_cmpuint(aborted, ==, 0);
    g_assert_true(self->deactivating);
    if (fail_scan)
      fpi_ssm_mark_failed(self->scan_ssm,
          g_error_new_literal(G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "FDT reply timed out"));
    else
      fpi_ssm_mark_completed(self->scan_ssm);
  }
  g_assert_cmpuint(completed, ==, 1);
  g_assert_cmpuint(idle_commands, ==, user_cancelled ? 1 : 0);
  g_assert_cmpuint(aborted, ==, user_cancelled && scan_pending ? 1 : 0);
  g_assert_null(self->scan_ssm);
  g_assert_false(self->deactivating);
  g_slist_free_full(self->frames, g_free);
  self->frames = NULL;
  g_object_unref(self);
}

static void final_enrollment_sample(void) { run_deactivation(TRUE, FALSE, FALSE); }
static void failed_final_scan(void) { run_deactivation(TRUE, FALSE, TRUE); }
static void finished_verification(void) { run_deactivation(FALSE, FALSE, FALSE); }
static void cancel_pending_scan(void) { run_deactivation(TRUE, TRUE, FALSE); }

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/goodix-deactivate/enroll-last-image", final_enrollment_sample);
  g_test_add_func("/goodix-deactivate/enroll-last-error", failed_final_scan);
  g_test_add_func("/goodix-deactivate/verify-finished", finished_verification);
  g_test_add_func("/goodix-deactivate/cancel", cancel_pending_scan);
  return g_test_run();
}
