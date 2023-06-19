
#include "virtual_index.h"

#include "algorithm"

Oid YezzeyFindAuxIndex_internal(Oid reloid);

Oid YezzeyCreateAuxIndex(Relation aorel) {
  {
    auto tmp = YezzeyFindAuxIndex_internal(RelationGetRelid(aorel));
    if (OidIsValid(tmp)) {
      return tmp;
    }
  }

  ObjectAddress baseobject;
  ObjectAddress yezzey_ao_auxiliaryobject;

  auto tupdesc = CreateTemplateTupleDesc(Natts_yezzey_virtual_index, false);

  TupleDescInitEntry(tupdesc, (AttrNumber)Anum_yezzey_virtual_index_segno,
                     "segno", INT4OID, -1, 0);
  TupleDescInitEntry(tupdesc, (AttrNumber)Anum_yezzey_virtual_start_off,
                     "offset_start", INT8OID, -1, 0);
  TupleDescInitEntry(tupdesc, (AttrNumber)Anum_yezzey_virtual_finish_off,
                     "offset_finish", INT8OID, -1, 0);
  TupleDescInitEntry(tupdesc, (AttrNumber)Anum_yezzey_virtual_modcount,
                     "modcount", INT8OID, -1, 0);
  TupleDescInitEntry(tupdesc, (AttrNumber)Anum_yezzey_virtual_ext_path,
                     "external_path", TEXTOID, -1, 0);

  auto yezzey_ao_auxiliary_relname = std::string("yezzey_virtual_index") +
                                     std::to_string(RelationGetRelid(aorel));

  auto yezzey_ao_auxiliary_relid = heap_create_with_catalog(
      yezzey_ao_auxiliary_relname.c_str() /* relname */,
      YEZZEY_AUX_NAMESPACE /* namespace */, 0 /* tablespace */,
      GetNewObjectId() /* relid */, GetNewObjectId() /* reltype oid */,
      InvalidOid /* reloftypeid */, aorel->rd_rel->relowner /* owner */,
      tupdesc /* rel tuple */, NIL, InvalidOid /* relam */, RELKIND_YEZZEYINDEX,
      aorel->rd_rel->relpersistence, RELSTORAGE_HEAP,
      aorel->rd_rel->relisshared, RelationIsMapped(aorel), true, 0,
      ONCOMMIT_NOOP, NULL /* GP Policy */, (Datum)0, false /* use_user_acl */,
      true, true, false /* valid_opts */, false /* is_part_child */,
      false /* is part parent */);

  /* Make this table visible, else yezzey virtual index creation will fail */
  CommandCounterIncrement();

  /*
   * Register dependency from the auxiliary table to the master, so that the
   * aoseg table will be deleted if the master is.
   */
  baseobject.classId = RelationRelationId;
  baseobject.objectId = RelationGetRelid(aorel);
  baseobject.objectSubId = 0;
  yezzey_ao_auxiliaryobject.classId = RelationRelationId;
  yezzey_ao_auxiliaryobject.objectId = yezzey_ao_auxiliary_relid;
  yezzey_ao_auxiliaryobject.objectSubId = 0;

  recordDependencyOn(&yezzey_ao_auxiliaryobject, &baseobject,
                     DEPENDENCY_INTERNAL);

  /*
   * Make changes visible
   */
  CommandCounterIncrement();

  return yezzey_ao_auxiliary_relid;
}

Oid YezzeyFindAuxIndex_internal(Oid reloid) {
  HeapTuple tup;
  ScanKeyData skey[2];

  auto yezzey_virtual_index_oid = InvalidOid;

  auto yezzey_ao_auxiliary_relname =
      std::string("yezzey_virtual_index") + std::to_string(reloid);

  /*
   * Check the pg_appendonly relation to be certain the ao table
   * is there.
   */
  auto pg_class = heap_open(RelationRelationId, AccessShareLock);

  ScanKeyInit(&skey[0], Anum_pg_class_relname, BTEqualStrategyNumber, F_NAMEEQ,
              CStringGetDatum(yezzey_ao_auxiliary_relname.c_str()));

  ScanKeyInit(&skey[1], Anum_pg_class_relnamespace, BTEqualStrategyNumber,
              F_OIDEQ, ObjectIdGetDatum(YEZZEY_AUX_NAMESPACE));

  auto scan =
      systable_beginscan(pg_class, ClassNameNspIndexId, true, NULL, 2, skey);

  if (HeapTupleIsValid(tup = systable_getnext(scan))) {
    yezzey_virtual_index_oid = HeapTupleGetOid(tup);
  }

  systable_endscan(scan);
  heap_close(pg_class, AccessShareLock);

  return yezzey_virtual_index_oid;
}

Oid YezzeyFindAuxIndex(Oid reloid) {
  Oid yezzey_virtual_index_oid = YezzeyFindAuxIndex_internal(reloid);
  if (OidIsValid(yezzey_virtual_index_oid)) {
    return yezzey_virtual_index_oid;
  }
  elog(ERROR, "could not find yezzey virtual index oid for relation \"%d\"",
       reloid);
}

void emptyYezzeyIndex(Oid yezzey_index_oid) {
  HeapTuple tuple;

  /* DELETE FROM yezzey.yezzey_virtual_index_<oid> */
  auto rel = heap_open(yezzey_index_oid, RowExclusiveLock);

  auto snap = RegisterSnapshot(GetTransactionSnapshot());

  auto desc = heap_beginscan(rel, snap, 0, NULL);

  while (HeapTupleIsValid(tuple = heap_getnext(desc, ForwardScanDirection))) {
    simple_heap_delete(rel, &tuple->t_self);
  }

  heap_endscan(desc);
  heap_close(rel, RowExclusiveLock);

  UnregisterSnapshot(snap);

  /* make changes visible*/
  CommandCounterIncrement();
} /* end emptyYezzeyIndex */

void emptyYezzeyIndexBlkno(Oid yezzey_index_oid, int blkno) {
  HeapTuple tuple;
  ScanKeyData skey[1];

  /* DELETE FROM yezzey.yezzey_virtual_index_<oid> WHERE segno = <blkno> */
  auto rel = heap_open(yezzey_index_oid, RowExclusiveLock);

  auto snap = RegisterSnapshot(GetTransactionSnapshot());

  ScanKeyInit(&skey[0], Anum_yezzey_virtual_index_segno, BTEqualStrategyNumber,
              F_INT4EQ, Int32GetDatum(blkno));

  auto desc = heap_beginscan(rel, snap, 1, skey);

  while (HeapTupleIsValid(tuple = heap_getnext(desc, ForwardScanDirection))) {
    simple_heap_delete(rel, &tuple->t_self);
  }

  heap_endscan(desc);
  heap_close(rel, RowExclusiveLock);

  UnregisterSnapshot(snap);

  /* make changes visible*/
  CommandCounterIncrement();
} /* end emptyYezzeyIndexBlkno */

void YezzeyVirtualIndexInsert(Oid yandexoid /*yezzey auxiliary index oid*/,
                              int64_t segindx, int64_t modcount,
                              const std::string &ext_path) {
  bool nulls[Natts_yezzey_virtual_index];
  Datum values[Natts_yezzey_virtual_index];

  memset(nulls, 0, sizeof(nulls));
  memset(values, 0, sizeof(values));

  /* INSERT INTO  yezzey.yezzey_virtual_index_<oid> VALUES(segno, start_offset,
   * 0, modcount, external_path) */

  auto yandxrel = heap_open(yandexoid, RowExclusiveLock);

  values[Anum_yezzey_virtual_index_segno - 1] = Int64GetDatum(segindx);
  values[Anum_yezzey_virtual_start_off - 1] = Int64GetDatum(0);
  values[Anum_yezzey_virtual_finish_off - 1] = Int64GetDatum(0);
  values[Anum_yezzey_virtual_modcount - 1] = Int64GetDatum(modcount);
  values[Anum_yezzey_virtual_ext_path - 1] =
      CStringGetTextDatum(ext_path.c_str());

  auto yandxtuple = heap_form_tuple(RelationGetDescr(yandxrel), values, nulls);

  simple_heap_insert(yandxrel, yandxtuple);
  CatalogUpdateIndexes(yandxrel, yandxtuple);

  heap_freetuple(yandxtuple);
  heap_close(yandxrel, RowExclusiveLock);

  CommandCounterIncrement();
}

std::vector<std::string>
YezzeyVirtualGetOrder(Oid yandexoid /*yezzey auxiliary index oid*/, int blkno) {

  /* SELECT external_path FROM yezzey.yezzey_virtual_index_<oid> WHERE segno =
   * <>; */
  HeapTuple tuple;
  ScanKeyData skey[1];

  std::vector<std::pair<int64_t, std::string>> tmp;

  auto rel = heap_open(yandexoid, RowExclusiveLock);

  auto snap = RegisterSnapshot(GetTransactionSnapshot());

  ScanKeyInit(&skey[0], Anum_yezzey_virtual_index_segno, BTEqualStrategyNumber,
              F_INT4EQ, Int32GetDatum(blkno));

  /* TBD: Read index  */
  auto desc = heap_beginscan(rel, snap, 1, skey);

  while (HeapTupleIsValid(tuple = heap_getnext(desc, ForwardScanDirection))) {
    auto ytup = ((FormData_yezzey_virtual_index *)GETSTRUCT(tuple));
    tmp.push_back({ytup->modcount, std::string(ytup->ext_path)});
  }

  heap_endscan(desc);
  heap_close(rel, RowExclusiveLock);

  UnregisterSnapshot(snap);

  /* make changes visible*/
  CommandCounterIncrement();

  /* sort by modcount - they are unic */
  std::sort(tmp.begin(), tmp.end());

  std::vector<std::string> res(tmp.size());
  for (auto el : tmp) {
    res.push_back(el.second);
  }

  return std::move(res);
}