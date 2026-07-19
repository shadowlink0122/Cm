/// @file system.cpp
/// @brief システム系ランタイム関数の宣言（TCP/UDP/DNS/Atomic/Channel/Thread/HTTP）

#include "internal/codegen/llvm/core/mir_to_llvm.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace cm::codegen::llvm_backend {

/// ネットワーク・並行処理・HTTP系ランタイム関数のシグネチャ宣言（該当しない場合はnullptr）
llvm::Function* MIRToLLVM::declareSystemRuntimeFunction(const std::string& name) {
    // ============================================================
    // TCP ネットワーク関数 (net_runtime.cpp)
    // ============================================================
    // int64_t cm_tcp_listen(int32_t port)
    if (name == "cm_tcp_listen") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_tcp_accept(int64_t server_fd)
    else if (name == "cm_tcp_accept") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_tcp_connect(int64_t host_ptr, int32_t port)
    else if (name == "cm_tcp_connect") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_tcp_read(int64_t fd, int64_t buf_ptr, int32_t size)
    // int32_t cm_tcp_write(int64_t fd, int64_t buf_ptr, int32_t size)
    else if (name == "cm_tcp_read" || name == "cm_tcp_write") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(), {ctx.getI64Type(), ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_tcp_close(int64_t fd)
    else if (name == "cm_tcp_close") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_tcp_set_nonblocking(int64_t fd)
    else if (name == "cm_tcp_set_nonblocking") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_tcp_poll_create()
    else if (name == "cm_tcp_poll_create") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_tcp_poll_add(int64_t poll_handle, int64_t fd, int32_t events)
    else if (name == "cm_tcp_poll_add") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(), {ctx.getI64Type(), ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_tcp_poll_remove(int64_t poll_handle, int64_t fd)
    else if (name == "cm_tcp_poll_remove") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_tcp_poll_wait(int64_t poll_handle, int32_t timeout_ms)
    else if (name == "cm_tcp_poll_wait") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_tcp_poll_get_fd(int64_t poll_handle, int32_t index)
    else if (name == "cm_tcp_poll_get_fd") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_tcp_poll_get_events(int64_t poll_handle, int32_t index)
    else if (name == "cm_tcp_poll_get_events") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_tcp_poll_destroy(int64_t poll_handle)
    else if (name == "cm_tcp_poll_destroy") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // UDP ネットワーク関数 (net_runtime.cpp)
    // ============================================================
    // int64_t cm_udp_create()
    else if (name == "cm_udp_create") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_udp_bind(int64_t fd, int32_t port)
    else if (name == "cm_udp_bind") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_udp_sendto(int64_t fd, int64_t host_ptr, int32_t port, int64_t buf_ptr, int32_t size)
    else if (name == "cm_udp_sendto") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(),
                                    {ctx.getI64Type(), ctx.getI64Type(), ctx.getI32Type(),
                                     ctx.getI64Type(), ctx.getI32Type()},
                                    false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_udp_recvfrom(int64_t fd, int64_t buf_ptr, int32_t size)
    else if (name == "cm_udp_recvfrom") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(), {ctx.getI64Type(), ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_udp_close(int64_t fd)
    else if (name == "cm_udp_close") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_udp_set_broadcast(int64_t fd)
    else if (name == "cm_udp_set_broadcast") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // DNS / Socket ユーティリティ (net_runtime.cpp)
    // ============================================================
    // char* cm_dns_resolve(const char* hostname)
    else if (name == "cm_dns_resolve") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_socket_set_timeout(int64_t fd, int32_t timeout_ms)
    // int32_t cm_socket_set_reuse_addr(int64_t fd) 等のソケットオプション
    else if (name == "cm_socket_set_timeout" || name == "cm_socket_set_recv_buffer" ||
             name == "cm_socket_set_send_buffer") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_socket_set_reuse_addr" || name == "cm_socket_set_nodelay" ||
               name == "cm_socket_set_keepalive") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // Atomic操作 (sync_runtime.cpp) — cm_プレフィクス版 + レガシー版
    // ============================================================
    // i32: load/store/fetch_add/fetch_sub
    else if (name == "cm_atomic_load_i32" || name == "atomic_load_i32") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_atomic_store_i32" || name == "atomic_store_i32") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_atomic_fetch_add_i32" || name == "cm_atomic_fetch_sub_i32" ||
               name == "atomic_fetch_add_i32" || name == "atomic_fetch_sub_i32") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // i32: compare_exchange(ptr, ptr, i32) → int
    else if (name == "cm_atomic_compare_exchange_i32" || name == "atomic_compare_exchange_i32") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(), {ctx.getPtrType(), ctx.getPtrType(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // i64: load/store/fetch_add/fetch_sub
    else if (name == "cm_atomic_load_i64" || name == "atomic_load_i64") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_atomic_store_i64" || name == "atomic_store_i64") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_atomic_fetch_add_i64" || name == "cm_atomic_fetch_sub_i64" ||
               name == "atomic_fetch_add_i64" || name == "atomic_fetch_sub_i64") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // i64: compare_exchange(ptr, i64, i64) → int
    else if (name == "cm_atomic_compare_exchange_i64" || name == "atomic_compare_exchange_i64") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(), {ctx.getPtrType(), ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // Channel (channel_runtime.cpp)
    // ============================================================
    // int64_t cm_channel_create(int32_t capacity)
    else if (name == "cm_channel_create") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_channel_send(int64_t handle, int64_t value)
    // int32_t cm_channel_try_send(int64_t handle, int64_t value)
    else if (name == "cm_channel_send" || name == "cm_channel_try_send") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_channel_recv(int64_t handle, int64_t* value)
    // int32_t cm_channel_try_recv(int64_t handle, int64_t* value)
    else if (name == "cm_channel_recv" || name == "cm_channel_try_recv") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_channel_close(int64_t handle) / void cm_channel_destroy(int64_t handle)
    else if (name == "cm_channel_close" || name == "cm_channel_destroy") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_channel_len(int64_t handle) / int32_t cm_channel_is_closed(int64_t handle)
    else if (name == "cm_channel_len" || name == "cm_channel_is_closed") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // Thread (thread_runtime.cpp)
    // ============================================================
    // uint64_t cm_thread_create(void* fn, void* arg)
    // uint64_t cm_thread_spawn_with_arg(void* fn, void* arg)
    else if (name == "cm_thread_create" || name == "cm_thread_spawn_with_arg") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int cm_thread_join(uint64_t thread_id, void** retval)
    else if (name == "cm_thread_join") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_thread_detach(uint64_t thread_id)
    else if (name == "cm_thread_detach") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // uint64_t cm_thread_self()
    else if (name == "cm_thread_self") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_thread_sleep_us(uint64_t microseconds)
    else if (name == "cm_thread_sleep_us") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_thread_join_all(uint64_t* handles, int32_t count)
    else if (name == "cm_thread_join_all") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // HTTP クライアント (http_runtime.cpp)
    // ============================================================
    // int64_t cm_http_request_create()
    else if (name == "cm_http_request_create") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_method(int64_t handle, int32_t method)
    else if (name == "cm_http_request_set_method") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_url(int64_t handle, const char* host, int32_t port, const char* path)
    else if (name == "cm_http_request_set_url") {
        auto funcType = llvm::FunctionType::get(
            ctx.getVoidType(),
            {ctx.getI64Type(), ctx.getPtrType(), ctx.getI32Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_header(int64_t handle, const char* key, const char* value)
    else if (name == "cm_http_request_set_header") {
        auto funcType = llvm::FunctionType::get(
            ctx.getVoidType(), {ctx.getI64Type(), ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_body(int64_t handle, const char* body)
    else if (name == "cm_http_request_set_body") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_destroy(int64_t handle)
    // void cm_http_response_destroy(int64_t handle)
    // void cm_http_server_req_destroy(int64_t handle)
    else if (name == "cm_http_request_destroy" || name == "cm_http_response_destroy" ||
             name == "cm_http_server_req_destroy") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_http_execute(int64_t req_handle)
    else if (name == "cm_http_execute") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_http_response_status(int64_t handle)
    // int32_t cm_http_response_is_error(int64_t handle)
    else if (name == "cm_http_response_status" || name == "cm_http_response_is_error") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // const char* cm_http_response_body(int64_t handle)
    else if (name == "cm_http_response_body") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // const char* cm_http_response_header(int64_t handle, const char* key)
    else if (name == "cm_http_response_header") {
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_http_get(const char* host, int32_t port, const char* path)
    // int64_t cm_http_delete(const char* host, int32_t port, const char* path)
    else if (name == "cm_http_get" || name == "cm_http_delete") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI64Type(), {ctx.getPtrType(), ctx.getI32Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_http_post(const char* host, int32_t port, const char* path, const char* body)
    // int64_t cm_http_put(const char* host, int32_t port, const char* path, const char* body)
    else if (name == "cm_http_post" || name == "cm_http_put") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI64Type(),
            {ctx.getPtrType(), ctx.getI32Type(), ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // HTTP サーバー (http_runtime.cpp)
    // ============================================================
    // int64_t cm_http_server_create(int32_t port)
    else if (name == "cm_http_server_create") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_server_close(int64_t server_fd)
    else if (name == "cm_http_server_close") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_http_server_accept(int64_t server_fd)
    else if (name == "cm_http_server_accept") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_server_respond(int64_t handle, int32_t status, const char* body)
    else if (name == "cm_http_server_respond") {
        auto funcType = llvm::FunctionType::get(
            ctx.getVoidType(), {ctx.getI64Type(), ctx.getI32Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // const char* cm_http_server_req_method/path/body(int64_t handle)
    else if (name == "cm_http_server_req_method" || name == "cm_http_server_req_path" ||
             name == "cm_http_server_req_body") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // const char* cm_http_server_req_header(int64_t handle, const char* key)
    else if (name == "cm_http_server_req_header") {
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // const char* cm_http_error_message(int64_t handle)
    // const char* cm_http_response_content_type(int64_t handle)
    // const char* cm_http_response_location(int64_t handle)
    else if (name == "cm_http_error_message" || name == "cm_http_response_content_type" ||
             name == "cm_http_response_location") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int cm_http_response_is_redirect(int64_t handle)
    else if (name == "cm_http_response_is_redirect") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_http_test_server_start(int32_t port, int32_t max_requests)
    else if (name == "cm_http_test_server_start") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI32Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // URL解析関数
    // int64_t cm_http_parse_url(const char* url)
    else if (name == "cm_http_parse_url") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // const char* cm_http_parsed_scheme/host/path(int64_t handle)
    else if (name == "cm_http_parsed_scheme" || name == "cm_http_parsed_host" ||
             name == "cm_http_parsed_path") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int cm_http_parsed_port(int64_t handle)
    else if (name == "cm_http_parsed_port") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_parsed_url_destroy(int64_t handle)
    else if (name == "cm_http_parsed_url_destroy") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // リクエストオプション: void cm_http_request_set_*(int64_t handle, ...)
    else if (name == "cm_http_request_set_timeout" ||
             name == "cm_http_request_set_follow_redirects" ||
             name == "cm_http_request_set_max_redirects") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_basic_auth(int64_t handle, const char* user, const char* pass)
    else if (name == "cm_http_request_set_basic_auth") {
        auto funcType = llvm::FunctionType::get(
            ctx.getVoidType(), {ctx.getI64Type(), ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_bearer_auth/content_type(int64_t handle, const char* value)
    else if (name == "cm_http_request_set_bearer_auth" ||
             name == "cm_http_request_set_content_type") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_json(int64_t handle)
    else if (name == "cm_http_request_set_json") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // 未該当: 呼び出し元（declareExternalFunction）で後続の解決を継続する
    return nullptr;
}

}  // namespace cm::codegen::llvm_backend
