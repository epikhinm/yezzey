

#ifdef __cplusplus
extern "C"
{
#endif

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#include "postgres.h"
#include "fmgr.h"

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#include "utils/builtins.h"
#include "executor/spi.h"
#include "pgstat.h"

#include "yezzey.h"
#include "storage/lmgr.h"
#include "access/aosegfiles.h"
#include "utils/tqual.h"

#include "catalog/storage.h"
#include "catalog/storage_xlog.h"
#include "storage/smgr.h"
#include "catalog/pg_namespace.h"
#include "utils/catcache.h"
#include "utils/syscache.h"

// For GpIdentity
#include "c.h"
#include "cdb/cdbvars.h"

#ifdef __cplusplus
}
#endif

#include "external_storage_smgr.h"
#include "external_storage.h"

#include "gpreader.h"
#include "gpwriter.h"

int yezzey_log_level = INFO;
int yezzey_ao_log_level = INFO;


/*
* This function used by AO-related realtion functions
*/
bool
ensureFilepathLocal(const char *filepath)
{
	int fd, cp, errno_;

	fd = open(filepath, O_RDONLY);
	errno_ = errno;

	cp = fd;
	//elog(yezzey_ao_log_level, "[YEZZEY_SMGR] trying to open %s, result - %d, %d", filepath, fd, errno_);
	close(fd);

	return cp >= 0;
}

int
offloadFileToExternalStorage(
	const char * nspname,
	const char *relname, 
	const char *localPath, 
	int64 modcount, 
	int64 logicalEof,
	const char * external_storage_path) {

	int rc;
	int tot;
	char * buffer;
	char * bptr;
	size_t chunkSize;
	File vfd;
	int64 curr_read_chunk;
	int64 progress;
	int64 virtual_size;

	chunkSize = 1 << 20;
	buffer = (char*)malloc(chunkSize);
	vfd = PathNameOpenFile((FileName)localPath, O_RDONLY, 0600);
	if (vfd <= 0) {
		elog(ERROR, "yezzey: failed to open %s file to transfer to external storage", localPath);
	}
	progress = 0;

	auto iohandler = yezzey_io_handler_allocate(
		gpg_engine_path,
		gpg_key_id,
		storage_config,
		nspname,
		relname,
		storage_host /* host */,
		storage_bucket/*bucket*/, 
		storage_prefix /*prefix*/, 
		localPath
	);

	/* Create external storage reader handle to calculate total external files size. 
	* this is needed to skip offloading of data already present in external storage.
 	*/
	(void)createReaderHandle(iohandler, GpIdentity.segindex);

	if (!iohandler->read_ptr) {
		elog(ERROR, "yezzey: offloading %s: failed to get external storage read handler", localPath);
	}
	
	virtual_size = yezzey_calc_virtual_relation_size(iohandler);
	progress = virtual_size;
	FileSeek(vfd, progress, SEEK_SET);

	if (!external_storage_path) {
		(void)createWriterHandle(iohandler,
			GpIdentity.segindex,
			/* internal usage, modcount dump commited, no need to bump */
			modcount);
	
	} else {
		elog(WARNING, "yezzey: creating write handle to path %s", external_storage_path);
		(void)createWriterHandleToPath(
			iohandler, 
			GpIdentity.segindex, 
			/* internal usage, modcount dump commited, no need to bump */ 
			modcount);
	}

	if (!iohandler->write_ptr) {
		elog(ERROR, "yezzey: offloading %s: failed to get external storage write handler", localPath);
	}

	rc = 0;

	while (progress < logicalEof)
	{
		curr_read_chunk = chunkSize;
		if (progress + curr_read_chunk > logicalEof) {
			/* should not read beyond logical eof */
			curr_read_chunk = logicalEof - progress;
		}
 		/* code */
		rc = FileRead(vfd, buffer, curr_read_chunk);
		if (rc < 0) {
			return rc;
		}
		if (rc == 0) continue;

		tot = 0;
		bptr = buffer;

		while (tot < rc) {
			size_t currptrtot = rc - tot;
			if (!yezzey_writer_transfer_data(iohandler, bptr, &currptrtot)) {
				return -1;
			}

			tot += currptrtot;
			bptr += currptrtot;
		}

		progress += rc;
	}

	if (!yezzey_complete_r_transfer_data(iohandler)) {
		elog(ERROR, "yezzey: failed to complete %s offloading", localPath);
	}
	if (!yezzey_complete_w_transfer_data(iohandler)) {
		elog(ERROR, "yezzey: failed to complete %s offloading", localPath);
	}
	yezzey_io_free(iohandler);
	FileClose(vfd);
	free(buffer);

	return rc;
}

bool
ensureFileLocal(RelFileNode rnode, BackendId backend, ForkNumber forkNum, BlockNumber blkno)
{	
	/* MDB-19689: do not consult catalog */

    elog(yezzey_log_level, "ensuring %d is local", rnode.relNode);
    return true;
	StringInfoData path;
	bool result;
	initStringInfo(&path);

	result = ensureFilepathLocal(path.data);

	pfree(path.data);
	return result;
}


int
removeLocalFile(const char *localPath)
{
	int rc = remove(localPath);
	elog(yezzey_ao_log_level, "[YEZZEY_SMGR_BG] remove local file \"%s\", result: %d", localPath, rc);
	return rc;
}

static int
offloadRelationSegmentPath(
	const char * nspname,
	const char * relname, 
	const char * localpath, 
	int64 modcount, 
	int64 logicalEof,
	const char * external_storage_path) {
    if (!ensureFilepathLocal(localpath)) {
        // nothing to do
        return 0;
    }
	return offloadFileToExternalStorage(nspname, relname, localpath, modcount, logicalEof, external_storage_path);
}

int
offloadRelationSegment(
	Relation aorel, 
	RelFileNode rnode, 
	int segno, 
	int64 modcount, 
	int64 logicalEof, 
	bool remove_locally,
	const char * external_storage_path) {
    StringInfoData local_path;
	int rc;
	int64_t virtual_sz;
 	char * nspname;
	HeapTuple	tp;

    initStringInfo(&local_path);
	if (segno == 0) {
    	appendStringInfo(&local_path, "base/%d/%d", rnode.dbNode, rnode.relNode);
	} else {
    	appendStringInfo(&local_path, "base/%d/%d.%d", rnode.dbNode, rnode.relNode, segno);
	}

    elog(yezzey_ao_log_level, "contructed path %s", local_path.data);

	/* xlog goes first */
	// xlog_smgr_local_truncate(rnode, MAIN_FORKNUM, 'a');

	tp = SearchSysCache1(NAMESPACEOID, ObjectIdGetDatum(aorel->rd_rel->relnamespace));

	if (HeapTupleIsValid(tp))
	{
		Form_pg_namespace nsptup = (Form_pg_namespace) GETSTRUCT(tp);
		nspname = pstrdup(NameStr(nsptup->nspname));
		ReleaseSysCache(tp);
	} else {
		elog(ERROR, "yezzey: failed to get namescape name of relation %s", aorel->rd_rel->relname.data);
	}

	if ((rc = offloadRelationSegmentPath(nspname, aorel->rd_rel->relname.data, local_path.data, modcount, logicalEof, external_storage_path)) < 0) {
		pfree(local_path.data);
		return rc;
	}

	if (remove_locally) {
		/*wtf*/
		RelationDropStorageNoClose(aorel);
	}


	auto iohandler = yezzey_io_handler_allocate(
		gpg_engine_path,
		gpg_key_id,
		storage_config,
		nspname,
		aorel->rd_rel->relname.data,
		storage_host /*host*/,
		storage_bucket/*bucket*/, 
		storage_prefix /*prefix*/, 
		local_path.data
	);

	virtual_sz = yezzey_virtual_relation_size(iohandler, GpIdentity.segindex);

    elog(yezzey_ao_log_level, "yezzey: relation segment reached external storage path %s, virtual size %ld, logical eof %ld", local_path.data, virtual_sz, logicalEof);

	pfree(nspname);
    pfree(local_path.data);
	yezzey_io_free(iohandler);
    return 0;
}


int
statRelationSpaceUsage(Relation aorel, int segno, int64 modcount, int64 logicalEof, size_t *local_bytes, size_t *local_commited_bytes, size_t *external_bytes) {
    StringInfoData local_path;
	size_t virtual_sz;
	RelFileNode rnode;
	HeapTuple	tp;
	int vfd;
 	char * nspname;

	rnode = aorel->rd_node;


	tp = SearchSysCache1(NAMESPACEOID, ObjectIdGetDatum(aorel->rd_rel->relnamespace));

	if (HeapTupleIsValid(tp))
	{
		Form_pg_namespace nsptup = (Form_pg_namespace) GETSTRUCT(tp);
		nspname = pstrdup(NameStr(nsptup->nspname));
		ReleaseSysCache(tp);
	} else {
		elog(ERROR, "yezzey: failed to get namescape name of relation %s", aorel->rd_rel->relname.data);
	}


    initStringInfo(&local_path);
	if (segno == 0) {
    	appendStringInfo(&local_path, "base/%d/%d", rnode.dbNode, rnode.relNode);
	} else {
    	appendStringInfo(&local_path, "base/%d/%d.%d", rnode.dbNode, rnode.relNode, segno);
	}

    elog(yezzey_ao_log_level, "yezzey: contructed path %s", local_path.data);
	
	auto iohandler = yezzey_io_handler_allocate(
		gpg_engine_path,
		gpg_key_id,
		storage_config, 
		nspname,
		aorel->rd_rel->relname.data, 
		storage_host /*host*/,
		storage_bucket/*bucket*/, 
		storage_prefix /*prefix*/, 
		local_path.data
	);

	/* stat external storage usage */
	virtual_sz = yezzey_virtual_relation_size(iohandler, GpIdentity.segindex);

	*external_bytes = virtual_sz;

	/* No local storage cache logic for now */
	*local_bytes = 0;

	vfd = PathNameOpenFile(local_path.data, O_RDONLY, 0600);
	if (vfd != -1) {
		*local_bytes += FileSeek(vfd, 0, SEEK_END);
		FileClose(vfd);
	}

	// Assert(virtual_sz <= logicalEof);
	*local_commited_bytes = logicalEof - virtual_sz;

	pfree(nspname);
    pfree(local_path.data);
	yezzey_io_free(iohandler);
    return 0;
}

int
statRelationSpaceUsagePerExternalChunk(
	Relation aorel, 
	int segno,
	int64 modcount,
	int64 logicalEof,
	size_t *local_bytes,
	size_t *local_commited_bytes,
	yezzeyChunkMeta **list,
	size_t *cnt_chunks) {
    
	StringInfoData local_path;
	RelFileNode rnode;
	int vfd;
	struct externalChunkMeta * meta;
	HeapTuple tp;
	char * nspname;

	rnode = aorel->rd_node;
	
	initStringInfo(&local_path);
	if (segno == 0) {
		appendStringInfo(&local_path, "base/%d/%d", rnode.dbNode, rnode.relNode);
	} else {
		appendStringInfo(&local_path, "base/%d/%d.%d", rnode.dbNode, rnode.relNode, segno);
	}

    elog(yezzey_ao_log_level, "contructed path %s", local_path.data);
	
	tp = SearchSysCache1(NAMESPACEOID, ObjectIdGetDatum(aorel->rd_rel->relnamespace));
	
	if (HeapTupleIsValid(tp)) {
		Form_pg_namespace nsptup = (Form_pg_namespace) GETSTRUCT(tp);
		nspname = pstrdup(NameStr(nsptup->nspname));
		ReleaseSysCache(tp);
	} else {
		elog(ERROR, "yezzey: failed to get namescape name of relation %s", aorel->rd_rel->relname.data);
    }

	auto iohandler = yezzey_io_handler_allocate(
		gpg_engine_path,
		gpg_key_id,
		storage_config,
		nspname,
		aorel->rd_rel->relname.data, 
		storage_host /*host*/,
		storage_bucket/*bucket*/, 
		storage_prefix /*prefix*/, 
		local_path.data
	);

	/* stat external storage usage */
	yezzey_list_relation_chunks(
		iohandler, GpIdentity.segindex, cnt_chunks);

	pfree(nspname);

	Assert((*cnt_chunks) >= 0);
	meta = (struct externalChunkMeta*) malloc(sizeof(struct externalChunkMeta) * (*cnt_chunks));

	yezzey_copy_relation_chunks(iohandler, meta);

	// do copy;
	// list will be allocated in current PostgreSQL mempry context
	*list = (struct yezzeyChunkMeta*)palloc(sizeof(struct yezzeyChunkMeta) * (*cnt_chunks));

	for (size_t i = 0; i < *cnt_chunks; ++i){
		(*list)[i].chunkSize = meta[i].chunkSize;
		(*list)[i].chunkName = pstrdup(meta[i].chunkName);
		free((void*)meta[i].chunkName);
	}

	yezzey_list_relation_chunks_cleanup(iohandler);
	free(meta);
	/* No local storage cache logic for now */
	*local_bytes = 0;

	vfd = PathNameOpenFile(local_path.data, O_RDONLY, 0600);
	if (vfd != -1) {
		*local_bytes += FileSeek(vfd, 0, SEEK_END);
		FileClose(vfd);
	}

	*local_commited_bytes = 0;

    pfree(local_path.data);
	yezzey_io_free(iohandler);
    return 0;
}

int
loadRelationSegment(RelFileNode rnode, int segno) {
    StringInfoData path;
    if (segno == 0) {
        /* should never happen */
        return 0;
    }

    initStringInfo(&path);
    appendStringInfo(&path, "base/%d/%d.%d", rnode.dbNode, rnode.relNode, segno);

    elog(yezzey_ao_log_level, "contructed path %s", path.data);
    if (ensureFilepathLocal(path.data)) {
        // nothing to do
        return 0;
    }

#if 0
    if ((rc = getFilepathFromS3(path.data)) < 0) {
        pfree(path.data);
        return rc;
    }
#endif

    pfree(path.data);
    return 0;
}