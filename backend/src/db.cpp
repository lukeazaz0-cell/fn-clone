#include "db.h"

#include <cstdio>

#include "sqlite3.h"

namespace backend {

std::string asString(const DbValue& v) {
    if (auto s = std::get_if<std::string>(&v)) return *s;
    if (auto i = std::get_if<int64_t>(&v)) return std::to_string(*i);
    if (auto d = std::get_if<double>(&v)) return std::to_string(*d);
    return "";
}
int64_t asInt(const DbValue& v) {
    if (auto i = std::get_if<int64_t>(&v)) return *i;
    if (auto d = std::get_if<double>(&v)) return (int64_t)*d;
    if (auto s = std::get_if<std::string>(&v)) return std::atoll(s->c_str());
    return 0;
}
double asDouble(const DbValue& v) {
    if (auto d = std::get_if<double>(&v)) return *d;
    if (auto i = std::get_if<int64_t>(&v)) return (double)*i;
    if (auto s = std::get_if<std::string>(&v)) return std::atof(s->c_str());
    return 0;
}

Db::~Db() { close(); }

bool Db::open(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA foreign_keys=ON");
    return true;
}

void Db::close() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
}

static bool bindAll(sqlite3_stmt* st, const std::vector<DbValue>& params) {
    for (size_t i = 0; i < params.size(); i++) {
        int idx = (int)i + 1;
        const DbValue& v = params[i];
        int rc;
        if (std::holds_alternative<std::nullptr_t>(v)) rc = sqlite3_bind_null(st, idx);
        else if (auto x = std::get_if<int64_t>(&v)) rc = sqlite3_bind_int64(st, idx, *x);
        else if (auto d = std::get_if<double>(&v)) rc = sqlite3_bind_double(st, idx, *d);
        else rc = sqlite3_bind_text(st, idx, std::get<std::string>(v).c_str(), -1, SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) return false;
    }
    return true;
}

bool Db::exec(const std::string& sql, const std::vector<DbValue>& params, int64_t* lastInsertId, int* changes) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        lastError_ = sqlite3_errmsg(db_);
        std::fprintf(stderr, "[db] prepare failed: %s\n  %s\n", lastError_.c_str(), sql.c_str());
        return false;
    }
    bool ok = bindAll(st, params);
    int rc = SQLITE_DONE;
    if (ok) {
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {}
    }
    ok = ok && rc == SQLITE_DONE;
    if (!ok) lastError_ = sqlite3_errmsg(db_);
    if (lastInsertId) *lastInsertId = sqlite3_last_insert_rowid(db_);
    if (changes) *changes = sqlite3_changes(db_);
    sqlite3_finalize(st);
    return ok;
}

std::vector<Row> Db::query(const std::string& sql, const std::vector<DbValue>& params) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    std::vector<Row> rows;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        lastError_ = sqlite3_errmsg(db_);
        std::fprintf(stderr, "[db] prepare failed: %s\n  %s\n", lastError_.c_str(), sql.c_str());
        return rows;
    }
    if (!bindAll(st, params)) { sqlite3_finalize(st); return rows; }
    while (sqlite3_step(st) == SQLITE_ROW) {
        Row r;
        int n = sqlite3_column_count(st);
        for (int i = 0; i < n; i++) {
            std::string name = sqlite3_column_name(st, i);
            switch (sqlite3_column_type(st, i)) {
                case SQLITE_INTEGER: r[name] = (int64_t)sqlite3_column_int64(st, i); break;
                case SQLITE_FLOAT: r[name] = sqlite3_column_double(st, i); break;
                case SQLITE_NULL: r[name] = nullptr; break;
                default: {
                    const unsigned char* t = sqlite3_column_text(st, i);
                    r[name] = std::string(t ? (const char*)t : "");
                }
            }
        }
        rows.push_back(std::move(r));
    }
    sqlite3_finalize(st);
    return rows;
}

std::optional<Row> Db::one(const std::string& sql, const std::vector<DbValue>& params) {
    auto rows = query(sql, params);
    if (rows.empty()) return std::nullopt;
    return rows[0];
}

} // namespace backend
