/**
 * @copyright
 *
 * Tencent is pleased to support the open source community by making IoT Hub available.
 * Copyright(C) 2018 - 2021 THL A29 Limited, a Tencent company.All rights reserved.
 *
 * Licensed under the MIT License(the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file qcloud_iot_tls_client.c
 * @brief implements tls client with mbedtls
 * @author fancyxu (fancyxu@tencent.com)
 * @version 1.0
 * @date 2021-07-12
 *
 * @par Change Log:
 * <table>
 * <tr><th>Date       <th>Version <th>Author    <th>Description
 * <tr><td>2021-07-12 <td>1.0     <td>fancyxu   <td>first commit
 * </table>
 */

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/debug.h"
#include "mbedtls/gcm.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/md.h"
#include "mbedtls/rsa.h"
#include "mbedtls/compat-1.3.h"
//#include "mbedtls/config.h"

#include "HAL_Platform.h"
#include "utils_log.h"

// psk模式加密套件
static const int32_t ciphersuites_psk[] = {MBEDTLS_TLS_PSK_WITH_AES_128_CBC_SHA, MBEDTLS_TLS_PSK_WITH_AES_256_CBC_SHA, 0};
// 证书模式加密套件
static const int32_t ciphersuites_cert[] = {MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256, 0};
/**
 * @brief Data structure for mbedtls SSL connection
 *
 */
typedef struct {
    mbedtls_net_context      socket_fd;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       ssl_conf;
    mbedtls_x509_crt         ca_cert;
    mbedtls_pk_context       private_key;
} TLSHandle;

/**
 * @brief mbedtls SSL client init
 *
 * 1. call a series of mbedtls init functions
 * 2. init and set seed for random functions
 * 3. load CA file, cert files or PSK
 *
 * @param[in,out] tls_handle mbedtls TLS handle
 * @param[in] connect_params device info for TLS connection
 * @return @see IotReturnCode
 */
static int32_t HAL_TlsInit(TLSHandle *tls_handle, const TLSConnectParams *connect_params)
{
    int32_t rc;

    mbedtls_net_init(&tls_handle->socket_fd);
    mbedtls_ssl_init(&tls_handle->ssl);
    mbedtls_ssl_config_init(&tls_handle->ssl_conf);
    mbedtls_ctr_drbg_init(&tls_handle->ctr_drbg);
    mbedtls_entropy_init(&tls_handle->entropy);
    mbedtls_x509_crt_init(&tls_handle->ca_cert);

    rc = mbedtls_ctr_drbg_seed(&tls_handle->ctr_drbg, mbedtls_entropy_func, &tls_handle->entropy, NULL, 0);
    if (rc) {
        Log_e("mbedtls_ctr_drbg_seed failed returned 0x%04x", -rc);
        return ERR_CODE_TLS_INIT;
    }

    if (connect_params->ca_crt_pem != NULL) {
        if ((rc = mbedtls_x509_crt_parse(&(tls_handle->ca_cert), (const unsigned char *)connect_params->ca_crt_pem,
                                          (connect_params->ca_crt_pem_len + 1)))) {
            Log_e("parse ca crt failed returned 0x%04x", rc < 0 ? -rc : rc);
            return ERR_CODE_TLS_CRTINVALID;
        }
    }

    if (connect_params->psk_raw != NULL && connect_params->psk_id != NULL) {
        rc = mbedtls_ssl_conf_psk(&tls_handle->ssl_conf, (unsigned char *)connect_params->psk_raw, connect_params->psk_raw_length,
                              (const unsigned char *)connect_params->psk_id, strlen(connect_params->psk_id));
        if (rc) {
            Log_e("mbedtls_ssl_conf_psk fail 0x%04x", -rc);
            return ERR_CODE_TLS_INIT;
        }
    }

    return ERR_CODE_SUCCESS;
}

/**
 * @brief Free memory/resources allocated by mbedtls
 *
 * @param[in,out] tls_handle @see TLSHandle
 */
static void HAL_Tls_Free(TLSHandle *tls_handle)
{
    mbedtls_net_free(&(tls_handle->socket_fd));
    mbedtls_x509_crt_free(&tls_handle->ca_cert);
    mbedtls_ssl_free(&tls_handle->ssl);
    mbedtls_ssl_config_free(&tls_handle->ssl_conf);
    mbedtls_ctr_drbg_free(&tls_handle->ctr_drbg);
    mbedtls_entropy_free(&tls_handle->entropy);
    HAL_Free(tls_handle);
}

/**
 * @brief Tls setup and sharkhand
 *
 * @param[in] connect_params @see TLSConnectParams
 * @param[in] ip server host
 * @param[in] port server port
 * @param[out] fd tls fd
 * @return @see IotReturnCode
 */
int32_t HAL_TlsConnect(const TLSConnectParams *connect_params, const char *ip, uint16_t port, uint64_t *fd)
{
    int32_t rc = ERR_CODE_SUCCESS;
    TLSHandle *tls_handle = NULL;

    rc = HAL_Malloc(sizeof(TLSHandle), (void *)&tls_handle);
    if (rc) {
        return ERR_CODE_OS_NOMEM;
    }

    rc = HAL_TlsInit(tls_handle, connect_params);
    if (rc) {
        goto error;
    }

    if (strstr(connect_params->domain_checked, "log")) {
        UPLOAD_DBG("Setting up the SSL/TLS structure...");
    } else {
        Log_d("Setting up the SSL/TLS structure...");
    }
    rc = mbedtls_ssl_config_defaults(&tls_handle->ssl_conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc) {
        if (strstr(connect_params->domain_checked, "log")) {
            UPLOAD_ERR("mbedtls_ssl_config_defaults failed returned 0x%04x", -rc);
        } else {
            Log_e("mbedtls_ssl_config_defaults failed returned 0x%04x", -rc);
        }
        goto error;
    }

    mbedtls_ssl_conf_rng(&tls_handle->ssl_conf, mbedtls_ctr_drbg_random, &tls_handle->ctr_drbg);
    mbedtls_ssl_conf_verify(&(tls_handle->ssl_conf), connect_params->f_vrfy, (void *)connect_params->domain_checked);
    mbedtls_ssl_conf_authmode(&(tls_handle->ssl_conf), MBEDTLS_SSL_VERIFY_REQUIRED);
    if (&(tls_handle->ca_cert)) {
        mbedtls_ssl_conf_ca_chain(&tls_handle->ssl_conf, &tls_handle->ca_cert, NULL);
    }

    mbedtls_ssl_conf_read_timeout(&tls_handle->ssl_conf, connect_params->timeout_ms);
    rc = mbedtls_ssl_setup(&tls_handle->ssl, &tls_handle->ssl_conf);
    if (rc) {
        if (strstr(connect_params->domain_checked, "log")) {
            UPLOAD_ERR("mbedtls_ssl_setup failed returned 0x%04x", -rc);
        } else {
            Log_e("mbedtls_ssl_setup failed returned 0x%04x", -rc);
        }
        goto error;
    }

    // Set the hostname to check against the received server certificate and sni
    if (connect_params->sni) {
        rc = mbedtls_ssl_set_hostname(&tls_handle->ssl, connect_params->sni);
        if (rc) {
            if (strstr(connect_params->domain_checked, "log")) {
                UPLOAD_ERR("mbedtls_ssl_set_hostname failed returned 0x%04x", -rc);
            } else {
                Log_e("mbedtls_ssl_set_hostname failed returned 0x%04x", -rc);
            }
            goto error;
        }
    }

    // ciphersuites selection for PSK device
    if (connect_params->psk_raw) {
        mbedtls_ssl_conf_ciphersuites(&(tls_handle->ssl_conf), ciphersuites_psk);
    } else if (connect_params->ca_crt_pem) {
        mbedtls_ssl_conf_ciphersuites(&(tls_handle->ssl_conf), ciphersuites_cert);
    }

    mbedtls_ssl_set_bio(&tls_handle->ssl, &tls_handle->socket_fd, mbedtls_net_send, mbedtls_net_recv,
                        mbedtls_net_recv_timeout);
    if (strstr(connect_params->domain_checked, "log")) {
        UPLOAD_DBG("Performing the SSL/TLS handshake...");
        UPLOAD_DBG("Connecting to /%s/%d...", STRING_PTR_PRINT_SANITY_CHECK(ip), port);
    } else {
        Log_d("Performing the SSL/TLS handshake...");
        Log_d("Connecting to /%s/%d...", STRING_PTR_PRINT_SANITY_CHECK(ip), port);
    }
    rc = mbedtls_net_connect(&tls_handle->socket_fd, ip, port, MBEDTLS_NET_PROTO_TCP);
    if (rc) {
        goto error;
    }

    do {
        rc = mbedtls_ssl_handshake(&tls_handle->ssl);
        if (rc && rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (strstr(connect_params->domain_checked, "log")) {
                UPLOAD_ERR("mbedtls_ssl_handshake failed returned 0x%04x", -rc);
            } else {
                Log_e("mbedtls_ssl_handshake failed returned 0x%04x", -rc);
            }

            if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
                rc = ERR_CODE_TLS_CRTVERIFY;
                if (strstr(connect_params->domain_checked, "log")) {
                    UPLOAD_ERR("Unable to verify the server's certificate");
                } else { 
                    Log_e("Unable to verify the server's certificate");
                }
            } else {
                rc = ERR_CODE_TLS_HANDSHAKEFAIL;
            }

            goto error;
        }
    } while (rc);

    rc = mbedtls_ssl_get_verify_result(&(tls_handle->ssl));
    if (rc) {
        if (strstr(connect_params->domain_checked, "log")) {
            UPLOAD_ERR("mbedtls_ssl_get_verify_result failed returned 0x%04x", -rc);
        } else {
            Log_e("mbedtls_ssl_get_verify_result failed returned 0x%04x", -rc);
        }
        rc = ERR_CODE_TLS_CRTVERIFY;
        goto error;
    }

    mbedtls_ssl_conf_read_timeout(&tls_handle->ssl_conf, 200);

    if (strstr(connect_params->domain_checked, "log")) {
        UPLOAD_DBG("connected with /%s/%d...", STRING_PTR_PRINT_SANITY_CHECK(ip), port);
    } else {
        Log_d("connected with /%s/%d...", STRING_PTR_PRINT_SANITY_CHECK(ip), port);
    }
    *fd = (uint64_t)tls_handle;
    return rc;

error:
    HAL_Tls_Free(tls_handle);
    return rc;
}

/**
 * @brief Disconect and free
 *
 * @param[in] fd tls handle
 */
void HAL_TlsDisconnect(uint64_t fd)
{
    int32_t rc = 0;

    TLSHandle *tls_handle = (TLSHandle *)fd;
    if (!tls_handle) {
        Log_d("handle is NULL");
        return;
    }

    do {
        rc = mbedtls_ssl_close_notify(&tls_handle->ssl);
    } while (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE);
    HAL_Tls_Free(tls_handle);
}

/**
 * @brief Write msg with tls
 *
 * @param[in] fd tls handle
 * @param[in] buf msg to write
 * @param[in] write_len number of bytes to write
 * @param[in] timeout_ms timeout millsecond
 * @param[out] written_len number of bytes writtern
 * @return @see IotReturnCode
 */
int32_t HAL_TlsWrite(uint64_t fd, const void *buf, uint32_t write_len, uint32_t timeout_ms, uint32_t *written_len)
{
    uint32_t       written_so_far;
    int32_t        write_rc = 0;
    uint64_t       time_expired_ms, time_current_ms;

    TLSHandle *tls_handle = (TLSHandle *)fd;
    if (!tls_handle) {
        Log_d("handle is NULL");
        return ERR_CODE_TLS_WRITEFAIL;
    }
    HAL_TimeSysTickMsGet(&time_current_ms);
    time_expired_ms = time_current_ms + timeout_ms;

    for (written_so_far = 0; written_so_far < write_len; written_so_far += write_rc) {
        do {
            write_rc = mbedtls_ssl_write(&tls_handle->ssl, buf + written_so_far, write_len - written_so_far);
            if (write_rc < 0 && write_rc != MBEDTLS_ERR_SSL_WANT_READ && write_rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
                Log_e("HAL_TLS_write failed 0x%04x", -write_rc);
                return ERR_CODE_TLS_WRITEFAIL;
            }

            HAL_TimeSysTickMsGet(&time_current_ms);
            if (time_current_ms > time_expired_ms) {
                break;
            }
        } while (write_rc <= 0);

        HAL_TimeSysTickMsGet(&time_current_ms);
        if (time_current_ms > time_expired_ms) {
            break;
        }
    }

    *written_len = written_so_far;
    HAL_TimeSysTickMsGet(&time_current_ms);
    if ((time_current_ms > time_expired_ms) && (written_so_far != write_len)) {
        return ERR_CODE_TLS_WRITETIMEOUT;
    }
    return ERR_CODE_SUCCESS;
}

/**
 * @brief Read msg with tls
 *
 * @param[in] fd tls handle
 * @param[out] buf msg buffer
 * @param[in] read_len buffer len
 * @param[in] timeout_ms timeout millsecond
 * @param[out] readed_len number of bytes read
 * @return @see IotReturnCode
 */
int32_t HAL_TlsRead(uint64_t fd, void *buf, uint32_t read_len, uint32_t timeout_ms, uint32_t *readed_len)
{
    int32_t        read_rc;
    uint64_t       time_expired_ms, time_current_ms;

    TLSHandle *tls_handle = (TLSHandle *)fd;
    if (!tls_handle) {
        Log_d("handle is NULL");
        return ERR_CODE_TLS_READFAIL;
    }
    HAL_TimeSysTickMsGet(&time_current_ms);
    time_expired_ms = time_current_ms + timeout_ms;

    *readed_len = 0;

    do {
        read_rc = mbedtls_ssl_read(&tls_handle->ssl, buf + *readed_len, read_len - *readed_len);
        if (read_rc == 0) {
            Log_i("peer shutdown");
            return ERR_CODE_TLS_PEERSHUTDOWN;
        } else if (read_rc < 0 && read_rc != MBEDTLS_ERR_SSL_WANT_WRITE && read_rc != MBEDTLS_ERR_SSL_WANT_READ &&
            read_rc != MBEDTLS_ERR_SSL_TIMEOUT) {
            Log_e("iot_network_tls_read failed: 0x%08x", read_rc < 0 ? -read_rc : read_rc);
            return ERR_CODE_TLS_READFAIL;
        }
        *readed_len += read_rc > 0 ? read_rc : 0;

        HAL_TimeSysTickMsGet(&time_current_ms);
        if (time_current_ms > time_expired_ms) {
            break;
        }
    } while (*readed_len < read_len);

    if (*readed_len > 0) {
        return ERR_CODE_SUCCESS;
    }

    return *readed_len == 0 ? ERR_CODE_TLS_NOTHINGTOREAD : ERR_CODE_TLS_READTIMEOUT;
}

/**
 * @brief set server certificate verify
 *
 * @param[in] hostname server ceritificate host name
 * @param[in] crt server ceritificate
 * @param[in] depth server ceritificate level
 * @param[in] flags ceritificate verify errcode
 * @return @see IotReturnCode
 */
int32_t HAL_TlsServerCrtVerify(void *hostname, mbedtls_x509_crt *crt, int32_t depth, uint32_t *flags)
{
    uint32_t ignore_flag;

    ignore_flag = MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE | MBEDTLS_X509_BADCERT_CN_MISMATCH;
    if (*flags & ignore_flag) {
        *flags &= ~ignore_flag;
    }
    return *flags;
}

// RSA2048公钥加密
// input内容不能超过256字节，也就是input_len不能超过256
// output内容长度固定是256字节
#if 0
int32_t HAL_RSA2048PublicKeyEncrypt(const char *public_key_pem, uint8_t *input_lt_256, uint32_t input_lt_256_len, uint8_t *output)
{
  mbedtls_pk_context pk;
  mbedtls_entropy_context entropy;
  int32_t rc = ERR_CODE_SUCCESS;

  if (input_lt_256 == NULL || input_lt_256_len == 0 || input_lt_256_len > 256 || output == NULL) {
    return ERR_CODE_INVALIDPARAM;
  }

  mbedtls_pk_init(&pk);
  mbedtls_entropy_init(&entropy);
  //mbedtls_ctr_drbg_init(&ctr_drbg);

    Log_e("11111111111111111111111111111");

  rc = mbedtls_pk_parse_public_key(&pk, public_key_pem, strlen(public_key_pem) + 1);
  if (rc != 0) {
    Log_e("Failed to parse public rc: %d", rc);
    rc = ERR_CODE_GENERALFAIL;
    goto cleanup;
  }
    Log_e("222222222222222222222222222");

  if (mbedtls_pk_get_type(&pk) != MBEDTLS_PK_RSA) {
    Log_e("bad public key");
    rc = ERR_CODE_GENERALFAIL;
    goto cleanup;
  }
    Log_e("4333333333333333333333333333");

  mbedtls_rsa_context* rsa = mbedtls_pk_rsa(pk);

  mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V15, 0);
    Log_e("44444444444444444444444");

#if 0
  // 设置随机数生成器
  if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
    UTILS_RSA2048_PUB_ENCRYPT_PERS, sizeof(UTILS_RSA2048_PUB_ENCRYPT_PERS) - 1) != 0) {
    Log_e("Failed to initialize random generator");
    rc = ERR_CODE_GENERALFAIL;
    goto cleanup;
  }
#endif

  // 执行RSA公钥加密对称密钥
  rc = mbedtls_rsa_pkcs1_encrypt(rsa,
      mbedtls_ctr_drbg_random, NULL,
      MBEDTLS_RSA_PUBLIC,
      input_lt_256_len,
      input_lt_256,
      output);

    Log_e("555555555555555555555555555");
  if (rc != 0) {
     Log_e("pub enc fail, ret:%d", rc);
     rc = ERR_CODE_GENERALFAIL;
     goto cleanup;
  }

cleanup:
  mbedtls_pk_free(&pk);
  mbedtls_entropy_free(&entropy);
  //mbedtls_ctr_drbg_free(&ctr_drbg);

  return rc;
}
#endif
#define USE_MY_RSA
#ifdef USE_MY_RSA
int GenerateRandomEx(void *p_rng, unsigned char *output, size_t output_len)
{
    size_t rnglen = output_len;
    size_t rngoffset = 0;

    while (rnglen > 0) {
        *(output + rngoffset) = (unsigned char)rand();
        rngoffset++;
        rnglen--;
    }
    return 0;
}

int32_t HAL_RSA2048PublicKeyEncrypt(const char *public_key_pem, uint8_t *input, uint32_t input_len, uint8_t output[256])
{
    int ret;
    rsa_context *rsa;
    unsigned char buf[POLARSSL_MPI_MAX_SIZE] = {0};
    size_t n=strlen((const char*)public_key_pem);
    pk_context pk;
    pk_init( &pk );
    ret = pk_parse_public_key( &pk, public_key_pem, n+1 );
    if( ret != 0 )
    {
    	return ret;
    }  
    if( pk_get_type( &pk ) != POLARSSL_PK_RSA )
    {
        return -1;
    }
    rsa = pk_rsa( pk );
    rsa_set_padding( rsa, RSA_PKCS_V21, MBEDTLS_MD_SHA1);
    rsa->len = ( mpi_msb( &rsa->N ) + 7 ) >> 3;

	ret = rsa_rsaes_oaep_encrypt(rsa, GenerateRandomEx, NULL, RSA_PUBLIC, NULL, 0, input_len, input, buf);
    if(ret==0)
	{
        memcpy(output,buf,rsa->len);
    }
    else
    {
        pk_free( &pk );
        return -1;
    }
    pk_free( &pk );
    return 0;
}

// key固定32字节, iv固定12字节, tag固定16字节
int32_t HAL_AES256GCMEncrypt(uint8_t *input, uint32_t input_len, const uint8_t *key_256bit, const uint8_t *iv12,
    const uint8_t *aad, uint32_t aad_len,  uint8_t *output, uint32_t *output_len, uint8_t *tag16)
{
  mbedtls_gcm_context ctx;
  int32_t rc = 0;
  uint8_t tag[16] = {0};

  if (input == NULL || input_len == 0 || key_256bit == NULL || iv12 == NULL ||
      aad == NULL || aad_len == 0 || output == NULL || output_len == NULL) {
    return ERR_CODE_INVALIDPARAM;
  }

  if(mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key_256bit, 256) != 0) {
    Log_e("gcm setkey fail");
    return ERR_CODE_GENERALFAIL;
  }

  rc = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT,
      input_len,
      iv12, 12,
      aad, aad_len,
      input,
      output,
      16, tag);

  if (rc != 0) {
    Log_e("gcm fail, rc:%d", rc);
    return ERR_CODE_GENERALFAIL;
  }

  mbedtls_gcm_free(&ctx);

  if (tag16 != NULL) {
    memcpy(tag16, tag, sizeof(tag));
  }

  *output_len = input_len + 16;

  return ERR_CODE_SUCCESS;
}
#endif // USE_MY_RSA

//#define USE_ST_RSA
#ifdef USE_ST_RSA
int GenerateRandomEx(void *p_rng, unsigned char *output, size_t output_len)
{
    size_t rnglen = output_len;
    size_t rngoffset = 0;

    while (rnglen > 0) {
        *(output + rngoffset) = (unsigned char)rand();
        rngoffset++;
        rnglen--;
    }
    return 0;
}

int32_t HAL_RSA2048PublicKeyEncrypt(const char *public_key_pem, uint8_t *input, uint32_t input_len, uint8_t output[256])
{
    int ret;
    rsa_context *rsa;
    unsigned char buf[POLARSSL_MPI_MAX_SIZE] = {0};
    size_t n=strlen((const char*)public_key_pem);
    pk_context pk;
    pk_init( &pk );
    ret = pk_parse_public_key( &pk, public_key_pem, n+1 );
    if( ret != 0 )
    {
    	return ret;
    }  
    if( pk_get_type( &pk ) != POLARSSL_PK_RSA )
    {
        return -1;
    }
    rsa = pk_rsa( pk );
    rsa_set_padding( rsa, RSA_PKCS_V21, MBEDTLS_MD_SHA1);
    rsa->len = ( mpi_msb( &rsa->N ) + 7 ) >> 3;

	ret = rsa_rsaes_oaep_encrypt(rsa, GenerateRandomEx, NULL, RSA_PUBLIC, NULL, 0, input_len, input, buf);
    if(ret==0)
	{
        memcpy(output,buf,rsa->len);
    }
    else
    {
        pk_free( &pk );
        return -1;
    }
    pk_free( &pk );
    return 0;
}

int32_t HAL_AES256GCMEncrypt(uint8_t *input, uint32_t input_len, const uint8_t key[32], const uint8_t iv[12],const uint8_t *aad, uint32_t aad_len, uint8_t *output, uint32_t *output_len, uint8_t tag[16])
{
	int ret = 0;
    mbedtls_cipher_context_t ctx;
    const mbedtls_cipher_info_t *cipher_info = NULL;
    size_t olen = 0;

    if (input == NULL || output == NULL || output_len == NULL || key == NULL || iv == NULL || tag == NULL) {
        Log_e("HAL_AES256GCMEncrypt: Invalid input parameters");
        return ERR_CODE_INVALIDPARAM;
    }

    mbedtls_cipher_init(&ctx);

    /* Get AES-256-GCM cipher info */
    cipher_info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_256_GCM);
    if (cipher_info == NULL) {
        Log_e("HAL_AES256GCMEncrypt: Failed to get cipher info");
        return ERR_CODE_GENERALFAIL;
    }

    /* Initialize cipher context */
    ret = mbedtls_cipher_setup(&ctx, cipher_info);
    if (ret != 0) {
        Log_e("HAL_AES256GCMEncrypt: cipher_setup failed, ret=%d", ret);
        mbedtls_cipher_free(&ctx);
        return ERR_CODE_GENERALFAIL;
    }

    /* Set key */
    ret = mbedtls_cipher_setkey(&ctx, key, 256, MBEDTLS_ENCRYPT);
    if (ret != 0) {
        Log_e("HAL_AES256GCMEncrypt: setkey failed, ret=%d", ret);
        mbedtls_cipher_free(&ctx);
        return ERR_CODE_GENERALFAIL;
    }

    /* Set IV (Nonce) */
    ret = mbedtls_cipher_set_iv(&ctx, iv, 12);
    if (ret != 0) {
        Log_e("HAL_AES256GCMEncrypt: set_iv failed, ret=%d", ret);
        mbedtls_cipher_free(&ctx);
        return ERR_CODE_GENERALFAIL;
    }

    /* Add Additional Authenticated Data (AAD) if provided */
    if (aad != NULL && aad_len > 0) {
        ret = mbedtls_cipher_update_ad(&ctx, aad, aad_len);
        if (ret != 0) {
            Log_e("HAL_AES256GCMEncrypt: update_ad failed, ret=%d", ret);
            mbedtls_cipher_free(&ctx);
            return ERR_CODE_GENERALFAIL;
        }
    }

    /* Encrypt data */
    ret = mbedtls_cipher_update(&ctx, input, input_len, output, &olen);
    if (ret != 0) {
        Log_e("HAL_AES256GCMEncrypt: cipher_update failed, ret=%d", ret);
        mbedtls_cipher_free(&ctx);
        return ERR_CODE_GENERALFAIL;
    }

    *output_len = olen;

    /* Finish encryption and generate authentication tag */
    ret = mbedtls_cipher_finish(&ctx, output + olen, &olen);
    if (ret != 0) {
        Log_e("HAL_AES256GCMEncrypt: cipher_finish failed, ret=%d", ret);
        mbedtls_cipher_free(&ctx);
        return ERR_CODE_GENERALFAIL;
    }

    *output_len += olen;

    /* Get authentication tag */
    ret = mbedtls_cipher_write_tag(&ctx, tag, 16);
    if (ret != 0) {
        Log_e("HAL_AES256GCMEncrypt: write_tag failed, ret=%d", ret);
        mbedtls_cipher_free(&ctx);
        return ERR_CODE_GENERALFAIL;
    }

    mbedtls_cipher_free(&ctx);
    Log_d("HAL_AES256GCMEncrypt: Success, output_len=%u", *output_len);

    return ERR_CODE_SUCCESS;
}

#endif // USE_ST_RSA

//#define USE_BSJ_RSA
#ifdef USE_BSJ_RSA
static int32_t rsa_rng_init(mbedtls_entropy_context *entropy, mbedtls_ctr_drbg_context *ctr_drbg)
{
    int ret = 0;
    const char *pers = "rsa_encrypt_rng";

    // 1. 初始化熵源（打印初始化状态）
    mbedtls_entropy_init(entropy);
    Log_e("熵源上下文初始化完成\n");

    // 2. 初始化CTR_DRBG
    mbedtls_ctr_drbg_init(ctr_drbg);
    Log_e("CTR_DRBG上下文初始化完成\n");

    // 3. 为CTR_DRBG播种（关键：检查返回值并打印详情）
    ret = mbedtls_ctr_drbg_seed(ctr_drbg, mbedtls_entropy_func, entropy,
                                (const uint8_t *)pers, strlen(pers));
    if (ret != 0) {
        //char err_buf[100] = {0};
        //mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        Log_e("CTR_DRBG播种失败！错误码：%d \n", ret);
        // 额外打印熵源可用的收集器数量（排查熵源不足）
        //size_t entropy_sources = mbedtls_entropy_get_source_count(entropy);
        //Log_e("当前熵源收集器数量：%zu\n", entropy_sources);
        return -1;
    }

    Log_e("CTR_DRBG播种成功，RNG初始化完成\n");
    return 0;
}

static int32_t rsa_load_pub_key_from_pem(mbedtls_rsa_context *rsa_ctx, const char *pem_key)
{
    mbedtls_pk_context pk_ctx;
    int ret = 0;
    mbedtls_pk_init(&pk_ctx);

    ret = mbedtls_pk_parse_public_key(&pk_ctx, (const uint8_t *)pem_key, strlen(pem_key) + 1);
    if (ret != 0) {
        //char err_buf[100] = {0};
        //mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        Log_e("PEM公钥解析失败：%d \n", ret);
        goto exit;
    }

    if (mbedtls_pk_get_type(&pk_ctx) != MBEDTLS_PK_RSA || mbedtls_pk_get_bitlen(&pk_ctx) != 2048) {
        Log_e("密钥类型/长度错误\n");
        ret = -1;
        goto exit;
    }

    ret = mbedtls_rsa_copy(rsa_ctx, mbedtls_pk_rsa(pk_ctx));
    if (ret != 0) {
        //char err_buf[100] = {0};
        //mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        Log_e("RSA上下文拷贝失败：%d \n", ret);
        goto exit;
    }

exit:
    mbedtls_pk_free(&pk_ctx);
    return ret;
}

int32_t HAL_RSA2048PublicKeyEncrypt(const char *encrypt_public_key_pem, uint8_t *input, uint32_t input_len, uint8_t output[256])
{
    mbedtls_rsa_context rsa_ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    int ret = 0;

    Log_e("input_len:%u, :%s", input_len, input);

    // 1. 参数检查（OAEP最大明文长度190字节）
    if (input == NULL || output == NULL || strlen(encrypt_public_key_pem) == 0)
        return -1;
    if (input_len > 190 || input_len == 0) { // 核心修改1：OAEP长度限制
        Log_e("错误：OAEP填充明文长度需≤190字节（实际：%u）\n", input_len);
        return -1;
    }
    // 2. 初始化RNG
    ret = rsa_rng_init(&entropy, &ctr_drbg);
    if (ret != 0) return ret;

// 3. 初始化RSA上下文+SHA256上下文（核心修改2：OAEP依赖哈希）
    mbedtls_rsa_init(&rsa_ctx, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1); // 核心修改3：指定OAEP+SHA256

    // 4. 加载公钥
    ret = rsa_load_pub_key_from_pem(&rsa_ctx, encrypt_public_key_pem);
    if (ret != 0) goto exit_all;

    mbedtls_rsa_set_padding(&rsa_ctx, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1);
    // 5. 执行OAEP加密
    ret = mbedtls_rsa_rsaes_oaep_encrypt(
        &rsa_ctx,
        mbedtls_ctr_drbg_random, &ctr_drbg, MBEDTLS_RSA_PUBLIC,
        NULL, 0, // 自定义标签
        input_len,
        input,
        output
    );

    if (ret != 0) {
        //char err_buf[100] = {0};
        //mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        Log_e("RSA OAEP加密失败：%d \n", ret);
        ret = -1;
        goto exit_all;
    }
    ret = 0;

exit_all:
    mbedtls_rsa_free(&rsa_ctx);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return ret;
}
#endif // USE_BSJ_RSA


#ifdef __cplusplus
}
#endif
