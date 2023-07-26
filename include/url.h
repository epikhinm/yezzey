
#include "io_adv.h"
#include "util.h"
#include "ylister.h"

std::string craftUrl(const std::shared_ptr<IOadv> &adv, ssize_t segindx,
                     ssize_t modcount, XLogRecPtr current_recptr);

std::string craftUrlXpath(const std::shared_ptr<IOadv> &adv, ssize_t segindx,
                     ssize_t modcount, XLogRecPtr current_recptr);

std::string craftStoragePath(const std::shared_ptr<IOadv> &adv, ssize_t segindx,
                             ssize_t modcount, const std::string &prefix, XLogRecPtr	current_recptr);

std::string craftWalgStoragePath(const std::shared_ptr<IOadv> &adv,
                                 ssize_t segindx, ssize_t modcount);