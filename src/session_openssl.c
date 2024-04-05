#define _GNU_SOURCE

#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>

#include "compat.h"
#include "config.h"
#include "log_p.h"
#include "session.h"
#include "session_p.h"
#include "session_wrapper.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

void *
nc_tls_session_new_wrap(void *tls_cfg)
{
    SSL *session;

    session = SSL_new(tls_cfg);
    if (!session) {
        ERR(NULL, "Setting up TLS context failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return NULL;
    }

    return session;
}

void
nc_tls_session_destroy_wrap(void *tls_session)
{
    SSL_free(tls_session);
}

void *
nc_server_tls_config_new_wrap()
{
    SSL_CTX *tls_cfg;

    tls_cfg = SSL_CTX_new(TLS_server_method());
    NC_CHECK_ERRMEM_RET(!tls_cfg, NULL)

    return tls_cfg;
}

void *
nc_client_tls_config_new_wrap()
{
    SSL_CTX *tls_cfg;

    tls_cfg = SSL_CTX_new(TLS_client_method());
    NC_CHECK_ERRMEM_RET(!tls_cfg, NULL)

    return tls_cfg;
}

void
nc_tls_config_destroy_wrap(void *tls_cfg)
{
    SSL_CTX_free(tls_cfg);
}

void *
nc_tls_cert_new_wrap()
{
    X509 *cert;

    cert = X509_new();
    NC_CHECK_ERRMEM_RET(!cert, NULL)

    return cert;
}

void
nc_tls_cert_destroy_wrap(void *cert)
{
    X509_free(cert);
}

void *
nc_tls_privkey_new_wrap()
{
    EVP_PKEY *pkey;

    pkey = EVP_PKEY_new();
    NC_CHECK_ERRMEM_RET(!pkey, NULL);

    return pkey;
}

void
nc_tls_privkey_destroy_wrap(void *pkey)
{
    EVP_PKEY_free(pkey);
}

void *
nc_tls_cert_store_new_wrap()
{
    X509_STORE *store;

    store = X509_STORE_new();
    NC_CHECK_ERRMEM_RET(!store, NULL);

    return store;
}

void
nc_tls_cert_store_destroy_wrap(void *cert_store)
{
    X509_STORE_free(cert_store);
}

void *
nc_tls_crl_store_new_wrap()
{
    return NULL;
}

void
nc_tls_crl_store_destroy_wrap(void *crl)
{
    (void) crl;
    return;
}

void
nc_tls_set_authmode_wrap(void *tls_cfg)
{
    SSL_CTX_set_mode(tls_cfg, SSL_MODE_AUTO_RETRY);
}

int
nc_server_tls_set_config_defaults_wrap(void *tls_cfg)
{
    return 0;
}

void *
nc_tls_pem_to_cert_wrap(const char *cert_data)
{
    BIO *bio;
    X509 *cert;

    bio = BIO_new_mem_buf(cert_data, strlen(cert_data));
    if (!bio) {
        ERR(NULL, "Creating new bio failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return NULL;
    }

    cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    if (!cert) {
        ERR(NULL, "Parsing certificate data failed (%s).", ERR_reason_error_string(ERR_get_error()));
    }
    BIO_free(bio);
    return cert;
}

int
nc_tls_pem_to_cert_add_to_store_wrap(const char *cert_data, void *cert_store)
{
    int rc;
    X509 *cert;

    cert = nc_tls_pem_to_cert_wrap(cert_data);
    if (!cert) {
        return 1;
    }

    rc = X509_STORE_add_cert(cert_store, cert);
    X509_free(cert);
    if (!rc) {
        ERR(NULL, "Adding certificate to store failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return 1;
    }
    return 0;
}

void *
nc_tls_pem_to_privkey_wrap(const char *privkey_data)
{
    BIO *bio;
    EVP_PKEY *pkey;

    bio = BIO_new_mem_buf(privkey_data, strlen(privkey_data));
    if (!bio) {
        ERR(NULL, "Creating new bio failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return NULL;
    }

    pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    if (!pkey) {
        ERR(NULL, "Parsing certificate data failed (%s).", ERR_reason_error_string(ERR_get_error()));
    }
    BIO_free(bio);
    return pkey;
}

int
nc_tls_load_cert_private_key_wrap(void *tls_cfg, void *cert, void *pkey)
{
    int rc;

    rc = SSL_CTX_use_certificate(tls_cfg, cert);
    if (rc) {
        ERR(NULL, "Loading the server certificate failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    rc = SSL_CTX_use_PrivateKey(tls_cfg, pkey);
    if (rc) {
        ERR(NULL, "Loading the server private key failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    return 0;
}

int
nc_server_tls_crl_path(const char *crl_path, void *cert_store, void *crl_store)
{
    int ret = 0;
    X509_CRL *crl = NULL;
    FILE *f;

    (void) crl_store;

    f = fopen(crl_path, "r");
    if (!f) {
        ERR(NULL, "Unable to open CRL file \"%s\".", crl_path);
        return 1;
    }

    /* try PEM first */
    crl = PEM_read_X509_CRL(f, NULL, NULL, NULL);
    if (crl) {
        /* success */
        goto ok;
    }

    /* PEM failed, try DER */
    rewind(f);
    crl = d2i_X509_CRL_fp(f, NULL);
    if (!crl) {
        ERR(NULL, "Reading CRL from file \"%s\" failed.", crl_path);
        ret = 1;
        goto cleanup;
    }

ok:
    ret = X509_STORE_add_crl(cert_store, crl);
    if (!ret) {
        ERR(NULL, "Error adding CRL to store (%s).", ERR_reason_error_string(ERR_get_error()));
        ret = 1;
        goto cleanup;
    }
    /* ok */
    ret = 0;

cleanup:
    fclose(f);
    X509_CRL_free(crl);
    return ret;
}

int
nc_server_tls_add_crl_to_store_wrap(const unsigned char *crl_data, size_t size, void *cert_store, void *crl_store)
{
    int ret = 0;
    X509_CRL *crl = NULL;
    BIO *bio = NULL;

    (void) crl_store;

    bio = BIO_new_mem_buf(crl_data, size);
    if (!bio) {
        ERR(NULL, "Creating new bio failed (%s).", ERR_reason_error_string(ERR_get_error()));
        ret = 1;
        goto cleanup;
    }

    /* try DER first */
    crl = d2i_X509_CRL_bio(bio, NULL);
    if (crl) {
        /* it was DER */
        goto ok;
    }

    /* DER failed, try PEM next */
    crl = PEM_read_bio_X509_CRL(bio, NULL, NULL, NULL);
    if (!crl) {
        ERR(NULL, "Parsing downloaded CRL failed (%s).", ERR_reason_error_string(ERR_get_error()));
        ret = 1;
        goto cleanup;
    }

ok:
    /* we obtained the CRL, now add it to the CRL store */
    ret = X509_STORE_add_crl(cert_store, crl);
    if (!ret) {
        ERR(NULL, "Error adding CRL to store (%s).", ERR_reason_error_string(ERR_get_error()));
        ret = 1;
        goto cleanup;
    }
    /* ok */
    ret = 0;

cleanup:
    X509_CRL_free(crl);
    BIO_free(bio);
    return ret;
}

void
nc_server_tls_set_certs_wrap(void *tls_cfg, void *cert_store, void *crl_store)
{
    (void) crl_store;

    X509_STORE_set_flags(cert_store, X509_V_FLAG_CRL_CHECK);
    SSL_CTX_set_cert_store(tls_cfg, cert_store);
}

int
nc_server_tls_set_tls_versions_wrap(void *tls_cfg, unsigned int tls_versions)
{
    int rc = 1;

    /* first set the minimum version */
    if (tls_versions & NC_TLS_VERSION_10) {
        rc = SSL_CTX_set_min_proto_version(tls_cfg, TLS1_VERSION);
    } else if (tls_versions & NC_TLS_VERSION_11) {
        rc = SSL_CTX_set_min_proto_version(tls_cfg, TLS1_1_VERSION);
    } else if (tls_versions & NC_TLS_VERSION_12) {
        rc = SSL_CTX_set_min_proto_version(tls_cfg, TLS1_2_VERSION);
    } else if (tls_versions & NC_TLS_VERSION_13) {
        rc = SSL_CTX_set_min_proto_version(tls_cfg, TLS1_3_VERSION);
    }
    if (!rc) {
        ERR(NULL, "Setting TLS min version failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    /* then set the maximum version */
    if (tls_versions & NC_TLS_VERSION_13) {
        rc = SSL_CTX_set_max_proto_version(tls_cfg, TLS1_3_VERSION);
    } else if (tls_versions & NC_TLS_VERSION_12) {
        rc = SSL_CTX_set_max_proto_version(tls_cfg, TLS1_2_VERSION);
    } else if (tls_versions & NC_TLS_VERSION_11) {
        rc = SSL_CTX_set_max_proto_version(tls_cfg, TLS1_1_VERSION);
    } else if (tls_versions & NC_TLS_VERSION_10) {
        rc = SSL_CTX_set_max_proto_version(tls_cfg, TLS1_VERSION);
    }
    if (!rc) {
        ERR(NULL, "Setting TLS max version failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    return 0;
}

static int
nc_server_tls_verify_cb(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
    int ret = 0, depth, err;
    struct nc_tls_verify_cb_data *data;
    SSL *ssl;
    X509 *cert;

    /* retrieve callback data stored in the SSL struct */
    ssl = X509_STORE_CTX_get_ex_data(x509_ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
    data = SSL_get_ex_data(ssl, 0);

    /* get current cert and its depth */
    cert = X509_STORE_CTX_get_current_cert(x509_ctx);
    depth = X509_STORE_CTX_get_error_depth(x509_ctx);

    if (preverify_ok) {
        /* in-built verification was successful */
        ret = nc_server_tls_verify_cert(cert, depth, 0, data);
    } else {
        /* in-built verification failed, but the client still may be authenticated if:
         * 1) the peer cert matches any configured end-entity cert
         * 2) the peer cert has a valid chain of trust to any configured certificate authority cert
         * otherwise just continue until we reach the peer cert (depth = 0)
         */
        err = X509_STORE_CTX_get_error(x509_ctx);
        if ((depth == 0) && (err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT)) {
            /* not trusted self-signed peer certificate, case 1) */
            ret = nc_server_tls_verify_cert(cert, depth, 1, data);
        } else if ((err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT) || (err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY)) {
            /* full chain of trust is invalid, but it may be valid partially, case 2) */
            ret = nc_server_tls_verify_cert(cert, depth, 0, data);
        } else {
            VRB(NULL, "Cert verify: fail (%s).", X509_verify_cert_error_string(X509_STORE_CTX_get_error(x509_ctx)));
            ret = 1;
        }
    }

    if (ret == -1) {
        /* fatal error */
        return 0;
    } else if (!ret) {
        /* success */
        return 1;
    } else {
        if (depth > 0) {
            /* chain verify failed */
            return 1;
        } else {
            /* peer cert did not match */
            return 0;
        }
    }
}

void
nc_server_tls_set_verify_cb_wrap(void *tls_session, struct nc_tls_verify_cb_data *cb_data)
{
    SSL_set_ex_data(tls_session, 0, cb_data);
    SSL_set_verify(tls_session, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nc_server_tls_verify_cb);
}

char *
nc_server_tls_get_subject_wrap(void *cert)
{
    return X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
}

char *
nc_server_tls_get_issuer_wrap(void *cert)
{
    return X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
}

int
nc_server_tls_get_username_from_cert_wrap(void *cert, NC_TLS_CTN_MAPTYPE map_type, char **username)
{
    STACK_OF(GENERAL_NAME) * san_names;
    GENERAL_NAME *san_name;
    ASN1_OCTET_STRING *ip;
    int i, san_count;
    char *subject, *common_name;
    X509 *peer_cert = cert;

    *username = NULL;

    if (map_type == NC_TLS_CTN_COMMON_NAME) {
        subject = nc_server_tls_get_subject_wrap(peer_cert);
        NC_CHECK_ERRMEM_RET(!subject, -1);
        common_name = strstr(subject, "CN=");
        if (!common_name) {
            WRN(NULL, "Certificate does not include the commonName field.");
            free(subject);
            return 1;
        }
        common_name += 3;
        if (strchr(common_name, '/')) {
            *strchr(common_name, '/') = '\0';
        }
        *username = strdup(common_name);
        free(subject);
        NC_CHECK_ERRMEM_RET(!*username, -1);
    } else {
        /* retrieve subjectAltName's rfc822Name (email), dNSName and iPAddress values */
        san_names = X509_get_ext_d2i(peer_cert, NID_subject_alt_name, NULL, NULL);
        if (!san_names) {
            WRN(NULL, "Certificate has no SANs or failed to retrieve them.");
            return 1;
        }

        san_count = sk_GENERAL_NAME_num(san_names);
        for (i = 0; i < san_count; ++i) {
            san_name = sk_GENERAL_NAME_value(san_names, i);

            /* rfc822Name (email) */
            if (((map_type == NC_TLS_CTN_SAN_ANY) || (map_type == NC_TLS_CTN_SAN_RFC822_NAME)) &&
                    (san_name->type == GEN_EMAIL)) {
                *username = strdup((char *)ASN1_STRING_get0_data(san_name->d.rfc822Name));
                NC_CHECK_ERRMEM_RET(!*username, -1);
                break;
            }

            /* dNSName */
            if (((map_type == NC_TLS_CTN_SAN_ANY) || (map_type == NC_TLS_CTN_SAN_DNS_NAME)) &&
                    (san_name->type == GEN_DNS)) {
                *username = strdup((char *)ASN1_STRING_get0_data(san_name->d.dNSName));
                NC_CHECK_ERRMEM_RET(!*username, -1);
                break;
            }

            /* iPAddress */
            if (((map_type == NC_TLS_CTN_SAN_ANY) || (map_type == NC_TLS_CTN_SAN_IP_ADDRESS)) &&
                    (san_name->type == GEN_IPADD)) {
                ip = san_name->d.iPAddress;
                if (ip->length == 4) {
                    if (asprintf(username, "%d.%d.%d.%d", ip->data[0], ip->data[1], ip->data[2], ip->data[3]) == -1) {
                        ERRMEM;
                        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
                        return -1;
                    }
                    break;
                } else if (ip->length == 16) {
                    if (asprintf(username, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                            ip->data[0], ip->data[1], ip->data[2], ip->data[3], ip->data[4], ip->data[5],
                            ip->data[6], ip->data[7], ip->data[8], ip->data[9], ip->data[10], ip->data[11],
                            ip->data[12], ip->data[13], ip->data[14], ip->data[15]) == -1) {
                        ERRMEM;
                        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
                        return -1;
                    }
                    break;
                } else {
                    WRN(NULL, "SAN IP address in an unknown format (length is %d).", ip->length);
                }
            }
        }
        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free); // TODO

        if (i == san_count) {
            switch (map_type) {
            case NC_TLS_CTN_SAN_RFC822_NAME:
                WRN(NULL, "Certificate does not include the SAN rfc822Name field.");
                break;
            case NC_TLS_CTN_SAN_DNS_NAME:
                WRN(NULL, "Certificate does not include the SAN dNSName field.");
                break;
            case NC_TLS_CTN_SAN_IP_ADDRESS:
                WRN(NULL, "Certificate does not include the SAN iPAddress field.");
                break;
            case NC_TLS_CTN_SAN_ANY:
                WRN(NULL, "Certificate does not include any relevant SAN fields.");
                break;
            default:
                break;
            }
            return 1;
        }
    }

    return 0;
}

int
nc_server_tls_certs_match_wrap(void *cert1, void *cert2)
{
    return !X509_cmp(cert1, cert2);
}

int
nc_server_tls_md5_wrap(void *cert, unsigned char *buf)
{
    int rc;

    rc = X509_digest(cert, EVP_md5(), buf, NULL);
    if (rc) {
        ERR(NULL, "Calculating MD-5 digest failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    return 0;
}

int
nc_server_tls_sha1_wrap(void *cert, unsigned char *buf)
{
    int rc;

    rc = X509_digest(cert, EVP_sha1(), buf, NULL);
    if (rc) {
        ERR(NULL, "Calculating SHA-1 digest failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    return 0;
}

int
nc_server_tls_sha224_wrap(void *cert, unsigned char *buf)
{
    int rc;

    rc = X509_digest(cert, EVP_sha224(), buf, NULL);
    if (rc) {
        ERR(NULL, "Calculating SHA-224 digest failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    return 0;
}

int
nc_server_tls_sha256_wrap(void *cert, unsigned char *buf)
{
    int rc;

    rc = X509_digest(cert, EVP_sha256(), buf, NULL);
    if (rc) {
        ERR(NULL, "Calculating SHA-256 digest failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    return 0;
}

int
nc_server_tls_sha384_wrap(void *cert, unsigned char *buf)
{
    int rc;

    rc = X509_digest(cert, EVP_sha384(), buf, NULL);
    if (rc) {
        ERR(NULL, "Calculating SHA-384 digest failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    return 0;
}

int
nc_server_tls_sha512_wrap(void *cert, unsigned char *buf)
{
    int rc;

    rc = X509_digest(cert, EVP_sha512(), buf, NULL);
    if (rc) {
        ERR(NULL, "Calculating SHA-512 digest failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    return 0;
}

void
nc_server_tls_set_fd_wrap(void *tls_session, int sock, struct nc_tls_ctx *UNUSED(tls_ctx))
{
    SSL_set_fd(tls_session, sock);
}

int
nc_server_tls_handshake_step_wrap(void *tls_session)
{
    int ret = 0;

    ret = SSL_accept(tls_session);
    if (ret == 1) {
        return 1;
    } else if (ret == -1) {
        if ((SSL_get_error(tls_session, ret) == SSL_ERROR_WANT_READ) || (SSL_get_error(tls_session, ret) == SSL_ERROR_WANT_WRITE)) {
            return 0;
        }
    }

    return -1;
}

int
nc_client_tls_handshake_step_wrap(void *tls_session)
{
    int ret = 0;

    ret = SSL_connect(tls_session);
    if (ret == 1) {
        return 1;
    } else if (ret == -1) {
        if ((SSL_get_error(tls_session, ret) == SSL_ERROR_WANT_READ) || (SSL_get_error(tls_session, ret) == SSL_ERROR_WANT_WRITE)) {
            return 0;
        }
    }

    return -1;
}

void
nc_tls_ctx_destroy_wrap(struct nc_tls_ctx *UNUSED(tls_ctx))
{
    return;
}

int
nc_client_tls_load_cert_key_wrap(const char *cert_path, const char *key_path, void **cert, void **pkey)
{
    BIO *bio;
    X509 *cert_tmp;
    EVP_PKEY *pkey_tmp;

    bio = BIO_new_file(cert_path, "r");
    if (!bio) {
        ERR(NULL, "Opening the client certificate file \"%s\" failed.", cert_path);
        return 1;
    }

    cert_tmp = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!cert_tmp) {
        ERR(NULL, "Parsing the client certificate file \"%s\" failed.", cert_path);
        return 1;
    }

    bio = BIO_new_file(key_path, "r");
    if (!bio) {
        ERR(NULL, "Opening the client private key file \"%s\" failed.", key_path);
        X509_free(cert_tmp);
        return 1;
    }

    pkey_tmp = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey_tmp) {
        ERR(NULL, "Parsing the client private key file \"%s\" failed.", key_path);
        X509_free(cert_tmp);
        return 1;
    }

    *cert = cert_tmp;
    *pkey = pkey_tmp;

    return 0;
}

int
nc_client_tls_load_trusted_certs_wrap(void *cert_store, const char *file_path, const char *dir_path)
{
    if (!X509_STORE_load_locations(cert_store, file_path, dir_path)) {
        ERR(NULL, "Loading CA certs from file \"%s\" or directory \"%s\" failed (%s).", file_path, dir_path, ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    return 0;
}

int
nc_client_tls_load_crl_wrap(void *cert_store, void *UNUSED(crl_store), const char *file_path, const char *dir_path)
{
    if (!X509_STORE_load_locations(cert_store, file_path, dir_path)) {
        ERR(NULL, "Loading CRLs from file \"%s\" or directory \"%s\" failed (%s).", file_path, dir_path, ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    return 0;
}

int
nc_client_tls_set_hostname_wrap(void *tls_session, const char *hostname)
{
    int ret = 0;
    X509_VERIFY_PARAM *vpm = NULL;

    vpm = X509_VERIFY_PARAM_new();
    NC_CHECK_ERRMEM_RET(!vpm, 1);

    if (!X509_VERIFY_PARAM_set1_host(vpm, hostname, 0)) {
        ERR(NULL, "Failed to set expected hostname (%s).", ERR_reason_error_string(ERR_get_error()));
        ret = 1;
        goto cleanup;
    }
    if (!SSL_CTX_set1_param(tls_session, vpm)) {
        ERR(NULL, "Failed to set verify param (%s).", ERR_reason_error_string(ERR_get_error()));
        ret = 1;
        goto cleanup;
    }

cleanup:
    X509_VERIFY_PARAM_free(vpm);
    return ret;
}

uint32_t
nc_tls_get_verify_result_wrap(void *tls_session)
{
    return SSL_get_verify_result(tls_session);
}

const char *
nc_tls_verify_error_string_wrap(uint32_t err_code)
{
    return X509_verify_cert_error_string(err_code);
}

void
nc_tls_print_error_string_wrap(int connect_ret, const char *peername, void *tls_session)
{
    switch (SSL_get_error(tls_session, connect_ret)) {
    case SSL_ERROR_SYSCALL:
        ERR(NULL, "TLS connection to \"%s\" failed (%s).", peername, errno ? strerror(errno) : "unexpected EOF");
        break;
    case SSL_ERROR_SSL:
        ERR(NULL, "TLS connection to \"%s\" failed (%s).", peername, ERR_reason_error_string(ERR_get_error()));
        break;
    default:
        ERR(NULL, "TLS connection to \"%s\" failed.", peername);
        break;
    }
}

void
nc_server_tls_print_accept_error_wrap(int accept_ret, void *tls_session)
{
    switch (SSL_get_error(tls_session, accept_ret)) {
    case SSL_ERROR_SYSCALL:
        ERR(NULL, "TLS accept failed (%s).", strerror(errno));
        break;
    case SSL_ERROR_SSL:
        ERR(NULL, "TLS accept failed (%s).", ERR_reason_error_string(ERR_get_error()));
        break;
    default:
        ERR(NULL, "TLS accept failed.");
        break;
    }
}

int
nc_der_to_pubkey_wrap(const unsigned char *der, long len)
{
    int ret;
    EVP_PKEY *pkey;

    pkey = d2i_PUBKEY(NULL, &der, len);
    if (pkey) {
        /* success */
        ret = 0;
    } else {
        /* fail */
        ret = 1;
    }

    EVP_PKEY_free(pkey);
    return ret;
}

int
nc_base64_decode_wrap(const char *base64, char **bin)
{
    BIO *bio, *bio64 = NULL;
    size_t used = 0, size = 0, r = 0;
    void *tmp = NULL;
    int nl_count, i, remainder, ret = 0;
    char *b64;

    /* insert new lines into the base64 string, so BIO_read works correctly */
    nl_count = strlen(base64) / 64;
    remainder = strlen(base64) - 64 * nl_count;
    b64 = calloc(strlen(base64) + nl_count + 1, 1);
    NC_CHECK_ERRMEM_RET(!b64, -1);

    for (i = 0; i < nl_count; i++) {
        /* copy 64 bytes and add a NL */
        strncpy(b64 + i * 65, base64 + i * 64, 64);
        b64[i * 65 + 64] = '\n';
    }

    /* copy the rest */
    strncpy(b64 + i * 65, base64 + i * 64, remainder);

    bio64 = BIO_new(BIO_f_base64());
    if (!bio64) {
        ERR(NULL, "Error creating a bio (%s).", ERR_reason_error_string(ERR_get_error()));
        ret = -1;
        goto cleanup;
    }

    bio = BIO_new_mem_buf(b64, strlen(b64));
    if (!bio) {
        ERR(NULL, "Error creating a bio (%s).", ERR_reason_error_string(ERR_get_error()));
        ret = -1;
        goto cleanup;
    }

    BIO_push(bio64, bio);

    /* store the decoded base64 in bin */
    *bin = NULL;
    do {
        size += 64;

        tmp = realloc(*bin, size);
        if (!tmp) {
            ERRMEM;
            free(*bin);
            *bin = NULL;
            ret = -1;
            goto cleanup;
        }
        *bin = tmp;

        r = BIO_read(bio64, *bin + used, 64);
        used += r;
    } while (r == 64);

    ret = size;

cleanup:
    free(b64);
    BIO_free_all(bio64);
    return ret;
}

int
nc_base64_encode_wrap(const unsigned char *bin, size_t len, char **base64)
{
    BIO *bio = NULL, *b64 = NULL;
    BUF_MEM *bptr;
    int ret = 0;

    bio = BIO_new(BIO_s_mem());
    if (!bio) {
        ERR(NULL, "Error creating a bio (%s).", ERR_reason_error_string(ERR_get_error()));
        ret = -1;
        goto cleanup;
    }

    b64 = BIO_new(BIO_f_base64());
    if (!b64) {
        ERR(NULL, "Error creating a bio (%s).", ERR_reason_error_string(ERR_get_error()));
        ret = -1;
        goto cleanup;
    }

    bio = BIO_push(b64, bio);
    BIO_write(bio, bin, len);
    if (BIO_flush(bio) != 1) {
        ERR(NULL, "Error flushing the bio (%s).", ERR_reason_error_string(ERR_get_error()));
        ret = -1;
        goto cleanup;
    }

    BIO_get_mem_ptr(bio, &bptr);
    *base64 = strndup(bptr->data, bptr->length);
    NC_CHECK_ERRMEM_GOTO(!*base64, ret = -1, cleanup);

cleanup:
    BIO_free_all(bio);
    return ret;
}

static char *
nc_ssl_error_get_reasons(void)
{
    unsigned int e;
    int reason_size, reason_len;
    char *reasons = NULL;

    reason_size = 1;
    reason_len = 0;
    while ((e = ERR_get_error())) {
        if (reason_len) {
            /* add "; " */
            reason_size += 2;
            reasons = nc_realloc(reasons, reason_size);
            NC_CHECK_ERRMEM_RET(!reasons, NULL);
            reason_len += sprintf(reasons + reason_len, "; ");
        }
        reason_size += strlen(ERR_reason_error_string(e));
        reasons = nc_realloc(reasons, reason_size);
        NC_CHECK_ERRMEM_RET(!reasons, NULL);
        reason_len += sprintf(reasons + reason_len, "%s", ERR_reason_error_string(e));
    }

    return reasons;
}

int
nc_tls_read_wrap(struct nc_session *session, unsigned char *buf, size_t size)
{
    int rc, err;
    char *reasons;
    SSL *tls_session = session->ti.tls.session;

    ERR_clear_error();
    rc = SSL_read(tls_session, buf, size);
    if (rc <= 0) {
        err = SSL_get_error(tls_session, rc);
        switch (err) {
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            rc = 0;
            break;
        case SSL_ERROR_ZERO_RETURN:
            ERR(session, "Communication socket unexpectedly closed (OpenSSL).");
            session->status = NC_STATUS_INVALID;
            session->term_reason = NC_SESSION_TERM_DROPPED;
            rc = -1;
            break;
        case SSL_ERROR_SYSCALL:
            ERR(session, "TLS socket error (%s).", errno ? strerror(errno) : "unexpected EOF");
            session->status = NC_STATUS_INVALID;
            session->term_reason = NC_SESSION_TERM_OTHER;
            rc = -1;
            break;
        case SSL_ERROR_SSL:
            reasons = nc_ssl_error_get_reasons();
            ERR(session, "TLS communication error (%s).", reasons);
            free(reasons);
            session->status = NC_STATUS_INVALID;
            session->term_reason = NC_SESSION_TERM_OTHER;
            rc = -1;
            break;
        default:
            ERR(session, "Unknown TLS error occurred (err code %d).", err);
            session->status = NC_STATUS_INVALID;
            session->term_reason = NC_SESSION_TERM_OTHER;
            rc = -1;
            break;
        }
    }

    return rc;
}

int
nc_tls_write_wrap(struct nc_session *session, const unsigned char *buf, size_t size)
{
    int rc, err;
    char *reasons;
    SSL *tls_session = session->ti.tls.session;

    ERR_clear_error();
    rc = SSL_write(tls_session, buf, size);
    if (rc < 1) {
        err = SSL_get_error(tls_session, rc);
        switch (err) {
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_READ:
            rc = 0;
            break;
        case SSL_ERROR_ZERO_RETURN:
            ERR(session, "TLS connection was properly closed.");
            rc = -1;
            break;
        case SSL_ERROR_SYSCALL:
            ERR(session, "TLS socket error (%s).", errno ? strerror(errno) : "unexpected EOF");
            rc = -1;
            break;
        case SSL_ERROR_SSL:
            reasons = nc_ssl_error_get_reasons();
            ERR(session, "TLS communication error (%s).", reasons);
            free(reasons);
            rc = -1;
            break;
        default:
            ERR(session, "Unknown TLS error occurred (err code %d).", err);
            rc = -1;
            break;
        }
    }

    return rc;
}

int
nc_tls_have_pending_wrap(void *tls_session)
{
    return SSL_pending(tls_session);
}

int
nc_tls_get_fd_wrap(const struct nc_session *session)
{
    return SSL_get_fd(session->ti.tls.session);
}

void
nc_tls_close_notify_wrap(void *tls_session)
{
    SSL_shutdown(tls_session);
}

void *
nc_tls_import_key_file_wrap(const char *key_path, FILE *file)
{
    EVP_PKEY *pkey;

    pkey = PEM_read_PrivateKey(file, NULL, NULL, NULL);
    if (!pkey) {
        ERR(NULL, "Parsing the private key file \"%s\" failed (%s).", key_path, ERR_reason_error_string(ERR_get_error()));
    }

    return pkey;
}

void *
nc_tls_import_cert_file_wrap(const char *cert_path)
{
    X509 *cert;
    FILE *file;

    file = fopen(cert_path, "r");
    if (!file) {
        ERR(NULL, "Opening the certificate file \"%s\" failed.", cert_path);
        return NULL;
    }

    cert = PEM_read_X509(file, NULL, NULL, NULL);
    fclose(file);
    if (!cert) {
        ERR(NULL, "Parsing the certificate file \"%s\" failed (%s).", cert_path, ERR_reason_error_string(ERR_get_error()));
    }
    return cert;
}

char *
nc_tls_export_key_wrap(void *pkey)
{
    BIO *bio = NULL;
    char *pem = NULL;

    bio = BIO_new(BIO_s_mem());
    if (!bio) {
        ERR(NULL, "Creating new bio failed (%s).", ERR_reason_error_string(ERR_get_error()));
        goto cleanup;
    }

    if (!PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL)) {
        ERR(NULL, "Exporting the private key failed (%s).", ERR_reason_error_string(ERR_get_error()));
        goto cleanup;
    }

    pem = malloc(BIO_number_written(bio) + 1);
    NC_CHECK_ERRMEM_GOTO(!pem, , cleanup);

    BIO_read(bio, pem, BIO_number_written(bio));
    pem[BIO_number_written(bio)] = '\0';

cleanup:
    BIO_free(bio);
    return pem;
}

char *
nc_tls_export_cert_wrap(void *cert)
{
    int rc, cert_len;
    BIO *bio = NULL;
    char *pem = NULL;

    bio = BIO_new(BIO_s_mem());
    if (!bio) {
        ERR(NULL, "Creating new bio failed (%s).", ERR_reason_error_string(ERR_get_error()));
        goto cleanup;
    }

    rc = PEM_write_bio_X509(bio, cert);
    if (!rc) {
        ERR(NULL, "Exporting the certificate failed (%s).", ERR_reason_error_string(ERR_get_error()));
        goto cleanup;
    }

    cert_len = BIO_pending(bio);
    if (cert_len <= 0) {
        ERR(NULL, "Getting the certificate length failed (%s).", ERR_reason_error_string(ERR_get_error()));
        goto cleanup;
    }

    pem = malloc(cert_len + 1);
    NC_CHECK_ERRMEM_GOTO(!pem, , cleanup);

    /* read the cert from bio */
    rc = BIO_read(bio, pem, cert_len);
    if (rc <= 0) {
        ERR(NULL, "Reading the certificate failed (%s).", ERR_reason_error_string(ERR_get_error()));
        free(pem);
        pem = NULL;
        goto cleanup;
    }

    pem[cert_len] = '\0';

cleanup:
    BIO_free(bio);
    return pem;
}

char *
nc_tls_export_pubkey_wrap(void *pkey)
{
    BIO *bio = NULL;
    char *pem = NULL;

    bio = BIO_new(BIO_s_mem());
    if (!bio) {
        ERR(NULL, "Creating new bio failed (%s).", ERR_reason_error_string(ERR_get_error()));
        goto cleanup;
    }

    if (!PEM_write_bio_PUBKEY(bio, pkey)) {
        ERR(NULL, "Exporting the public key failed (%s).", ERR_reason_error_string(ERR_get_error()));
        goto cleanup;
    }

    pem = malloc(BIO_number_written(bio) + 1);
    NC_CHECK_ERRMEM_GOTO(!pem, , cleanup);

    BIO_read(bio, pem, BIO_number_written(bio));
    pem[BIO_number_written(bio)] = '\0';

cleanup:
    BIO_free(bio);
    return pem;
}

int
nc_tls_export_key_der_wrap(void *pkey, unsigned char **der, size_t *size)
{
    *der = NULL;

    *size = i2d_PrivateKey(pkey, der);
    if (*size < 0) {
        ERR(NULL, "Exporting the private key to DER format failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    return 0;
}

int
nc_tls_privkey_is_rsa_wrap(void *pkey)
{
    return EVP_PKEY_is_a(pkey, "RSA");
}

int
nc_tls_get_rsa_pubkey_params_wrap(void *pkey, void **e, void **n)
{
    BIGNUM *exp, *mod;

    if (!EVP_PKEY_get_bn_param(pkey, "e", &exp)) {
        ERR(NULL, "Getting the RSA public exponent failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return 1;
    }

    if (!EVP_PKEY_get_bn_param(pkey, "n", &mod)) {
        ERR(NULL, "Getting the RSA modulus failed (%s).", ERR_reason_error_string(ERR_get_error()));
        BN_free(exp);
        return 1;
    }

    *e = exp;
    *n = mod;
    return 0;
}

int
nc_tls_privkey_is_ec_wrap(void *pkey)
{
    return EVP_PKEY_is_a(pkey, "EC");
}

char *
nc_tls_get_ec_group_wrap(void *pkey)
{
    size_t ec_group_len = 0;
    char *ec_group = NULL;

    if (!EVP_PKEY_get_utf8_string_param(pkey, "group", NULL, 0, &ec_group_len)) {
        ERR(NULL, "Getting EC group length failed (%s).", ERR_reason_error_string(ERR_get_error()));
        return NULL;
    }

    /* alloc mem for group + 1 for \0 */
    ec_group = malloc(ec_group_len + 1);
    NC_CHECK_ERRMEM_RET(!ec_group, NULL);

    /* get the group */
    if (!EVP_PKEY_get_utf8_string_param(pkey, "group", ec_group, ec_group_len + 1, NULL)) {
        ERR(NULL, "Getting EC group failed (%s).", ERR_reason_error_string(ERR_get_error()));
        free(ec_group);
        return NULL;
    }

    return ec_group;
}

int
nc_tls_get_ec_pubkey_param_wrap(void *pkey, unsigned char **bin, int *bin_len)
{
    int ret = 0;
    BIGNUM *p;

    if (!EVP_PKEY_get_bn_param(pkey, "p", &p)) {
        ERR(NULL, "Getting public key point from the EC private key failed (%s).", ERR_reason_error_string(ERR_get_error()));
        ret = 1;
        goto cleanup;
    }

    /* prepare buffer for converting p to binary */
    *bin = malloc(BN_num_bytes(p));
    NC_CHECK_ERRMEM_GOTO(!*bin, ret = 1, cleanup);

    /* convert to binary */
    *bin_len = BN_bn2bin(p, *bin);

cleanup:
    BN_free(p);
    return ret;
}

int
nc_tls_get_bn_num_bytes_wrap(void *bn)
{
    return BN_num_bytes(bn);
}

void
nc_tls_bn_bn2bin_wrap(void *bn, unsigned char *bin)
{
    BN_bn2bin(bn, bin);
}

void *
nc_tls_import_pubkey_file_wrap(const char *pubkey_path)
{
    FILE *f;
    EVP_PKEY *pk = NULL;

    f = fopen(pubkey_path, "r");
    if (!f) {
        ERR(NULL, "Unable to open file \"%s\".", pubkey_path);
        return NULL;
    }

    /* read the pubkey from file */
    pk = PEM_read_PUBKEY(f, NULL, NULL, NULL);
    fclose(f);
    if (!pk) {
        ERR(NULL, "Reading public key from file \"%s\" failed (%s).", pubkey_path, ERR_reason_error_string(ERR_get_error()));
        return NULL;
    }

    return pk;
}