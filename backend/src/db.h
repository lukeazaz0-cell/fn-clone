// Thin, thread-safe SQLite wrapper.
#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct sqlite3;

namespace backend {

using DbValue = std::variant<std::nullptr_t, int64_t, double, std::string>;
using Row = std::map<std::string, DbValue>;

std::string asString(const DbValue& v);
int64_t asInt(const DbValue& v);
double asDouble(const DbValue& v);

class Db {
public:
    ~Db();
    bool open(const std::string& path);
    void close();
    // Executes a statement with positional parameters (?1, ?2 ... or ?).
    bool exec(const std::string& sql, const std::vector<DbValue>& params = {}, int64_t* lastInsertId = nullptr, int* changes = nullptr);
    std::vector<Row> query(const std::string& sql, const std::vector<DbValue>& params = {});
    std::optional<Row> one(const std::string& sql, const std::vector<DbValue>& params = {});
    std::string lastError() const { return lastError_; }
    std::recursive_mutex& mutex() { return mu_; }

private:
    sqlite3* db_ = nullptr;
    std::recursive_mutex mu_;
    std::string lastError_;
};

} // namespace backend
