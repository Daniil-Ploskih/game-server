#pragma once

#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/json.hpp>
#include <iostream>

namespace logging = boost::log;
namespace expr = boost::log::expressions;
namespace json = boost::json;

inline void JsonFormatter(logging::record_view const& rec, logging::formatting_ostream& strm) {
    json::object result;

    auto ts_attr = rec["TimeStamp"];
    if (ts_attr) {
        auto pt_opt = ts_attr.extract<boost::posix_time::ptime>();
        if (pt_opt) {
            result["timestamp"] = boost::posix_time::to_iso_extended_string(*pt_opt);
        }
    }

    auto msg = rec[expr::smessage];
    if (msg) {
        result["message"] = msg.get();
    }

    auto data_attr = rec["AdditionalData"];
    if (data_attr) {
        auto data_opt = data_attr.extract<json::value>();
        if (data_opt) {
            result["data"] = *data_opt;
        } 
        else {
            result["data"] = json::object{};
        }
    } 
    else {
        result["data"] = json::object{};
    }

    strm << json::serialize(result);
}

inline void InitLogger() {
    logging::add_common_attributes();
    auto core = logging::core::get();
    auto sink = logging::add_console_log(
        std::cout,
        boost::log::keywords::format = &JsonFormatter,
        boost::log::keywords::auto_flush = true
    );
    core->set_filter(logging::trivial::severity >= logging::trivial::info);
}