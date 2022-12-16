

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

#include "external_storage.h"



int yezzey_log_level = INFO;


/*
* This function used by AO-related realtion functions
*/
bool
ensureFilepathLocal(char *filepath)
{
	int fd, cp, errno_;

	fd = open(filepath, O_RDONLY);
	errno_ = errno;

	cp = fd;
	elog(yezzey_log_level, "[YEZZEY_SMGR] trying to open %s, result - %d, %d", filepath, fd, errno_);
	close(fd);

	return cp >= 0;
}

int
offloadFileToExternalStorage(const char *localPath)
{
	StringInfoData s3Path;
	initStringInfo(&s3Path);
	int rc;
	char *cd;
	
    appendStringInfoString(&s3Path, s3_prefix);
	
	elog(INFO, "[YEZZEY_SMGR_BG] s3 prefix is  \"%s\"", s3_prefix);
    appendStringInfoString(&s3Path, localPath);

	cd = buildExternalStorageCommand(s3_putter, localPath, s3Path.data);
	rc = system(cd);
	
	elog(INFO, "[YEZZEY_SMGR_BG] tried \"%s\", got %d", cd, rc);
	
	pfree(s3Path.data);
	pfree(cd);

    return rc;
}

bool
ensureFileLocal(RelFileNode rnode, BackendId backend, ForkNumber forkNum, BlockNumber blkno)
{	
	// XXX: not used by AOseg logic
	// if (IsCrashRecoveryOnly()) {
	// 	/* MDB-19689: do not consult catalog 
	// 		if crash recovery is in progress */

	// 	elog(yezzey_log_level, "[YEZZEY_SMGR]: skip ensuring while crash recovery");
	// 	return true;
	// }

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
	elog(INFO, "[YEZZEY_SMGR_BG] tried to remove local file \"%s\", result: %d", localPath, rc);
	return rc;
}

int
offloadRelationSegment(RelFileNode rnode, int segno) {
    StringInfoData path;
	int rc;

    if (segno == 0) {
        /* should never happen */
        return 0;
    }

    initStringInfo(&path);
    appendStringInfo(&path, "base/%d/%d.%d", rnode.dbNode, rnode.relNode, segno);

    elog(INFO, "contructed path %s", path.data);
    if (!ensureFilepathLocal(path.data)) {
        // nothing to do
        return 0;
    }

    if ((rc = offloadFileToExternalStorage(path.data)) < 0) {
        pfree(path.data);
        return rc;
    }

    if ((rc = removeLocalFile(path.data)) < 0) {
        elog(INFO, "errno while remove %d", errno);
        pfree(path.data);
        return rc;
    }

    pfree(path.data);
    return 0;
}


int
loadRelationSegment(RelFileNode rnode, int segno) {
    StringInfoData path;
	int rc;

    if (segno == 0) {
        /* should never happen */
        return 0;
    }

    initStringInfo(&path);
    appendStringInfo(&path, "base/%d/%d.%d", rnode.dbNode, rnode.relNode, segno);

    elog(INFO, "contructed path %s", path.data);
    if (ensureFilepathLocal(path.data)) {
        // nothing to do
        return 0;
    }


    if ((rc = getFilepathFromS3(path.data)) < 0) {
        pfree(path.data);
        return rc;
    }

    pfree(path.data);
    return 0;
}

int
getFilepathFromS3(const char *filepath)
{
	StringInfoData s3Path;
	initStringInfo(&s3Path);
	char *cd;
	int rc;

	appendStringInfoString(&s3Path, s3_prefix);
	appendStringInfoString(&s3Path, filepath);
	appendStringInfoString(&s3Path, ".br");

	elog(INFO, "[YEZZEY_SMGR] fetching %s from %s using %s", filepath, s3Path.data, s3_getter);

	cd = buildExternalStorageCommand(s3_getter, filepath, s3Path.data);
	rc = system(cd);

	elog(INFO, "[YEZZEY_SMGR] tried \"%s\", got %d", cd, rc);
	pfree(cd);

	pfree(s3Path.data);
	
	elog(INFO, "[YEZZEY_SMGR] loading %s, retcode %d", filepath, rc);

	return rc;
}

char *
buildExternalStorageCommand(const char *s3Command, const char *localPath, const char *externalPath)
{
	StringInfoData result;
	const char *sp;

	initStringInfo(&result);

	for (sp = s3Command; *sp; sp++)
	{
		if (*sp == '%')
		{
			switch (sp[1])
			{
				case 'f':
					{
						sp++;
						appendStringInfoString(&result, localPath);
						break;
					}
				case 's':
					{
						sp++;
						appendStringInfoString(&result, externalPath);
						break;
					}
				case '%':
					{
						sp++;
						appendStringInfoChar(&result, *sp);
						break;
					}
				default:
					{
						appendStringInfoChar(&result, *sp);
						break;
					}
			}
		}
		else
		{
			appendStringInfoChar(&result, *sp);
		}
	}

	return result.data;
}
