#include "api.h"

#include "httplib.h"

namespace client {

std::string ApiResult::error() const {
    if (status == 0) return "Can't reach the backend server";
    if (body.is_object() && body.contains("error") && body["error"].is_string()) return body["error"].get<std::string>();
    return "Request failed (HTTP " + std::to_string(status) + ")";
}

std::future<ApiResult> Api::get(const std::string& path) { return request("GET", path, nullptr); }
std::future<ApiResult> Api::post(const std::string& path, const json& body) { return request("POST", path, body); }
std::future<ApiResult> Api::put(const std::string& path, const json& body) { return request("PUT", path, body); }

std::future<ApiResult> Api::request(const std::string& method, const std::string& path, const json& body) {
    std::string base = baseUrl, tok = token;
    std::string payload = body.is_null() ? "" : body.dump();
    return std::async(std::launch::async, [=]() {
        ApiResult r;
        std::string url = base;
        while (!url.empty() && url.back() == '/') url.pop_back();
        if (url.find("://") == std::string::npos) url = "http://" + url;
        httplib::Client cli(url);
        cli.set_connection_timeout(4, 0);
        cli.set_read_timeout(8, 0);
        httplib::Headers h;
        if (!tok.empty()) h.emplace("Authorization", "Bearer " + tok);
        httplib::Result res(nullptr, httplib::Error::Unknown);
        if (method == "GET") res = cli.Get(path, h);
        else if (method == "POST") res = cli.Post(path, h, payload, "application/json");
        else res = cli.Put(path, h, payload, "application/json");
        if (!res) return r;
        r.status = res->status;
        r.body = json::parse(res->body, nullptr, false);
        if (r.body.is_discarded()) r.body = json::object();
        return r;
    });
}

} // namespace client
