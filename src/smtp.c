

#include "sockets.h"
#include "dns.h"
#include "netdb.h"
#include "sms_forward_config.h"
#include "luat_debug.h"
#include "luat_network_adapter.h"

#include "mbedtls/platform.h"
#include "mbedtls/base64.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509.h"
#include "psa\crypto_types.h"
#include "psa\crypto_values.h"
#include <unistd.h>

static int write_ssl_and_get_response(mbedtls_ssl_context *ssl, unsigned char *buf, size_t len)
{
    int ret;
    unsigned char data[128];
    char code[4];
    size_t i, idx = 0;

    LUAT_DEBUG_PRINT("\n%s", buf);
    while (len && (ret = mbedtls_ssl_write(ssl, buf, len)) <= 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            LUAT_DEBUG_PRINT(" failed\n  ! mbedtls_ssl_write returned %d\n\n", ret);
            return -1;
        }

        luat_rtos_task_sleep(10);
    }

    do {
        len = sizeof(data) - 1;
        memset(data, 0, sizeof(data));
        ret = mbedtls_ssl_read(ssl, data, len);

        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            luat_rtos_task_sleep(10);
            continue;
        }

        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            return -1;
        }

        if (ret <= 0) {
            LUAT_DEBUG_PRINT("failed\n  ! mbedtls_ssl_read returned %d\n\n", ret);
            return -1;
        }

        LUAT_DEBUG_PRINT("\n%s", data);
        len = ret;
        for (i = 0; i < len; i++) {
            if (data[i] != '\n') {
                if (idx < 4) {
                    code[idx++] = data[i];
                }
                continue;
            }

            if (idx == 4 && code[0] >= '0' && code[0] <= '9' && code[3] == ' ') {
                code[3] = '\0';
                return atoi(code);
            }

            idx = 0;
        }
    } while (1);
}

static int do_handshake(mbedtls_ssl_context *ssl)
{
    int ret;
    uint32_t flags;
    unsigned char buf[1024];
    memset(buf, 0, 1024);

    /*
        Handshake
     */
    LUAT_DEBUG_PRINT("  . Performing the SSL/TLS handshake...");

    while ((ret = mbedtls_ssl_handshake(ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            LUAT_DEBUG_PRINT(" failed\n  ! mbedtls_ssl_handshake returned %d: %s\n\n", ret, buf);
            return -1;
        }
    }

    return 0;
}

static int write_ssl_data(mbedtls_ssl_context *ssl, unsigned char *buf, size_t len)
{
    int ret;

    mbedtls_printf("\n%s", buf);
    while (len && (ret = mbedtls_ssl_write(ssl, buf, len)) <= 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            mbedtls_printf(" failed\n  ! mbedtls_ssl_write returned %d\n\n", ret);
            return -1;
        }
    }

    return 0;
}

void send_email(char *user, char *pass, char *addr, char *body) {
    
    mbedtls_ssl_context ssl_cxt;
    const char* hostname = "x-device";
    
    unsigned char buf[SMTP_BUF_SIZE]  = {0};
    unsigned char base[SMTP_BUF_SIZE]  = {0};
    int ret = 1, len;

    int exit_code = MBEDTLS_EXIT_FAILURE;

	mbedtls_net_context server_fd;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    int i;
    size_t n;
    char *p, *q;
    const int *list;

    /*
     * Make sure memory references are valid in case we exit early.
     */
    mbedtls_net_init(&server_fd);
    mbedtls_ssl_init(&ssl_cxt);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    memset(&buf, 0, sizeof(buf));

    const char *pers = "smtp_client";
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                (const unsigned char *) pers, strlen(pers));
    if (ret != 0) {
        LUAT_DEBUG_PRINT("failed! mbedtls_ctr_drbg_seed returned -0x%x\n", -ret);
        goto exit;
    }



    if ((ret = mbedtls_net_connect(&server_fd, SMTP_ADDR, SMTP_PORT_STR, MBEDTLS_NET_PROTO_TCP, 0)) != 0) {
        LUAT_DEBUG_PRINT(" failed\n  ! mbedtls_net_connect returned %d\n\n", ret);
        goto exit;
    }
    LUAT_DEBUG_PRINT("Connected to tcp/%s/%s\n", SMTP_ADDR, SMTP_PORT_STR);

    if ((ret = mbedtls_ssl_config_defaults(&conf,
                                           MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        LUAT_DEBUG_PRINT(" failed\n  ! mbedtls_ssl_config_defaults returned %d\n\n", ret);
        goto exit;
    }
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);

    if ((ret = mbedtls_ssl_setup(&ssl_cxt, &conf)) != 0) {
        LUAT_DEBUG_PRINT(" failed\n  ! mbedtls_ssl_setup returned %d\n\n", ret);
        goto exit;
    }

    if ((ret = mbedtls_ssl_set_hostname(&ssl_cxt, SMTP_ADDR)) != 0) {
        LUAT_DEBUG_PRINT(" failed\n  ! mbedtls_ssl_set_hostname returned %d\n\n", ret);
        goto exit;
    }

    mbedtls_ssl_set_bio(&ssl_cxt, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);
    do_handshake(&ssl_cxt);
    if(ret != 0){
        LUAT_DEBUG_PRINT("ssl_mail_client fail\n");
        goto exit;
    }

    ret = write_ssl_and_get_response(&ssl_cxt, buf, 0);
    LUAT_DEBUG_PRINT("rcv ret %d :%s\n", ret, buf);

    // HELO & AUTH LOGIN
    LUAT_DEBUG_PRINT("EHLO x\n");

    len = sprintf((char *) buf, "EHLO %s\r\n", hostname);
    ret = write_ssl_and_get_response(&ssl_cxt, buf, len);

    LUAT_DEBUG_PRINT("EHLO rcv ret %d :%s\n", ret, buf);

    len = sprintf((char *) buf, "AUTH LOGIN\r\n");
    ret = write_ssl_and_get_response(&ssl_cxt, buf, len);
    if (ret < 200 || ret > 399) {
        LUAT_DEBUG_PRINT(" failed\n  ! server responded with %d\n\n", ret);
        goto exit;
    }

    // 用户名 & 密码
    LUAT_DEBUG_PRINT("  > Write username to server: %s", user);

    ret = mbedtls_base64_encode(base, sizeof(base), &n, (const unsigned char *) user,
                                    strlen(user));
    if (ret != 0) {
        LUAT_DEBUG_PRINT(" failed\n  ! mbedtls_base64_encode returned %d\n\n", ret);
        goto exit;
    }

    len = sprintf((char *) buf, "%s\r\n", base);
    ret = write_ssl_and_get_response(&ssl_cxt, buf, len);
    if (ret < 300 || ret > 399) {
        LUAT_DEBUG_PRINT(" failed\n  ! server responded with %d\n\n", ret);
        goto exit;
    }

    LUAT_DEBUG_PRINT("  > Write password to server: %s", pass);

    ret = mbedtls_base64_encode(base, sizeof(base), &n, (const unsigned char *) pass,
                                strlen(pass));

    if (ret != 0) {
        LUAT_DEBUG_PRINT(" failed\n  ! mbedtls_base64_encode returned %d\n\n", ret);
        goto exit;
    }
    len = sprintf((char *) buf, "%s\r\n", base);
    ret = write_ssl_and_get_response(&ssl_cxt, buf, len);
    if (ret < 200 || ret > 399) {
        LUAT_DEBUG_PRINT(" failed\n  ! server responded with %d\n\n", ret);
        goto exit;
    }

    // MAIL FROM & RCPT TO
    LUAT_DEBUG_PRINT("  > Write MAIL FROM to server:");

    len = mbedtls_snprintf((char *) buf, sizeof(buf), "MAIL FROM:<%s>\r\n", user);
    if (len < 0 || (size_t) len >= sizeof(buf)) {
        LUAT_DEBUG_PRINT(" failed\n  ! mbedtls_snprintf encountered error or truncated output\n\n");
        goto exit;
    }
    ret = write_ssl_and_get_response(&ssl_cxt, buf, len);
    if (ret < 200 || ret > 299) {
        LUAT_DEBUG_PRINT(" failed\n  ! server responded with %d\n\n", ret);
        goto exit;
    }

    LUAT_DEBUG_PRINT(" ok\n");

    LUAT_DEBUG_PRINT("  > Write RCPT TO to server:");

    len = mbedtls_snprintf((char *) buf, sizeof(buf), "RCPT TO:<%s>\r\n", addr);
    if (len < 0 || (size_t) len >= sizeof(buf)) {
        LUAT_DEBUG_PRINT(" failed\n  ! mbedtls_snprintf encountered error or truncated output\n\n");
        goto exit;
    }
    ret = write_ssl_and_get_response(&ssl_cxt, buf, len);
    if (ret < 200 || ret > 299) {
        LUAT_DEBUG_PRINT(" failed\n  ! server responded with %d\n\n", ret);
        goto exit;
    }

    LUAT_DEBUG_PRINT(" ok\n");

    // DATA & 邮件正文
    LUAT_DEBUG_PRINT("  > Write DATA to server:");

    len = sprintf((char *) buf, "DATA\r\n");
    ret = write_ssl_and_get_response(&ssl_cxt, buf, len);
    if (ret < 300 || ret > 399) {
        LUAT_DEBUG_PRINT(" failed\n  ! server responded with %d\n\n", ret);
        goto exit;
    }

    LUAT_DEBUG_PRINT(" ok\n");

    LUAT_DEBUG_PRINT("  > Write content to server:");

    len = mbedtls_snprintf((char *) buf, sizeof(buf),  "%s\r\n.\r\n", body);
    if (len < 0 || (size_t) len >= sizeof(buf)) {
        LUAT_DEBUG_PRINT(" failed\n  ! mbedtls_snprintf encountered error or truncated output\n\n");
        goto exit;
    }
    ret = write_ssl_data(&ssl_cxt, buf, len);

    len = sprintf((char *) buf, "\r\n.\r\n");
    ret = write_ssl_and_get_response(&ssl_cxt, buf, len);
    if (ret < 200 || ret > 299) {
        LUAT_DEBUG_PRINT(" failed\n  ! server responded with %d\n\n", ret);
        goto exit;
    }

    LUAT_DEBUG_PRINT(" ok\n");


    exit_code = MBEDTLS_EXIT_SUCCESS;

exit:
    mbedtls_ssl_close_notify(&ssl_cxt);
    mbedtls_net_free(&server_fd);
    mbedtls_ssl_free(&ssl_cxt);
    mbedtls_ssl_config_free(&conf);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);

    LUAT_DEBUG_PRINT(" send email end\n");
}