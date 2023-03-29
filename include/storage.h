

#ifndef EXTERNAL_STORAGE_H
#define EXTERNAL_STORAGE_H

// XXX: todo proder interface for external storage offloading

#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "stddef.h"

#include "postgres.h"

#include "access/aosegfiles.h"
#include "access/htup_details.h"
#include "utils/tqual.h"

// For GpIdentity
#include "cdb/cdbvars.h"

#ifdef __cplusplus
}
#endif

#include "gucs.h"

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

typedef struct yezzeyChunkMeta {
  const char *chunkName;
  size_t chunkSize;
} yezzeyChunkMeta;

#ifdef __cplusplus
#include "string"

#include "types.h"

std::string getlocalpath(Oid dbnode, Oid relNode, int segno);
bool ensureFilepathLocal(const std::string &filepath);
std::string getlocalpath(const relnodeCoord &coords);
#endif

EXTERNC int offloadRelationSegment(Relation aorel, int segno, int64 modcount,
                                   int64 logicalEof,
                                   const char *external_storage_path);

EXTERNC int loadRelationSegment(Relation aorel, Oid origrelfilenode, int segno,
                                const char *dest_path);

EXTERNC bool ensureFileLocal(RelFileNode rnode, BackendId backend,
                             ForkNumber forkNum, BlockNumber blkno);

EXTERNC int statRelationSpaceUsage(Relation aorel, int segno, int64 modcount,
                                   int64 logicalEof, size_t *local_bytes,
                                   size_t *local_commited_bytes,
                                   size_t *external_bytes);

EXTERNC int statRelationSpaceUsagePerExternalChunk(
    Relation aorel, int segno, int64 modcount, int64 logicalEof,
    size_t *local_bytes, size_t *local_commited_bytes, yezzeyChunkMeta **list,
    size_t *cnt_chunks);

#endif /* EXTERNAL_STORAGE */