#pragma once

#include <soci/session.h>
#include <soci/sqlite3/soci-sqlite3.h>

namespace xrpl::detail {

/**
 * Returns the sqlite connection underlying a soci session.
 *
 * @return The connection, or nullptr if the session is not backed by sqlite.
 */
inline sqlite_api::sqlite3*
getConnection(soci::session& s)
{
    if (auto const* const b = dynamic_cast<soci::sqlite3_session_backend*>(s.get_backend()))
        return b->conn_;
    return nullptr;
}

}  // namespace xrpl::detail
