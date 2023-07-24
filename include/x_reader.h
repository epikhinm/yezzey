#pragma once

#include "io_adv.h"
#include "ylister.h"
#include "yreader.h"
#include <memory>


#include "chunkinfo.h"

#include "gpreader.h"

class ExternalReader : public YReader, public YLister {
public:
  friend class ExternalWriter;
  explicit ExternalReader(std::shared_ptr<IOadv> adv,
                          const std::vector<ChunkInfo> &order,
                          ssize_t segindx);
public:
  virtual ~ExternalReader();

  virtual bool close();
  virtual bool read(char *buffer, size_t *amount);
  virtual std::vector<storageChunkMeta> list_relation_chunks();
  virtual std::vector<std::string> list_chunk_names();
  virtual void BumpArenda(size_t count);

public:
  GPReader * createReaderHandle(const char* x_path);

  bool empty();

  GPReader *reader_{nullptr};

protected:
  bool cleaned_{false};

private:
  std::shared_ptr<IOadv> adv_;
  const std::vector<ChunkInfo> order_;
  ssize_t order_ptr_;
  ssize_t segindx_;
};