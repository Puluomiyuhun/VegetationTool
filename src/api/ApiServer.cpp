#include "ApiServer.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>

using Json = nlohmann::json;

// Command 完整定义放这里(需要完整 Json 类型才能实例化 std::promise<Json>)。
struct ApiServer::Command {
    std::string           method;
    std::shared_ptr<Json> params;   // 用指针避免在头文件里完整包含 json
    std::promise<Json>    result;
};

ApiServer::ApiServer() = default;

ApiServer::~ApiServer() {
    stop();
}

void ApiServer::start(int port, Handler handler) {
    if (m_running) return;
    m_handler = std::move(handler);
    m_port    = port;
    m_server  = std::make_unique<httplib::Server>();

    // POST /rpc  { "method": "...", "params": {...} }
    // 应答: 成功 { "ok": true, "result": ... }; 失败 { "ok": false, "error": "..." }
    m_server->Post("/rpc", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        Json reply;
        try {
            Json body = Json::parse(req.body);
            std::string method = body.value("method", std::string());
            if (method.empty()) throw std::runtime_error("missing 'method'");

            auto cmd    = std::make_shared<Command>();
            cmd->method = method;
            cmd->params = std::make_shared<Json>(body.value("params", Json::object()));
            std::future<Json> fut = cmd->result.get_future();

            {
                std::lock_guard<std::mutex> lk(m_mtx);
                m_queue.push(cmd);
            }

            // 等待主线程 drain() 执行完毕(带超时, 防止主循环卡死时把 HTTP 线程也拖死)。
            if (fut.wait_for(std::chrono::seconds(10)) == std::future_status::ready) {
                Json r = fut.get();
                if (r.is_object() && r.contains("__error")) {
                    reply["ok"]    = false;
                    reply["error"] = r["__error"];
                } else {
                    reply["ok"]     = true;
                    reply["result"] = r;
                }
            } else {
                reply["ok"]    = false;
                reply["error"] = "timeout waiting for main thread";
            }
        } catch (const std::exception& e) {
            reply["ok"]    = false;
            reply["error"] = e.what();
        }
        res.set_content(reply.dump(), "application/json");
    });

    // 简单健康检查(GET, 直接在 HTTP 线程应答, 不入队)。
    m_server->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("{\"ok\":true}", "application/json");
    });

    m_running = true;
    m_thread  = std::thread([this, port]() {
        // 仅监听本地回环, 不对外暴露。
        if (!m_server->listen("127.0.0.1", port)) {
            std::fprintf(stderr, "[ApiServer] 无法监听 127.0.0.1:%d\n", port);
            m_running = false;
        }
    });

    std::fprintf(stdout, "[ApiServer] 已启动, 监听 127.0.0.1:%d/rpc\n", port);
}

void ApiServer::stop() {
    if (m_server) m_server->stop();
    if (m_thread.joinable()) m_thread.join();
    m_server.reset();
    m_running = false;

    // 释放仍在等待的请求, 避免 HTTP 线程 join 后 promise 悬空。
    std::lock_guard<std::mutex> lk(m_mtx);
    while (!m_queue.empty()) {
        auto cmd = m_queue.front();
        m_queue.pop();
        Json err;
        err["error"] = "server stopped";
        try { cmd->result.set_value(err); } catch (...) {}
    }
}

void ApiServer::drain() {
    for (;;) {
        std::shared_ptr<Command> cmd;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            if (m_queue.empty()) break;
            cmd = m_queue.front();
            m_queue.pop();
        }
        Json out;
        try {
            out = m_handler ? m_handler(cmd->method, *cmd->params) : Json::object();
        } catch (const std::exception& e) {
            // 处理器抛异常 -> 作为结果里的错误标记回传, 让 /rpc 层包成 ok=false。
            Json err;
            err["__error"] = e.what();
            out = err;
        }
        try { cmd->result.set_value(out); } catch (...) {}
    }
}
