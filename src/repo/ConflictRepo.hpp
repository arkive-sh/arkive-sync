#pragma once

#include <sqlite3.h>
#include <string>

class ConflictRepo {
public:
  explicit ConflictRepo(sqlite3 *db);

  void markConflict(const std::string &entryId, const std::string &state,
                    const std::string &reason);

private:
  sqlite3 *db_;
};
