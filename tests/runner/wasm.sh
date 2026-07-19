#!/bin/bash
# unified_test_runner.sh から source されるWASM実行補助モジュール。
# wasmtimeが無い環境向けにWASM実行用の共有nodeラッパー生成（setup_wasm_node_wrapper）を提供する。

# WASM実行用の共有nodeラッパーを生成する（wasmtimeが無い環境のフォールバック。
# 並列ワーカーからも参照するため起動時に一度だけ書き出す）
WASM_NODE_WRAPPER=""
setup_wasm_node_wrapper() {
    WASM_NODE_WRAPPER="$TEMP_DIR/wasm_node_wrapper.js"
    mkdir -p "$TEMP_DIR"
    cat > "$WASM_NODE_WRAPPER" << 'EOWRAP'
const fs = require('fs');
const wasmBuffer = fs.readFileSync(process.argv[2]);
const decoder = new TextDecoder();
let outputBuffer = '';
let result;
WebAssembly.instantiate(wasmBuffer, {
    wasi_snapshot_preview1: {
        proc_exit: (code) => {
            if (outputBuffer) process.stdout.write(outputBuffer);
            process.exit(code);
        },
        fd_write: (fd, iovs_ptr, iovs_len, nwritten_ptr) => {
            const memory = result.instance.exports.memory;
            const dataView = new DataView(memory.buffer);
            if (fd === 1) {
                let totalWritten = 0;
                for (let i = 0; i < iovs_len; i++) {
                    const iov_offset = iovs_ptr + (i * 8);
                    const buf_ptr = dataView.getUint32(iov_offset, true);
                    const buf_len = dataView.getUint32(iov_offset + 4, true);
                    const bytes = new Uint8Array(memory.buffer, buf_ptr, buf_len);
                    outputBuffer += decoder.decode(bytes);
                    totalWritten += buf_len;
                }
                const lines = outputBuffer.split('\n');
                if (lines.length > 1) {
                    for (let i = 0; i < lines.length - 1; i++) console.log(lines[i]);
                    outputBuffer = lines[lines.length - 1];
                }
                dataView.setUint32(nwritten_ptr, totalWritten, true);
                return 0;
            }
            return -1;
        },
        fd_close: () => 0, fd_seek: () => 0, fd_read: () => 0,
        environ_sizes_get: () => 0, environ_get: () => 0,
        args_sizes_get: () => 0, args_get: () => 0,
        random_get: () => 0, clock_time_get: () => 0, proc_raise: () => 0
    }
}).then(res => {
    result = res;
    if (result.instance.exports._start) {
        result.instance.exports._start();
        if (outputBuffer) process.stdout.write(outputBuffer);
    } else if (result.instance.exports.main) {
        const ret = result.instance.exports.main();
        if (outputBuffer) process.stdout.write(outputBuffer);
        process.exit(ret);
    }
}).catch(err => {
    console.error('WASM Error:', err);
    process.exit(1);
});
EOWRAP
}
