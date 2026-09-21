#include "test_main.hpp"

#include "base/error.hpp"

#include <string>

using otter::Errc;
using otter::Error;
using otter::Result;
using otter::Status;

namespace {

Result<int> succeed() { return 7; }

Result<int> fail_with(Errc code, std::string msg) {
    return otter::fail(code, std::move(msg));
}

Result<int> propagates() {
    OTTER_ASSIGN_OR_RETURN(auto value, fail_with(Errc::query_failed, "syntax error"));
    return value * 2;
}

Result<int> chains_ok() {
    OTTER_ASSIGN_OR_RETURN(auto value, succeed());
    return value * 2;
}

Status void_failure() { return otter::fail(Errc::closed, "connection closed"); }

Status forwards_void() {
    OTTER_RETURN_IF_ERROR(void_failure());
    return {};
}

} // namespace

OTTER_TEST(error_result_success) {
    auto r = succeed();
    OTTER_CHECK(r.has_value());
    OTTER_CHECK_EQ(*r, 7);
}

OTTER_TEST(error_result_failure_carries_code_and_message) {
    auto r = fail_with(Errc::auth_failed, "senha invalida");
    OTTER_CHECK(!r.has_value());
    OTTER_CHECK(r.error().code() == Errc::auth_failed);
    OTTER_CHECK_EQ(r.error().message(), std::string{"senha invalida"});
}

OTTER_TEST(error_assign_or_return_propagates) {
    auto r = propagates();
    OTTER_CHECK(!r.has_value());
    OTTER_CHECK(r.error().code() == Errc::query_failed);
}

OTTER_TEST(error_assign_or_return_passes_value_through) {
    auto r = chains_ok();
    OTTER_CHECK(r.has_value());
    OTTER_CHECK_EQ(*r, 14);
}

OTTER_TEST(error_return_if_error_on_status) {
    auto s = forwards_void();
    OTTER_CHECK(!s.has_value());
    OTTER_CHECK(s.error().code() == Errc::closed);
}

OTTER_TEST(error_with_context_prepends) {
    Error base{Errc::io_error, "disco cheio"};
    Error wrapped = base.with_context("gravar cache de metadados");

    OTTER_CHECK(wrapped.code() == Errc::io_error);
    OTTER_CHECK_EQ(wrapped.message(),
                   std::string{"gravar cache de metadados: disco cheio"});
}

OTTER_TEST(error_with_context_on_empty_message) {
    Error base{Errc::timed_out};
    OTTER_CHECK_EQ(base.with_context("conectar").message(), std::string{"conectar"});
}

OTTER_TEST(error_to_string_includes_code_name) {
    Error e{Errc::connection_failed, "host inacessivel"};
    OTTER_CHECK_EQ(e.to_string(), std::string{"host inacessivel [connection failed]"});
}

OTTER_TEST(error_to_string_without_message) {
    OTTER_CHECK_EQ(Error{Errc::cancelled}.to_string(), std::string{"cancelled"});
}

OTTER_TEST(error_all_codes_have_names) {
    // Um codigo sem nome cai no default e devolve "unknown error" -- queremos
    // que adicionar um Errc sem atualizar to_string() seja detectado aqui.
    const Errc codes[] = {
        Errc::ok, Errc::invalid_argument, Errc::out_of_range, Errc::not_found,
        Errc::already_exists, Errc::not_supported, Errc::internal,
        Errc::out_of_memory, Errc::io_error, Errc::permission_denied,
        Errc::cancelled, Errc::timed_out, Errc::closed,
        Errc::connection_failed, Errc::auth_failed, Errc::protocol_error,
        Errc::query_failed, Errc::type_mismatch,
        Errc::invalid_utf8, Errc::parse_error,
    };
    for (Errc c : codes) {
        OTTER_CHECK(otter::to_string(c) != std::string_view{"unknown error"});
    }
}
