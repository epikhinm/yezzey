#ifndef YEZZEY_IO_H
#define YEZZEY_IO_H

#include <thread>

#include "blocking_buf.h"

#include "gpgme.h"

#include "gpreader.h"
#include "gpwriter.h"

struct yezzey_io_handler {
    void * read_ptr; // GPReader *
    void * write_ptr; // GPWriter *

// 
    BlockingBuffer buf;

// private fields

//  GPG - related
    /* "/usr/bin/gpg" or something */
    const char * engine_path; 

    const char * gpg_key_id;
// S3 + WAL-G related

	const char * config_path;
     // schema name for relation
    const char * nspname;
    // relation name itself
	const char * relname;
    // s3 host
	const char * host;
    // s3 bucked
	const char * bucket;
    // wal-g specific prefix
	const char * external_storage_prefix;
    // base/5/12345.1
	const char * fileName;

//
//  GPG - related structs
    gpgme_data_t crypto_in;
    gpgme_data_t crypto_out;
    
    gpgme_ctx_t crypto_ctx;
    gpgme_key_t keys[2];

//  S3 + WAL-G - related structs

    GPReader * rhandle;
    GPWriter * whandle;

    // handler thread
    std::thread wt;
};


yezzey_io_handler * yezzey_io_handler_allocate(
    const char * engine_path,
    const char * gpg_key_id,
    const char * config_path,
    const char * nspname,
	const char * relname,
	const char * host,
	const char * bucket,
	const char * external_storage_prefix,
	const char * fileName
);

int yezzey_io_free(yezzey_io_handler * ptr);

bool yezzey_io_read(yezzey_io_handler * handle, char *buffer, int *amount);

bool yezzey_io_write(yezzey_io_handler * handle, char *buffer, int *amount);



#endif /* YEZZEY_IO_H */