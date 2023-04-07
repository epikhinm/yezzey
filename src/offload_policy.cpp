#include "offload_policy.h"

PG_FUNCTION_INFO_V1(yezzey_set_relation_expirity_seg);

/*

CREATE TABLE yezzey.offload_metadata(
    reloid           OID,
    relpolicy        offload_policy INT --NOT NULL,
    relext_time      TIMESTAMP,
    rellast_archived TIMESTAMP
)
DISTRIBUTED REPLICATED;
*/

const char *offload_metadata_relname = "offload_metadata";

void YezzeyOffloadPolicyRelation() {
  { /* check existed, if no, return */
  }

  Oid yezzey_offload_metadata_relid;
  TupleDesc tupdesc;

  ObjectAddress baseobject;
  ObjectAddress yezzey_ao_auxiliaryobject;

  tupdesc = CreateTemplateTupleDesc(Natts_offload_metadata, false);

  TupleDescInitEntry(tupdesc, (AttrNumber)Anum_offload_metadata_reloid,
                     "reloid", OIDOID, -1, 0);
  TupleDescInitEntry(tupdesc, (AttrNumber)Anum_offload_metadata_relpolicy,
                     "relpolicy", INT8OID, -1, 0);
  TupleDescInitEntry(tupdesc, (AttrNumber)Anum_offload_metadata_relext_time,
                     "relext_time", TIMESTAMPOID, -1, 0);
  TupleDescInitEntry(tupdesc,
                     (AttrNumber)Anum_offload_metadata_rellast_archived,
                     "rellast_archived", TIMESTAMPOID, -1, 0);

  yezzey_offload_metadata_relid = heap_create_with_catalog(
      offload_metadata_relname /* relname */,
      YEZZEY_AUX_NAMESPACE /* namespace */, 0 /* tablespace */,
      YEZZEY_OFFLOAD_POLICY_RELATION /* relid */,
      GetNewObjectId() /* reltype oid */, InvalidOid /* reloftypeid */,
      GetUserId() /* owner */, tupdesc /* rel tuple */, NIL,
      InvalidOid /* relam */, RELKIND_RELATION, RELPERSISTENCE_PERMANENT,
      RELSTORAGE_HEAP, false, false, true, 0, ONCOMMIT_NOOP,
      NULL /* GP Policy */, (Datum)0, false /* use_user_acl */, true, true,
      false /* valid_opts */, false /* is_part_child */,
      false /* is part parent */);

  /* Make this table visible, else yezzey virtual index creation will fail */
  CommandCounterIncrement();

  /*
   * Register dependency from the auxiliary table to the master, so that the
   * aoseg table will be deleted if the master is.
   */
  baseobject.classId = ExtensionRelationId;
  baseobject.objectId = get_extension_oid("yezzey", false);
  baseobject.objectSubId = 0;
  yezzey_ao_auxiliaryobject.classId = RelationRelationId;
  yezzey_ao_auxiliaryobject.objectId = YEZZEY_OFFLOAD_POLICY_RELATION;
  yezzey_ao_auxiliaryobject.objectSubId = 0;

  recordDependencyOn(&yezzey_ao_auxiliaryobject, &baseobject,
                     DEPENDENCY_INTERNAL);

  /*
   * Make changes visible
   */
  CommandCounterIncrement();
}

void YezzeyOffloadPolicyInsert(Oid reloid /* offload relation oid */,
                               int offload_policy) {
  HeapTuple offtup;
  Relation offrel;

  bool nulls[Natts_offload_metadata];
  Datum values[Natts_offload_metadata];

  memset(nulls, 0, sizeof(nulls));
  memset(values, 0, sizeof(values));

  offrel = heap_open(YEZZEY_OFFLOAD_POLICY_RELATION, RowExclusiveLock);

  /* INSERT INTO yezzey.offload_metadata VALUES(v_reloid, 1, NULL, NOW()); */

  values[Anum_offload_metadata_reloid - 1] = reloid;
  values[Anum_offload_metadata_relpolicy - 1] = offload_policy;
  values[Anum_offload_metadata_relext_time - 1] =
      TimestampTzGetDatum(GetCurrentTimestamp());
  values[Anum_offload_metadata_rellast_archived - 1] =
      TimestampTzGetDatum(GetCurrentTimestamp());

  offtup = heap_form_tuple(RelationGetDescr(offrel), values, nulls);

  simple_heap_insert(offrel, offtup);
  CatalogUpdateIndexes(offrel, offtup);

  heap_freetuple(offtup);
  heap_close(offrel, RowExclusiveLock);

  /* make changes visible */
  CommandCounterIncrement();
}


Datum yezzey_set_relation_expirity_seg(PG_FUNCTION_ARGS) {

}