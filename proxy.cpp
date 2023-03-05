

#include "proxy.h"


typedef struct yezzey_vfd {
	int y_vfd; /* Either YEZZEY_* preserved fd or pg internal fd >= 9 */
	int localTmpVfd; /* for writing files */
	char * filepath;
	char * nspname;
	char * relname;
	int fileFlags; 
	int fileMode;
	int64 offset;
	int64 virtualSize;
	int64 modcount;
	void * rhandle;
	void * whandle;
} yezzey_vfd;

#define YEZZEY_VANANT_VFD 0
#define YEZZEY_NOT_OPENED 1
#define YEZZEY_OPENED 2
#define YEZZEY_MIN_VFD 3
#define MAXVFD 100

File virtualEnsure(SMGRFile file);

yezzey_vfd yezzey_vfd_cache[MAXVFD];

File s3ext = -2;

/* lazy allocate external storage connections */
int readprepare(SMGRFile file) {
#ifdef CACHE_LOCAL_WRITES_FEATURE
	StringInfoData localTmpPath;

	if (yezzey_vfd_cache[file].rhandle) {
		return;
	}
#endif

	yezzey_vfd_cache[file].rhandle = createReaderHandle(
		storage_config,
		yezzey_vfd_cache[file].nspname,
		yezzey_vfd_cache[file].relname,
		storage_host /*host*/,
		storage_bucket /*bucket*/, 
		storage_prefix /*prefix*/, 
		yezzey_vfd_cache[file].filepath, 
		GpIdentity.segindex);

	if (yezzey_vfd_cache[file].rhandle == NULL) {
		return -1;
	}

	Assert(yezzey_vfd_cache[file].rhandle != NULL);

#ifdef CACHE_LOCAL_WRITES_FEATURE

	initStringInfo(&localTmpPath);
	if (yezzey_vfd_cache[file].localTmpVfd) return;

	appendStringInfo(&localTmpPath, "%s_tmpbuf", yezzey_vfd_cache[file].filepath);

	yezzey_vfd_cache[file].localTmpVfd = 
	PathNameOpenFile(localTmpPath.data, yezzey_vfd_cache[file].fileFlags, yezzey_vfd_cache[file].fileMode);
	if (yezzey_vfd_cache[file].localTmpVfd <= 0) {
		// is ok
		elog(yezzey_ao_log_level, "failed to open proxy file for read %s", localTmpPath.data);
	} else {
		elog(yezzey_ao_log_level, "opened proxy file for read %s", localTmpPath.data);
	}
#endif

	return 0;
}

int writeprepare(SMGRFile file) {

	/* should be called once*/
	if (readprepare(file) == -1) {
		return -1;
	}

	yezzey_vfd_cache[file].whandle = createWriterHandle(
		storage_config,
		yezzey_vfd_cache[file].rhandle,
		yezzey_vfd_cache[file].nspname,
		yezzey_vfd_cache[file].relname,
		storage_host /*host*/,
		storage_bucket/*bucket*/,
		storage_prefix/*prefix*/,
		yezzey_vfd_cache[file].filepath, 
		GpIdentity.segindex,
		1 /*because modcount will increase on write*/ + yezzey_vfd_cache[file].modcount);

	elog(yezzey_ao_log_level, "prepared writer handle for modcount %ld", yezzey_vfd_cache[file].modcount);
	if (yezzey_vfd_cache[file].whandle  == NULL) {
		return -1;
	}

	Assert(yezzey_vfd_cache[file].whandle != NULL);


#ifdef CACHE_LOCAL_WRITES_FEATURE
	StringInfoData localTmpPath;
	initStringInfo(&localTmpPath);
	if (yezzey_vfd_cache[file].localTmpVfd) return;

	appendStringInfo(&localTmpPath, "%s_tmpbuf", yezzey_vfd_cache[file].filepath);

	elog(yezzey_ao_log_level, "creating proxy file for write %s", localTmpPath.data);

	yezzey_vfd_cache[file].localTmpVfd = 
	PathNameOpenFile(localTmpPath.data, yezzey_vfd_cache[file].fileFlags | O_CREAT, yezzey_vfd_cache[file].fileMode);
	if (yezzey_vfd_cache[file].localTmpVfd <= 0) {
		elog(yezzey_ao_log_level, "error creating proxy file for write %s: %d", localTmpPath.data, errno);
	}
	FileSeek(yezzey_vfd_cache[file].localTmpVfd, 0, SEEK_END);
#endif

	return 0;
}

File virtualEnsure(SMGRFile file) {
	File internal_vfd;
	if (yezzey_vfd_cache[file].y_vfd == YEZZEY_VANANT_VFD) {
		elog(ERROR, "attempt to ensure locality of not opened file");
	}	
	if (yezzey_vfd_cache[file].y_vfd == YEZZEY_NOT_OPENED) {
		// not opened yet
		if (!ensureFilepathLocal(yezzey_vfd_cache[file].filepath)) {
			// do s3 read
			return s3ext;
		}

		/* Do we need this? */

		internal_vfd = PathNameOpenFile(yezzey_vfd_cache[file].filepath,
		yezzey_vfd_cache[file].fileFlags, yezzey_vfd_cache[file].fileMode);

		elog(
			yezzey_ao_log_level, 
			"virtualEnsure: yezzey virtual file descriptor for file %s become %d", 
			yezzey_vfd_cache[file].filepath, 
			internal_vfd);
		
		if (internal_vfd == -1) {
			// error
			elog(ERROR, "virtualEnsure: failed to proxy open file %s for fd %d", yezzey_vfd_cache[file].filepath, file);
		}
		elog(yezzey_ao_log_level, "y vfd become %d", internal_vfd);

		yezzey_vfd_cache[file].y_vfd = internal_vfd; // -1 is ok

		elog(yezzey_ao_log_level, "virtualEnsure: file %s yezzey descriptor become %d", yezzey_vfd_cache[file].filepath, file);
		/* allocate handle struct */
	}

	return yezzey_vfd_cache[file].y_vfd;
}

int64 yezzey_NonVirtualCurSeek(SMGRFile file) {
	File actual_fd = virtualEnsure(file);
	if (actual_fd == s3ext) {
		elog(yezzey_ao_log_level, 
			"yezzey_NonVirtualCurSeek: non virt file seek with yezzey fd %d and actual file in external storage, responding %ld", 
			file,
			yezzey_vfd_cache[file].offset);
		return yezzey_vfd_cache[file].offset;
	}
	elog(yezzey_ao_log_level, 
		"yezzey_NonVirtualCurSeek: non virt file seek with yezzey fd %d and actual %d", 
		file, 
		actual_fd);
	return FileNonVirtualCurSeek(actual_fd);
}


int64 yezzey_FileSeek(SMGRFile file, int64 offset, int whence) {
	File actual_fd = virtualEnsure(file);
	if (actual_fd == s3ext) {
		// what?
		yezzey_vfd_cache[file].offset = offset;
		return offset; 
	}
	elog(yezzey_ao_log_level, "yezzey_FileSeek: file seek with yezzey fd %d offset %ld actual %d", file, offset, actual_fd);
	return FileSeek(actual_fd, offset, whence);
}

int	yezzey_FileSync(SMGRFile file) {
	File actual_fd = virtualEnsure(file);
	if (actual_fd == s3ext) {
		/* s3 always sync ? */
		/* sync tmp buf file here */
		return 0;
	}
	elog(yezzey_ao_log_level, "file sync with fd %d actual %d", file, actual_fd);
	return FileSync(actual_fd);
}

SMGRFile yezzey_AORelOpenSegFile(char *nspname, char * relname, FileName fileName, int fileFlags, int fileMode, int64 modcount) {
	int yezzey_fd;
	elog(yezzey_ao_log_level, "yezzey_AORelOpenSegFile: path name open file %s", fileName);

	/* lookup for virtual file desc entry */
	for (yezzey_fd = YEZZEY_NOT_OPENED + 1; yezzey_fd < MAXVFD; ++yezzey_fd) {
		if (yezzey_vfd_cache[yezzey_fd].y_vfd == YEZZEY_VANANT_VFD) {
			yezzey_vfd_cache[yezzey_fd].filepath = pstrdup(fileName);
			if (relname == NULL) {
				/* Should be possible only in recovery */
				Assert(RecoveryInProgress());
			} else {
				yezzey_vfd_cache[yezzey_fd].relname = pstrdup(relname);
			}
			if (nspname == NULL) {
				/* Should be possible only in recovery */
				Assert(RecoveryInProgress());
			} else {
				yezzey_vfd_cache[yezzey_fd].nspname = pstrdup(nspname);
			}
			yezzey_vfd_cache[yezzey_fd].fileFlags = fileFlags;
			yezzey_vfd_cache[yezzey_fd].fileMode = fileMode;
			yezzey_vfd_cache[yezzey_fd].modcount = modcount;
			if (yezzey_vfd_cache[yezzey_fd].filepath == NULL || 
			(!RecoveryInProgress() && yezzey_vfd_cache[yezzey_fd].relname == NULL)) {
				elog(ERROR, "out of memory");
			}

			yezzey_vfd_cache[yezzey_fd].y_vfd = YEZZEY_NOT_OPENED;

			/* we dont need to interact with s3 while in recovery*/

			if (RecoveryInProgress()) {
				/* replicae */ 
				return yezzey_fd;
			} else {
				/* primary */
				if (!ensureFilepathLocal(yezzey_vfd_cache[yezzey_fd].filepath)) {
					switch (fileFlags) {
						case O_WRONLY:
							/* allocate handle struct */						
							if (writeprepare(yezzey_fd) == -1) {
								return -1;
							}
							break;
						case O_RDONLY:
							/* allocate handle struct */
							if (readprepare(yezzey_fd) == -1) {
								return -1;
							}
							break;
						case O_RDWR:
							if (writeprepare(yezzey_fd) == -1) {
								return -1;
							}
							break;
						default:
							break;
						/* raise error */
					}
					// do s3 read
				}
				return yezzey_fd;
			}
		}
	}
/* no match*/
	return -1;
}

void yezzey_FileClose(SMGRFile file) {
	File actual_fd = virtualEnsure(file);
	elog(yezzey_ao_log_level, "file close with %d actual %d", file, actual_fd);
	if (actual_fd == s3ext) {
		yezzey_complete_r_transfer_data(&yezzey_vfd_cache[file].rhandle);
		yezzey_complete_w_transfer_data(&yezzey_vfd_cache[file].whandle);
	} else {
		FileClose(actual_fd);
	}

#ifdef DISKCACHE
	if (yezzey_vfd_cache[file].localTmpVfd > 0) {
		FileClose(yezzey_vfd_cache[file].localTmpVfd);
	}
#endif
	if (yezzey_vfd_cache[file].filepath) {
		pfree(yezzey_vfd_cache[file].filepath);
	}
	if (yezzey_vfd_cache[file].relname) {
		pfree(yezzey_vfd_cache[file].relname);
	}
	if (yezzey_vfd_cache[file].nspname) {
		pfree(yezzey_vfd_cache[file].nspname);
	}
	memset(&yezzey_vfd_cache[file], 0, sizeof(yezzey_vfd));
}

#define ALLOW_MODIFY_EXTERNAL_TABLE

int yezzey_FileWrite(SMGRFile file, char *buffer, int amount) {
	File actual_fd = virtualEnsure(file);
	int rc;
	if (actual_fd == s3ext) {		

		/* Assert here we are not in crash or regular recovery
		* If yes, simply skip this call as data is already 
		* persisted in external storage
		 */
		if (RecoveryInProgress()) {
			/* Should we return $amount or min (virtualSize - currentLogicalEof, amount) ? */
			return amount;
		}

		if (yezzey_vfd_cache[file].whandle == NULL) {
			elog(yezzey_ao_log_level, "read from external storage while read handler uninitialized");
			return -1;
		}
#ifdef ALLOW_MODIFY_EXTERNAL_TABLE
#ifdef CACHE_LOCAL_WRITES_FEATURE
		/*local writes*/
		/* perform direct write to external storage */
		rc = FileWrite(yezzey_vfd_cache[file].localTmpVfd, buffer, amount);
		if (rc > 0) {
			yezzey_vfd_cache[file].offset += rc;
		}
		return rc;
#endif
#else
		elog(ERROR, "external table modifications are not supported yet");
#endif
		rc = amount;
		if (!yezzey_writer_transfer_data(yezzey_vfd_cache[file].whandle, buffer, &rc)) {
			elog(WARNING, "failed to write to external storage");
			return -1;
		}
		elog(yezzey_ao_log_level, "yezzey_FileWrite: write %d bytes, %d transfered, yezzey fd %d", amount, rc, file);
		yezzey_vfd_cache[file].offset += rc;
		return rc;
	}
	elog(yezzey_ao_log_level, "file write with %d, actual %d", file, actual_fd);
	rc = FileWrite(actual_fd, buffer, amount);
	if (rc > 0) {
		yezzey_vfd_cache[file].offset += rc;
	}
	return rc;
}

int yezzey_FileRead(SMGRFile file, char *buffer, int amount) {
	File actual_fd = virtualEnsure(file);
	int curr = amount;

	if (actual_fd == s3ext) {
		if (yezzey_vfd_cache[file].rhandle == NULL) {
			elog(yezzey_ao_log_level, "read from external storage while read handler uninitialized");
			return -1;
		}
		if (yezzey_reader_empty(yezzey_vfd_cache[file].rhandle)) {
			if (yezzey_vfd_cache[file].localTmpVfd <= 0) {
				return 0;
			}
#ifdef DISKCACHE
			/* tring to read proxy file */
			curr = FileRead(yezzey_vfd_cache[file].localTmpVfd, buffer, amount);
			/* fall throught */
			elog(yezzey_ao_log_level, "read from proxy file %d", curr);
#endif
		} else {
			if (!yezzey_reader_transfer_data(yezzey_vfd_cache[file].rhandle, buffer, &curr)) {
				elog(yezzey_ao_log_level, "problem while direct read from s3 read with %d curr: %d", file, curr);
				return -1;
			}
#ifdef DISKCACHE
			if (yezzey_reader_empty(yezzey_vfd_cache[file].rhandle)) {
				if (yezzey_vfd_cache[file].localTmpVfd <= 0) {
					return 0;
				}
				/* tring to read proxy file */
				curr = FileRead(yezzey_vfd_cache[file].localTmpVfd, buffer, amount);
				/* fall throught */

				elog(yezzey_ao_log_level, "read from proxy file %d", curr);
			}
#endif
		}

		yezzey_vfd_cache[file].offset += curr;

		elog(yezzey_ao_log_level, "file read with %d, actual %d, amount %d real %d", file, actual_fd, amount, curr);
		return curr;
	}
	return FileRead(actual_fd, buffer, amount);
}

int yezzey_FileTruncate(SMGRFile file, int offset) {
	File actual_fd = virtualEnsure(file);
	if (actual_fd == s3ext) {
		/* Leave external storage file untouched 
		* We may need them for point-in-time recovery
		* later. We will face no issues with writing to
		* this AO/AOCS table later, because truncate operation
		* with AO/AOCS table changes relfilenode OID of realtion
		* segments.  
		*/
		return 0;
	}
	return FileTruncate(actual_fd, offset);
}
