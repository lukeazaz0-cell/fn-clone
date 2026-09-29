// Backend REST client. Every call runs on a worker thread; poll the returned future.
#pragma once
#include <future>
#include <string>

#include "nlohmann/json.hpp"

namespace client {

using json = nlohmann::json;

struct ApiResult {
    int status = 0;          // HTTP status, 0 = connection failure
    json body;
    bool ok() const { return status >= 200 && status < 300; }
    std::string error() const;
};

class Api {
public:
    std::string baseUrl = "http://127.0.0.1:8080";
    std::string token;

    std::future<ApiResult> get(const std::string& path);
    std::future<ApiResult> post(const std::string& path, const json& body);
    std::future<ApiResult> put(const std::string& path, const json& body);

private:
    std::future<ApiResult> request(const std::string& method, const std::string& path, const json& body);
};

template <typename T>
bool ready(std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

} // namespace client
