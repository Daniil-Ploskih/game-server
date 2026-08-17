#pragma once
#include "sdk.h"

#define BOOST_BEAST_USE_STD_STRING_VIEW

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include "logger.h"

#include <memory>
#include <string>
#include <functional>
#include <iostream>

namespace http_server {

namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace beast = boost::beast;
namespace http = beast::http;

using namespace std::literals;

using StringRequest = http::request<http::string_body>;
using StringResponse = http::response<http::string_body>;

void ReportError(beast::error_code ec, std::string_view what);

class SessionBase {
protected:
    using HttpRequest = http::request<http::string_body>;
    using SendResponseCallback = std::function<void(StringResponse&&)>;

    SessionBase(const SessionBase&) = delete;
    SessionBase& operator=(const SessionBase&) = delete;

protected:
    explicit SessionBase(tcp::socket&& socket);
    ~SessionBase();

    template <typename Body, typename Fields>
    void Write(http::response<Body, Fields>&& response);

    void Close();

    const beast::tcp_stream& GetStream() const noexcept { return stream_; }

public:
    void Run();

private:
    void Read();
    void OnRead(beast::error_code ec, std::size_t);
    void OnWrite(bool close, beast::error_code ec);

    virtual std::shared_ptr<SessionBase> GetSharedThis() = 0;
    virtual void HandleRequestAsync(HttpRequest&& req) = 0;

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    HttpRequest request_;
};

template <typename RequestHandler>
class Session : public SessionBase, public std::enable_shared_from_this<Session<RequestHandler>> {
public:
    template <typename Handler>
    Session(tcp::socket&& socket, Handler&& request_handler)
        : SessionBase(std::move(socket))
        , request_handler_(std::forward<Handler>(request_handler)) {}

private:
    std::shared_ptr<SessionBase> GetSharedThis() override {
        return this->shared_from_this();
    }

    void HandleRequestAsync(HttpRequest&& req) override {
        auto self = this->shared_from_this();
        SendResponseCallback sender = [self](StringResponse&& response) {
            self->Write(std::move(response));
        };
        try {
            request_handler_(std::move(req), std::move(sender));
        } catch (const std::exception& e) {
            std::cerr << "Handler error: " << e.what() << std::endl;
            self->Close();
        }
    }

    RequestHandler request_handler_;
};

template <typename RequestHandler>
class Listener : public std::enable_shared_from_this<Listener<RequestHandler>> {
public:
    template <typename Handler>
    Listener(net::io_context& ioc, const tcp::endpoint& endpoint, Handler&& request_handler)
        : ioc_(ioc)
        , strand_(ioc.get_executor())
        , acceptor_(ioc)
        , request_handler_(std::forward<Handler>(request_handler)) {
        
        beast::error_code ec;
        acceptor_.open(endpoint.protocol(), ec);
        if (ec) throw std::runtime_error("open: " + ec.message());
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) throw std::runtime_error("set_option: " + ec.message());
        acceptor_.bind(endpoint, ec);
        if (ec) throw std::runtime_error("bind: " + ec.message());
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) throw std::runtime_error("listen: " + ec.message());
    }

    void Run() { DoAccept(); }

private:
    void DoAccept() {
        acceptor_.async_accept(
            strand_,
            beast::bind_front_handler(&Listener::OnAccept, this->shared_from_this()));
    }

    void OnAccept(beast::error_code ec, tcp::socket socket) {
        if (ec) {
            ReportError(ec, "accept");
        } else {
            std::make_shared<Session<RequestHandler>>(
                std::move(socket), request_handler_)->Run();
        }
        DoAccept();
    }

    net::io_context& ioc_;
    net::strand<net::io_context::executor_type> strand_;
    tcp::acceptor acceptor_;
    RequestHandler request_handler_;
};

template <typename RequestHandler>
void ServeHttp(net::io_context& ioc, const tcp::endpoint& endpoint, RequestHandler&& handler) {
    using MyListener = Listener<std::decay_t<RequestHandler>>;
    std::make_shared<MyListener>(ioc, endpoint, std::forward<RequestHandler>(handler))->Run();
}

}  // namespace http_server