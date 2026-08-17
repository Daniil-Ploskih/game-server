#include "http_server.h"
#include "logger.h"

#include <boost/log/utility/manipulators/add_value.hpp>

#include <iostream>

using boost::log::add_value;

namespace http_server {

void ReportError(beast::error_code ec, std::string_view what) {
    if (ec != net::error::operation_aborted) {
        json::value error_data = json::object{
            {"code", ec.value()},
            {"text", ec.message()},
            {"where", std::string(what)}
        };
        
        BOOST_LOG_TRIVIAL(error)
            << add_value("AdditionalData", error_data)
            << "error"sv;
        
        std::cerr << what << ": " << ec.message() << std::endl;
    }
}

SessionBase::SessionBase(tcp::socket&& socket)
    : stream_(std::move(socket)) {}

SessionBase::~SessionBase() = default;

template <typename Body, typename Fields>
void SessionBase::Write(http::response<Body, Fields>&& response) {
    auto safe_response = std::make_shared<http::response<Body, Fields>>(std::move(response));
    auto self = GetSharedThis();
    
    http::async_write(
        stream_,
        *safe_response,
        [safe_response, self](beast::error_code ec, std::size_t) {
            self->OnWrite(safe_response->need_eof(), ec);
        });
}

template void SessionBase::Write<http::string_body, http::fields>(http::response<http::string_body, http::fields>&&);

void SessionBase::Close() {
    try {
        stream_.socket().shutdown(tcp::socket::shutdown_send);
    } catch (const std::exception& e) {
        std::cerr << "Close error: " << e.what() << std::endl;
    }
}

void SessionBase::Run() {
    net::dispatch(stream_.get_executor(),
                  beast::bind_front_handler(&SessionBase::Read, GetSharedThis()));
}

void SessionBase::Read() {
    request_ = {};
    buffer_.consume(buffer_.size());
    stream_.expires_after(30s);
    
    http::async_read(
        stream_, buffer_, request_,
        beast::bind_front_handler(&SessionBase::OnRead, GetSharedThis()));
}

void SessionBase::OnRead(beast::error_code ec, std::size_t) {
    if (ec == http::error::end_of_stream) {
        return Close();
    }
    if (ec) {
        return ReportError(ec, "read");
    }
    HandleRequestAsync(std::move(request_));
}

void SessionBase::OnWrite(bool close, beast::error_code ec) {
    if (ec) {
        return ReportError(ec, "write");
    }
    if (close) {
        return Close();
    }
    Read();
}

}  // namespace http_server