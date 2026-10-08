/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Compile the actual transport with fake USB endpoints. No hardware access. */
#include "goodix.c"
#include <signal.h>
static int claims,releases,finished,success_packets,protocol_packets;
static GIOErrorEnum completion_code;
static FpiUsbTransfer *pending;
static GPtrArray *sources;
static gboolean fail_write, wrong_peer_key;
static GByteArray *tls_output, *usb_output;

typedef struct { FpiDeviceGoodixTls parent; } TestSensor;
typedef struct { FpiDeviceGoodixTlsClass parent; } TestSensorClass;
G_DEFINE_TYPE(TestSensor,test_sensor,FPI_TYPE_DEVICE_GOODIXTLS)
static void test_sensor_init(TestSensor *self) {}
static void test_sensor_class_init(TestSensorClass *class)
{
    FP_DEVICE_CLASS(class)->id="goodixtls53xd";
    FP_DEVICE_CLASS(class)->type=FP_DEVICE_TYPE_USB;
    FP_DEVICE_CLASS(class)->features=FP_DEVICE_FEATURE_VERIFY;
    FPI_DEVICE_GOODIXTLS_CLASS(class)->ep_in=0x81;
    FPI_DEVICE_GOODIXTLS_CLASS(class)->ep_out=0x01;
}
GUsbDevice *fpi_device_get_usb_device(FpDevice *dev) { return NULL; }
gboolean g_usb_device_claim_interface(GUsbDevice *usb,gint iface,GUsbDeviceClaimInterfaceFlags flags,GError **error)
{ claims++; return TRUE; }
gboolean g_usb_device_release_interface(GUsbDevice *usb,gint iface,GUsbDeviceClaimInterfaceFlags flags,GError **error)
{ g_assert_null(pending);releases++;return TRUE; }
GSource *fpi_device_add_timeout(FpDevice *dev,gint ms,FpTimeoutFunc func,gpointer data,GDestroyNotify destroy)
{
    GSource *source=g_timeout_source_new(ms);g_ptr_array_add(sources,source);return source;
}
FpiUsbTransfer *fpi_usb_transfer_new(FpDevice *dev)
{ FpiUsbTransfer *t=g_new0(FpiUsbTransfer,1);t->device=dev;return t; }
void fpi_usb_transfer_unref(FpiUsbTransfer *t)
{ if(t->free_buffer)t->free_buffer(t->buffer);g_free(t); }
void fpi_usb_transfer_fill_bulk_full(FpiUsbTransfer *t,guint8 ep,guint8 *buf,gsize len,GDestroyNotify free_func)
{ t->endpoint=ep;t->buffer=buf;t->length=len;t->free_buffer=free_func; }
void fpi_usb_transfer_fill_bulk(FpiUsbTransfer *t,guint8 ep,gsize len)
{ fpi_usb_transfer_fill_bulk_full(t,ep,g_malloc0(len),len,g_free); }
void fpi_usb_transfer_submit(FpiUsbTransfer *t,guint timeout,GCancellable *cancel,FpiUsbTransferCallback cb,gpointer data)
{ g_assert_null(pending);pending=t;t->callback=cb;t->user_data=data; }
gboolean fpi_usb_transfer_submit_sync(FpiUsbTransfer *t,guint timeout,GError **error)
{
    if(fail_write) {g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BROKEN_PIPE,"Synthetic USB write failure");return FALSE;}
    guint8 flags;guint16 len;gboolean valid;g_autofree guint8 *payload=NULL;
    g_byte_array_append(usb_output,t->buffer,t->length);
    if(goodix_decode_pack(usb_output->data,usb_output->len,&flags,&payload,&len,&valid)) {
        g_byte_array_set_size(usb_output,0);
        if(flags==GOODIX_FLAGS_MSG_PROTOCOL) {
            protocol_packets++;
            /* Restrict this test to handshake commands; no provisioning. */
            g_assert_true(payload[0]==GOODIX_CMD_REQUEST_TLS_CONNECTION || payload[0]==GOODIX_CMD_TLS_SUCCESSFULLY_ESTABLISHED || payload[0]==GOODIX_CMD_MCU_SWITCH_TO_IDLE_MODE);
            if(payload[0]==GOODIX_CMD_TLS_SUCCESSFULLY_ESTABLISHED)success_packets++;
        } else if(flags==GOODIX_FLAGS_TLS) g_byte_array_append(tls_output,payload,len);
    }
    return TRUE;
}
static void complete(FpDevice *dev,gpointer data,GError *error)
{ finished++;completion_code=error?error->code:0;g_clear_error(&error); }
static void read_cancel(FpDevice *dev)
{
    FpiUsbTransfer *t=pending;pending=NULL;g_assert_nonnull(t);
    t->callback(t,dev,t->user_data,g_error_new_literal(G_IO_ERROR,G_IO_ERROR_CANCELLED,"Synthetic cancellation"));
    fpi_usb_transfer_unref(t);
}
static FpDevice *new_device(void)
{
    finished=success_packets=protocol_packets=0;completion_code=0;fail_write=FALSE;
    sources=g_ptr_array_new_with_free_func((GDestroyNotify)g_source_unref);
    tls_output=g_byte_array_new();usb_output=g_byte_array_new();
    FpDevice *dev=g_object_new(test_sensor_get_type(),NULL);
    GError *error=NULL;g_assert_true(goodix_dev_init(dev,&error));g_assert_no_error(error);
    return dev;
}
static void free_device(FpDevice *dev)
{
    goodix_dev_deinit_async(dev,complete,NULL);
    if(pending)read_cancel(dev);
    g_object_unref(dev);g_ptr_array_unref(sources);g_byte_array_unref(tls_output);g_byte_array_unref(usb_output);
}
static void start_tls(FpDevice *dev)
{ guint8 key[32];for(int i=0;i<32;i++)key[i]=i+1;goodix_set_existing_psk(dev,key);goodix_tls(dev,complete,NULL); }
static void test_timeout_and_cancel(void)
{
    for(int mode=0;mode<3;mode++) {
        FpDevice *dev=new_device();start_tls(dev);
        g_assert_cmpint(finished,==,0);g_assert_cmpint(protocol_packets,==,1);
        if(mode==0)tls_timeout(dev,NULL);
        else if(mode==1)g_assert_true(goodix_cancel_command(dev));
        else goodix_receive_done(dev,NULL,0,g_error_new_literal(G_IO_ERROR,G_IO_ERROR_FAILED,"Request rejected"));
        g_assert_cmpint(finished,==,1);g_assert_cmpint(success_packets,==,0);
        g_assert_cmpint(completion_code,==,mode==0?G_IO_ERROR_TIMED_OUT:mode==1?G_IO_ERROR_CANCELLED:G_IO_ERROR_FAILED);
        g_assert_false(goodix_cancel_command(dev));
        free_device(dev);
    }
}
static void test_failed_write(void)
{
    FpDevice *dev=new_device();fail_write=TRUE;start_tls(dev);
    g_assert_cmpint(finished,==,1);g_assert_cmpint(completion_code,==,G_IO_ERROR_BROKEN_PIPE);
    g_assert_cmpint(success_packets,==,0);free_device(dev);
}
static void test_cleanup_retry(void)
{
    FpDevice *dev=new_device();int initial_releases=releases;
    for(int round=0;round<8;round++) {
        goodix_start_read_loop(dev);g_assert_nonnull(pending);
        start_tls(dev);goodix_cancel_command(dev);
        goodix_dev_deinit_async(dev,complete,NULL);
        g_assert_cmpint(releases,==,initial_releases+round);
        GError *error=NULL;
        g_assert_false(goodix_dev_init(dev,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_PENDING);g_clear_error(&error);
        read_cancel(dev);
        g_assert_cmpint(releases,==,initial_releases+round+1);
        g_assert_true(goodix_dev_init(dev,&error));g_assert_no_error(error);
    }
    free_device(dev);
}
static unsigned int peer_psk(SSL *ssl,const char *hint,char *identity,unsigned int cap,unsigned char *key,unsigned int size)
{ strcpy(identity,"Client");for(int i=0;i<32;i++)key[i]=i+1;if(wrong_peer_key)key[0]^=0x80;return 32; }
static void test_success_ack(void)
{
    for(int bad_ack=0;bad_ack<3;bad_ack++) {
        wrong_peer_key=bad_ack==2;
        FpDevice *dev=new_device();start_tls(dev);
        SSL_CTX *ctx=SSL_CTX_new(TLS_client_method());SSL_CTX_set_min_proto_version(ctx,TLS1_2_VERSION);SSL_CTX_set_max_proto_version(ctx,TLS1_2_VERSION);
        SSL_CTX_set_cipher_list(ctx,"PSK-AES128-CBC-SHA256");SSL_CTX_set_psk_client_callback(ctx,peer_psk);
        SSL *peer=SSL_new(ctx);SSL_CTX_free(ctx);
        BIO *in=BIO_new(BIO_s_mem()),*out=BIO_new(BIO_s_mem());BIO_set_mem_eof_return(in,-1);BIO_set_mem_eof_return(out,-1);
        SSL_set_bio(peer,in,out);SSL_set_connect_state(peer);
        for(int step=0;step<30 && !success_packets && !finished;step++) {
            int rc=SSL_do_handshake(peer);if(rc!=1)g_assert_cmpint(SSL_get_error(peer,rc),==,SSL_ERROR_WANT_READ);
            guint8 buf[4096];
            if(BIO_ctrl_pending(out)) {int size=BIO_read(out,buf,sizeof(buf));goodix_receive_done(dev,buf,size,NULL);}
            if(tls_output->len){BIO_write(in,tls_output->data,tls_output->len);g_byte_array_set_size(tls_output,0);}
        }
        if(wrong_peer_key) {
            g_assert_cmpint(success_packets,==,0);g_assert_cmpint(finished,==,1);
            g_assert_cmpint(completion_code,==,G_IO_ERROR_FAILED);
            SSL_free(peer);free_device(dev);continue;
        }
        g_assert_cmpint(success_packets,==,1);g_assert_cmpint(finished,==,0);
        goodix_receive_done(dev,NULL,0,bad_ack?g_error_new_literal(G_IO_ERROR,G_IO_ERROR_TIMED_OUT,"Synthetic ACK timeout"):NULL);
        g_assert_cmpint(finished,==,1);g_assert_cmpint(completion_code,==,bad_ack?G_IO_ERROR_TIMED_OUT:0);
        SSL_free(peer);free_device(dev);
    }
}
static void raw_complete(FpDevice *dev,guint8 *data,guint16 len,gpointer user_data,GError *error)
{ complete(dev,user_data,error); }
static void test_truncated_and_blocked(void)
{
    FpDevice *dev=new_device();
    goodix_read_tls(dev,raw_complete,NULL);
    guint8 one=0,*pack=NULL;guint32 len;
    goodix_encode_pack(GOODIX_FLAGS_TLS_DATA,&one,1,TRUE,&pack,&len);
    goodix_receive_pack(dev,pack,len);g_free(pack);
    g_assert_cmpint(finished,==,1);g_assert_cmpint(completion_code,==,G_IO_ERROR_INVALID_DATA);
    g_setenv("GOODIX_538D_TRANSPORT_ONLY","1",TRUE);
    goodix_send_protocol(dev,GOODIX_CMD_RESET,&one,1,NULL,TRUE,1000,TRUE,raw_complete,NULL);
    g_assert_cmpint(finished,==,2);g_assert_cmpint(completion_code,==,G_IO_ERROR_NOT_SUPPORTED);
    g_assert_cmpint(protocol_packets,==,0);
    g_unsetenv("GOODIX_538D_TRANSPORT_ONLY");
    free_device(dev);
}
static void test_cancelled_image_does_not_ack_idle(void)
{
    FpDevice *dev=new_device();
    goodix_send_mcu_switch_to_idle_mode(dev,20,complete,NULL);
    guint8 cancelled_image[10]={0},*pack=NULL;guint32 len;
    goodix_encode_pack(GOODIX_FLAGS_TLS_DATA,cancelled_image,sizeof(cancelled_image),TRUE,&pack,&len);
    goodix_receive_pack(dev,pack,len);g_free(pack);
    g_assert_cmpint(finished,==,0);
    goodix_receive_done(dev,NULL,0,NULL);
    g_assert_cmpint(finished,==,1);
    free_device(dev);
}
int main(int argc,char **argv)
{
    signal(SIGPIPE,SIG_DFL);g_test_init(&argc,&argv,NULL);
    g_test_add_func("/transport/timeout-cancel-request-failure",test_timeout_and_cancel);
    g_test_add_func("/transport/write-failure",test_failed_write);
    g_test_add_func("/transport/cleanup-retry",test_cleanup_retry);
    g_test_add_func("/transport/authenticated-success-ack",test_success_ack);
    g_test_add_func("/transport/truncated-and-blocked",test_truncated_and_blocked);
    g_test_add_func("/transport/cancelled-image-before-idle-ack",test_cancelled_image_does_not_ack_idle);
    return g_test_run();
}
