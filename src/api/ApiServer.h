#pragma once
#include <nlohmann/json_fwd.hpp>
#include <functional>
#include <queue>
#include <mutex>
#include <future>
#include <thread>
#include <atomic>
#include <memory>
#include <string>

namespace httplib { class Server; }

// 编辑器内嵌的本地 HTTP 控制服务(供外部 AI / MCP 调用)。
// 线程模型: HTTP 收发在后台线程; 收到的每条请求打包成命令压入线程安全队列,
// 主循环每帧 drain() 在主线程真正执行(NodeGraph/OpenGL 非线程安全), 结果经
// std::promise 回传给 HTTP 线程应答。这样既不阻塞渲染, 又保证图操作都在主线程。
class ApiServer {
public:
    using Json = nlohmann::json;
    // 命令处理器: 在主线程执行, 输入 method + params, 返回结果 json。可抛异常(会被转成错误应答)。
    using Handler = std::function<Json(const std::string& method, const Json& params)>;

    ApiServer();
    ~ApiServer();

    // 启动 HTTP 服务并绑定命令处理器。port 默认 8765, 仅监听 127.0.0.1。
    void start(int port, Handler handler);
    void stop();

    // 主线程每帧调用: 执行队列里累积的命令。
    void drain();

    bool running() const { return m_running; }
    int  port()    const { return m_port; }

private:
    struct Command;   // 定义在 .cpp(含 std::promise<Json>, 需要完整 Json 类型)

    Handler                             m_handler;
    std::thread                         m_thread;
    std::unique_ptr<httplib::Server>    m_server;
    std::mutex                          m_mtx;
    std::queue<std::shared_ptr<Command>> m_queue;
    int                                 m_port = 0;
    std::atomic<bool>                   m_running{false};
};
