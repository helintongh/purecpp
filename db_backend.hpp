#pragma once

#include <string_view>

#include <ormpp/connection_pool.hpp>
#include <ormpp/dbng.hpp>

#if defined(PURECPP_DB_SQLITE)
#include <ormpp/sqlite.hpp>
#elif defined(PURECPP_DB_MYSQL)
#include <ormpp/mysql.hpp>
#elif defined(PURECPP_DB_POSTGRESQL)
#include <ormpp/postgresql.hpp>
#else
#error "No database backend selected. Define one of PURECPP_DB_SQLITE, PURECPP_DB_MYSQL, or PURECPP_DB_POSTGRESQL."
#endif

namespace purecpp {

#if defined(PURECPP_DB_SQLITE)
using database_backend = ormpp::sqlite;
inline constexpr std::string_view database_backend_name() { return "sqlite"; }
#elif defined(PURECPP_DB_MYSQL)
using database_backend = ormpp::mysql;
inline constexpr std::string_view database_backend_name() { return "mysql"; }
#elif defined(PURECPP_DB_POSTGRESQL)
using database_backend = ormpp::postgresql;
inline constexpr std::string_view database_backend_name() {
  return "postgresql";
}
#endif

using database_client = ormpp::dbng<database_backend>;
using database_pool = ormpp::connection_pool<database_client>;

inline database_pool &get_db_pool() { return database_pool::instance(); }

} // namespace purecpp
