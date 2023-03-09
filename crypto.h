
#ifndef YEZZEY_CRYPTO_H
#define YEZZEY_CRYPTO_H

#include <gpgme.h>
#include "io.h"

// first arg should be (expected to be) yezzey_io_handler * 
ssize_t
yezzey_crypto_stream_dec_read(void *handler, void *buffer, size_t size);


// first arg should be (expected to be) yezzey_io_handler * 
ssize_t
yezzey_crypto_stream_dec_write(void *handler, const void *buffer, size_t size);



// first arg should be (expected to be) yezzey_io_handler * 
ssize_t
yezzey_crypto_stream_enc_read(void *handler, void *buffer, size_t size);


// first arg should be (expected to be) yezzey_io_handler * 
ssize_t
yezzey_crypto_stream_enc_write(void *handler, const void *buffer, size_t size);


// yezzey_io_handler
void yezzey_crypto_stream_free(void *handler);

static struct gpgme_data_cbs yezzey_crypto_dec_cbs = {
    yezzey_crypto_stream_dec_read,
    yezzey_crypto_stream_dec_write,
    NULL,
    yezzey_crypto_stream_free,
};

static struct gpgme_data_cbs yezzey_crypto_enc_cbs = {
    yezzey_crypto_stream_enc_read,
    yezzey_crypto_stream_enc_write,
    NULL,
    yezzey_crypto_stream_free,
};

int yezzey_io_prepare_crypt(yezzey_io_handler &ptr, bool dec);

void yezzey_io_dispatch_encrypt(yezzey_io_handler &ptr);

void yezzey_io_dispatch_decrypt(yezzey_io_handler &ptr);

#endif /* YEZZEY_CRYPTO_H */