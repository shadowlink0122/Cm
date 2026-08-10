#!/bin/bash
# unified_test_runner.sh から source されるテスト実行モジュール。
# 単一テストの実行と結果判定（run_single_test）を提供する。

# テスト実行関数
run_single_test() {
    local test_file="$1"
    local test_name="$(basename "${test_file%.cm}")"
    # programs/以下の相対パスをカテゴリとして使用（例: common/thread）
    local category="$(dirname "$test_file" | sed "s|^$PROGRAMS_DIR/||")"
    local expect_file="${test_file%.cm}.expect"
    local backend_expect_file="${test_file%.cm}.expect.${BACKEND}"
    local error_expect_file="${test_file%.cm}.error"
    local backend_error_file="${test_file%.cm}.error.${BACKEND}"
    local output_file="$TEMP_DIR/${category//\//_}_${test_name}.out"
    local error_file="$TEMP_DIR/${category//\//_}_${test_name}.err"
    local is_error_test=false

    # テスト別タイムアウト: .timeoutファイルがあれば値を上書き
    local test_timeout="$TIMEOUT"
    local timeout_file="${test_file%.cm}.timeout"
    if [ -f "$timeout_file" ]; then
        test_timeout=$(cat "$timeout_file" | tr -d '[:space:]')
    fi

    # //! platform: ディレクティブチェック
    local platform_skip_reason
    platform_skip_reason=$(check_platform_directive "$test_file" "$BACKEND")
    if [ $? -ne 0 ]; then
        echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - $platform_skip_reason"
        ((SKIPPED++))
        return
    fi

    # .skipファイルのチェック
    local skip_file="${test_file%.cm}.skip"
    # カテゴリ.skipはテストファイルのディレクトリから祖先方向へ最初に見つかったものを採用（サブフォルダの階層は問わない）
    local category_skip_file=""
    local _skip_dir="$(dirname "$test_file")"
    while [ -n "$_skip_dir" ] && [ "$_skip_dir" != "/" ] && [ "$_skip_dir" != "." ]; do
        if [ -f "$_skip_dir/.skip" ]; then category_skip_file="$_skip_dir/.skip"; break; fi
        case "$_skip_dir" in */tests) break;; esac
        _skip_dir="$(dirname "$_skip_dir")"
    done
    local current_os=$(uname -s | tr '[:upper:]' '[:lower:]')
    local current_arch=$(uname -m)
    local current_opt="o${OPT_LEVEL:-3}"

    # スキップパターンマッチング関数
    # 形式: backend[-optlevel][:os[:arch]]
    # 例: llvm, llvm:linux, llvm:linux:x86_64, llvm-o3, llvm-o3:linux:x86_64
    match_skip_pattern() {
        local pattern="$1"
        local backend="$2"
        local opt="$3"
        local os="$4"
        local arch="$5"

        # パターンをパース
        local p_backend p_opt p_os p_arch
        if [[ "$pattern" =~ ^([a-z-]+)(-o[0-3])?(:([a-z]+))?(:([a-z0-9_]+))?$ ]]; then
            p_backend="${BASH_REMATCH[1]}"
            p_opt="${BASH_REMATCH[2]#-}"  # -o3 -> o3
            p_os="${BASH_REMATCH[4]}"
            p_arch="${BASH_REMATCH[6]}"
        else
            # 旧形式: backend または backend:os
            if [[ "$pattern" =~ ^([a-z-]+):([a-z]+)$ ]]; then
                p_backend="${BASH_REMATCH[1]}"
                p_os="${BASH_REMATCH[2]}"
            else
                p_backend="$pattern"
            fi
        fi

        # バックエンドマッチ
        [[ "$p_backend" != "$backend" ]] && return 1

        # 最適化レベルマッチ（指定されていれば）
        [[ -n "$p_opt" && "$p_opt" != "$opt" ]] && return 1

        # OSマッチ（指定されていれば）
        [[ -n "$p_os" && "$p_os" != "$os" ]] && return 1

        # アーキテクチャマッチ（指定されていれば）
        [[ -n "$p_arch" && "$p_arch" != "$arch" ]] && return 1

        return 0
    }

    # ファイル固有の.skipファイルがある場合
    if [ -f "$skip_file" ]; then
        # .skipファイルの内容を読んで、現在のバックエンドがスキップ対象か確認
        if [ -s "$skip_file" ]; then
            local has_pattern=0
            while IFS= read -r line || [[ -n "$line" ]]; do
                # コメントと空行をスキップ
                [[ "$line" =~ ^[[:space:]]*# ]] && continue
                [[ -z "${line// }" ]] && continue
                line="${line%%#*}"  # インラインコメント除去
                line="${line// /}"  # 空白除去
                has_pattern=1

                if match_skip_pattern "$line" "$BACKEND" "$current_opt" "$current_os" "$current_arch"; then
                    echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - Skipped for $line"
                    ((SKIPPED++))
                    return
                fi
            done < "$skip_file"
            # パターン行がなくコメント（理由）のみの場合、全バックエンドでスキップ
            if [ "$has_pattern" -eq 0 ]; then
                local skip_reason=$(grep -m1 '^[[:space:]]*#' "$skip_file" | sed 's/^[[:space:]]*#[[:space:]]*//')
                echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - ${skip_reason:-理由未記載}"
                ((SKIPPED++))
                return
            fi
        else
            # ファイルが空の場合、すべてのバックエンドでスキップ
            # （skipファイルには理由をコメントで記録すること）
            echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - Skip file exists (理由未記載: ${skip_file} にコメントで理由を記録してください)"
            ((SKIPPED++))
            return
        fi
    fi

    # カテゴリ全体の.skipファイルがある場合
    if [ -f "$category_skip_file" ]; then
        if [ -s "$category_skip_file" ]; then
            while IFS= read -r line || [[ -n "$line" ]]; do
                [[ "$line" =~ ^[[:space:]]*# ]] && continue
                [[ -z "${line// }" ]] && continue
                line="${line%%#*}"
                line="${line// /}"

                if match_skip_pattern "$line" "$BACKEND" "$current_opt" "$current_os" "$current_arch"; then
                    echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - Category skipped for $line"
                    ((SKIPPED++))
                    return
                fi
            done < "$category_skip_file"
        else
            echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - Category skip file exists"
            ((SKIPPED++))
            return
        fi
    fi

    # バックエンド固有のerrorファイルがあれば優先して使用
    if [ -f "$backend_error_file" ]; then
        error_expect_file="$backend_error_file"
        is_error_test=true
    elif [ -f "$error_expect_file" ]; then
        is_error_test=true
    fi

    # バックエンド固有のexpectファイルがあれば優先して使用
    # バックエンド固有のexpectファイルがある場合、汎用errorファイルよりも優先する
    if [ -f "$backend_expect_file" ]; then
        expect_file="$backend_expect_file"
        is_error_test=false
    fi

    # expectファイルもerrorファイルもない場合はスキップ
    if [ ! -f "$expect_file" ] && [ ! -f "$error_expect_file" ]; then
        echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - No expect/error file"
        ((SKIPPED++))
        return
    fi

    # マクロ/モジュールテストは未実装のため特別処理
    if [[ "$category" == "macros" || "$category" == "modules" ]]; then
        # expectファイルに「未実装」が含まれているかチェック
        if grep -q "未実装\|not implemented" "$expect_file" 2>/dev/null; then
            if [ "$BACKEND" != "interpreter" ]; then
                echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - Not implemented"
                ((SKIPPED++))
                return
            fi
        fi
    fi

    local exit_code=0

    # タイムアウト付きコマンド実行のヘルパー関数
    run_with_timeout() {
        if [ -n "$TIMEOUT_CMD" ]; then
            if [ "$TIMEOUT_MODE" = "python" ]; then
                "$TIMEOUT_CMD" - "$test_timeout" "$@" <<'PY'
import subprocess
import sys

timeout = int(sys.argv[1])
cmd = sys.argv[2:]
try:
    proc = subprocess.Popen(cmd)
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
        sys.exit(124)
    sys.exit(proc.returncode if proc.returncode is not None else 1)
except FileNotFoundError:
    sys.exit(127)
PY
            else
                # --kill-after: タイムアウト後さらに2秒待ってもプロセスが終了しなければSIGKILL
                $TIMEOUT_CMD --kill-after=2 "$test_timeout" "$@"
            fi
        else
            # タイムアウトコマンドがない場合は直接実行
            "$@"
        fi
    }

    case "$BACKEND" in
        interpreter)
            # テストファイルのディレクトリに移動して実行（モジュールの相対パス解決のため）
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # インタプリタで実行
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" run -O$OPT_LEVEL $CACHE_OPTS "$test_basename" > "$output_file" 2>&1) || exit_code=$?
            ;;

        jit)
            # JITコンパイラで実行（LLVM ORC JIT）
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # JITで実行
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" run -O$OPT_LEVEL $CACHE_OPTS "$test_basename" > "$output_file" 2>&1) || exit_code=$?
            ;;

        typescript)
            # TypeScriptにコンパイルして実行
            local ts_dir="$TEMP_DIR/ts_${test_name}"
            rm -rf "$ts_dir"

            # コンパイル（エラー時は出力ファイルにエラーメッセージを書き込む）
            run_with_timeout "$CM_EXECUTABLE" compile --emit-ts $CACHE_OPTS "$test_file" -o "$ts_dir" > "$output_file" 2>&1 || exit_code=$?

            if [ $exit_code -eq 0 ]; then
                # TypeScriptプロジェクトのビルドと実行
                pushd "$ts_dir" > /dev/null 2>&1
                npm install > /dev/null 2>&1 || exit_code=$?
                if [ $exit_code -eq 0 ]; then
                    npm run build > /dev/null 2>&1 || exit_code=$?
                    if [ $exit_code -eq 0 ]; then
                        run_with_timeout node main.js > "$output_file" 2>&1 || exit_code=$?
                    fi
                fi
                popd > /dev/null 2>&1
            fi
            ;;

        rust)
            # Rustにコンパイルして実行
            local rust_dir="$TEMP_DIR/rust_${test_name}"
            rm -rf "$rust_dir"

            # コンパイル（エラー時は出力ファイルにエラーメッセージを書き込む）
            run_with_timeout "$CM_EXECUTABLE" compile --emit-rust $CACHE_OPTS "$test_file" -o "$rust_dir" > "$output_file" 2>&1 || exit_code=$?

            if [ $exit_code -eq 0 ]; then
                # Rustコンパイルと実行
                pushd "$rust_dir" > /dev/null 2>&1
                rustc main.rs -o main 2> /dev/null || exit_code=$?
                if [ $exit_code -eq 0 ]; then
                    run_with_timeout ./main > "$output_file" 2>&1 || exit_code=$?
                fi
                popd > /dev/null 2>&1
            fi
            ;;

        cpp)
            # C++にコンパイルして実行
            local cpp_dir="$TEMP_DIR/cpp_${test_name}"
            rm -rf "$cpp_dir"

            # コンパイル
            run_with_timeout "$CM_EXECUTABLE" compile --emit-cpp $CACHE_OPTS "$test_file" -o "$cpp_dir" > /dev/null 2>&1 || exit_code=$?

            if [ $exit_code -eq 0 ]; then
                # C++コンパイルと実行
                pushd "$cpp_dir" > /dev/null 2>&1
                g++ -std=c++17 main.cpp -o main 2> /dev/null || exit_code=$?
                if [ $exit_code -eq 0 ]; then
                    run_with_timeout ./main > "$output_file" 2>&1 || exit_code=$?
                fi
                popd > /dev/null 2>&1
            fi
            ;;

        llvm)
            # LLVMバックエンドでネイティブコードにコンパイルして実行
            local llvm_exec="$TEMP_DIR/llvm_${test_name}"
            # Windows対応: .exe拡張子
            if [ "$IS_WINDOWS" = true ]; then
                llvm_exec="${llvm_exec}.exe"
            fi
            rm -f "$llvm_exec"

            # テストファイルのディレクトリに移動してコンパイル（モジュールの相対パス解決のため）
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # LLVM経由でネイティブ実行ファイル生成（エラー時は出力ファイルにエラーメッセージを書き込む）
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile --emit-llvm -O$OPT_LEVEL $CACHE_OPTS "$test_basename" -o "$llvm_exec" > "$output_file" 2>&1) || exit_code=$?

            if [ $exit_code -eq 0 ] && [ -f "$llvm_exec" ]; then
                # テストディレクトリで実行（相対パス解決のため）
                (cd "$test_dir" && run_with_timeout "$llvm_exec" > "$output_file" 2>&1) || exit_code=$?
                
                # セグフォ時にgdbでデバッグ情報を取得（CI環境のみ）
                if [ $exit_code -eq 139 ] && [ -n "$CI" ] && command -v gdb >/dev/null 2>&1; then
                    echo "=== Segmentation fault detected, running gdb ===" >> "$output_file"
                    echo "run" | gdb --batch -ex "set pagination off" -ex "run" -ex "bt" -ex "quit" "$llvm_exec" >> "$output_file" 2>&1 || true
                fi
            fi
            ;;

        llvm-wasm)
            # LLVMバックエンドでWebAssemblyにコンパイルして実行
            local wasm_file="$TEMP_DIR/wasm_${test_name}.wasm"
            rm -f "$wasm_file"

            # LLVM経由でWebAssembly生成（エラー時は出力ファイルにエラーメッセージを書き込む）
            # テストファイルのディレクトリに移動してコンパイル（モジュールの相対パス解決のため）
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile --emit-llvm --target=wasm -O$OPT_LEVEL $CACHE_OPTS "$test_basename" -o "$wasm_file" > "$output_file" 2>&1) || exit_code=$?

            if [ $exit_code -eq 0 ] && [ -f "$wasm_file" ]; then
                # WASMランタイムで実行
                # 優先順位: wasmtime > node (with wasm wrapper) > wasmer
                if command -v wasmtime >/dev/null 2>&1; then
                    # wasmtimeを使用
                    run_with_timeout wasmtime "$wasm_file" > "$output_file" 2>&1 || exit_code=$?
                elif command -v node >/dev/null 2>&1; then
                    # nodeを使用（共有WASIラッパー。未生成なら生成する）
                    if [ -z "$WASM_NODE_WRAPPER" ] || [ ! -f "$WASM_NODE_WRAPPER" ]; then
                        setup_wasm_node_wrapper
                    fi
                    run_with_timeout node "$WASM_NODE_WRAPPER" "$wasm_file" > "$output_file" 2>&1 || exit_code=$?
                elif command -v wasmer >/dev/null 2>&1; then
                    # wasmerを使用
                    run_with_timeout wasmer run "$wasm_file" > "$output_file" 2>&1 || exit_code=$?
                else
                    # 起動時チェック通過後にランタイムが消えた異常事態はSKIPせず失敗させる
                    echo -e "${RED}[FAIL]${NC} $category/$test_name - No WASM runtime available (wasmtime/node/wasmer)"
                    ((FAILED++))
                    return
                fi
            fi
            ;;

        js)
            # JavaScriptバックエンドでコンパイルして実行
            # （カテゴリ・PID修飾: 同名テストや同時に走る別のrunner実行との衝突を防ぐ）
            local js_file="$TEMP_DIR/js_${category//\//_}_${test_name}_$$.js"
            rm -f "$js_file"

            # テストファイルのディレクトリに移動してコンパイル
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # JavaScript生成（エラー時は出力ファイルにエラーメッセージを書き込む）
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile --target=js -O$OPT_LEVEL $CACHE_OPTS "$test_basename" -o "$js_file" > "$output_file" 2>&1) || exit_code=$?

            if [ $exit_code -eq 0 ] && [ -f "$js_file" ]; then
                # Node.jsで実行
                # NODE_PATH: 生成JSは.tmp配下に置かれるため、テストディレクトリ同梱のnode_modules（FFIフィクスチャ）をフォールバック解決させる
                if command -v node >/dev/null 2>&1; then
                    run_with_timeout env NODE_PATH="$test_dir/node_modules" node "$js_file" > "$output_file" 2>&1 || exit_code=$?
                else
                    # 起動時チェック通過後にnodeが消えた異常事態はSKIPせず失敗させる
                    echo -e "${RED}[FAIL]${NC} $category/$test_name - Node.js not found"
                    ((FAILED++))
                    return
                fi
            fi
            ;;

        llvm-uefi)
            # UEFI ターゲットへのコンパイルのみ検証（実行不可）
            local uefi_obj="$TEMP_DIR/uefi_${test_name}.efi"
            rm -f "$uefi_obj"

            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # UEFI ターゲットでコンパイル（オブジェクト出力のみ）
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile --emit-llvm --target=uefi -O$OPT_LEVEL $CACHE_OPTS "$test_basename" -o "$uefi_obj" > "$output_file" 2>&1) || exit_code=$?

            # コンパイル成功 = PASS（実行はしない）
            if [ $exit_code -eq 0 ]; then
                # expectファイルが "COMPILE_OK" なら出力比較をスキップ
                if grep -q "COMPILE_OK" "$expect_file" 2>/dev/null; then
                    echo "COMPILE_OK" > "$output_file"
                fi
            fi
            rm -f "$uefi_obj"
            ;;

        llvm-baremetal)
            # ベアメタルターゲットへのコンパイルのみ検証（実行不可）
            local baremetal_obj="$TEMP_DIR/baremetal_${test_name}.o"
            rm -f "$baremetal_obj"

            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # ベアメタルターゲットでコンパイル（オブジェクト出力のみ）
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile --emit-llvm --target=baremetal-x86 -O$OPT_LEVEL $CACHE_OPTS "$test_basename" -o "$baremetal_obj" > "$output_file" 2>&1) || exit_code=$?

            # x86成功時はarmでもコンパイル検証する（armのみの起動コード生成経路がx86ゲートでは露見しないため。エラーテストはx86失敗時点で判定される）
            if [ $exit_code -eq 0 ]; then
                (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile --emit-llvm --target=baremetal-arm -O$OPT_LEVEL $CACHE_OPTS "$test_basename" -o "$baremetal_obj" > "$output_file" 2>&1) || exit_code=$?
            fi

            # コンパイル成功 = PASS（実行はしない）
            if [ $exit_code -eq 0 ]; then
                if grep -q "COMPILE_OK" "$expect_file" 2>/dev/null; then
                    echo "COMPILE_OK" > "$output_file"
                fi
            fi
            rm -f "$baremetal_obj"
            ;;

        sv)
            # SystemVerilog ターゲット: Cm→SV変換 + verilator lint検証
            local sv_file="$TEMP_DIR/sv_${test_name}.sv"
            rm -f "$sv_file"

            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # Stage 1: Cm → SV コンパイル
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile \
                --target=sv --test "$test_basename" -o "$sv_file" -O$OPT_LEVEL > "$output_file" 2>&1) || exit_code=$?

            if [ $exit_code -eq 0 ] && [ -f "$sv_file" ]; then
                # Stage 2: SVビルド検証 (Verilator or iverilog)
                if command -v verilator >/dev/null 2>&1; then
                    verilator --lint-only --timing -Wno-fatal "$sv_file" >> "$output_file" 2>&1
                    exit_code=$?
                    if [ $exit_code -ne 0 ]; then
                        echo "VERILATOR_LINT_FAIL" >> "$output_file"
                    fi
                elif command -v iverilog >/dev/null 2>&1; then
                    iverilog -g2012 -o /dev/null "$sv_file" >> "$output_file" 2>&1
                    exit_code=$?
                    if [ $exit_code -ne 0 ]; then
                        echo "IVERILOG_COMPILE_FAIL" >> "$output_file"
                    fi
                else
                    echo -e "${YELLOW}[WARN]${NC} verilator/iverilog not found, skip build verification"
                fi

                if [ $exit_code -eq 0 ] && grep -qE "^(SIM_OK|TEST |SIM_FAIL_EXPECTED)" "$expect_file" 2>/dev/null; then
                    # Stage 3: シミュレーション実行 (iverilog + vvp)
                    # expectファイルにSIM_OKまたはTEST行がある場合のみ実行
                    local tb_file="${sv_file%.sv}_tb.sv"
                    if [ -f "$tb_file" ] && command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1; then
                        local sim_binary="$TEMP_DIR/sim_${test_name}"
                        local sim_output="$TEMP_DIR/sim_${test_name}.log"
                        # iverilogでコンパイル
                        iverilog -g2012 -o "$sim_binary" "$sv_file" "$tb_file" >> "$output_file" 2>&1
                        if [ $? -eq 0 ]; then
                            # vvpでシミュレーション実行。テストベンチの $dumpfile は相対名のため、波形(.vcd)がリポジトリルートへ漏れないようTEMP_DIRをCWDにして実行する
                            (cd "$TEMP_DIR" && vvp "$sim_binary") > "$sim_output" 2>&1
                            local sim_exit=$?
                            if grep -q "SIM_FAIL_EXPECTED" "$expect_file" 2>/dev/null; then
                                # シミュレーション失敗を期待するテスト（assert不成立の$fatal等）
                                if [ $sim_exit -ne 0 ]; then
                                    echo "SIM_FAIL_EXPECTED" > "$output_file"
                                    exit_code=0
                                else
                                    echo "SIM_UNEXPECTED_PASS" > "$output_file"
                                    exit_code=1
                                fi
                            elif [ $sim_exit -eq 0 ] && grep -q "Test Complete" "$sim_output" 2>/dev/null; then
                                # シミュレーション成功: TEST行の検証
                                local sim_test_lines=$(grep "^TEST " "$sim_output" 2>/dev/null)
                                local expect_test_lines=$(grep "^TEST " "$expect_file" 2>/dev/null)

                                if [ -n "$expect_test_lines" ]; then
                                    # 期待値が.expectにある場合: 比較検証
                                    local sim_test_file="$TEMP_DIR/sim_test_${test_name}.txt"
                                    local exp_test_file="$TEMP_DIR/exp_test_${test_name}.txt"
                                    grep "^TEST " "$sim_output" > "$sim_test_file" 2>/dev/null
                                    grep "^TEST " "$expect_file" > "$exp_test_file" 2>/dev/null

                                    if diff -q "$exp_test_file" "$sim_test_file" > /dev/null 2>&1; then
                                        # TEST行が完全一致: SIM_OK + TEST行を出力
                                        echo "SIM_OK" > "$output_file"
                                        cat "$sim_test_file" >> "$output_file"
                                    else
                                        # 値不一致: SIM_FAIL
                                        echo "SIM_FAIL" > "$output_file"
                                        echo "--- 期待値 ---" >> "$output_file"
                                        cat "$exp_test_file" >> "$output_file"
                                        echo "--- 実際の値 ---" >> "$output_file"
                                        cat "$sim_test_file" >> "$output_file"
                                        exit_code=1
                                    fi
                                elif grep -q "SIM_OK" "$expect_file" 2>/dev/null; then
                                    echo "SIM_OK" > "$output_file"
                                elif grep -q "COMPILE_OK" "$expect_file" 2>/dev/null; then
                                    echo "COMPILE_OK" > "$output_file"
                                fi
                            else
                                echo "SIM_FAIL" >> "$output_file"
                                cat "$sim_output" >> "$output_file" 2>/dev/null
                                exit_code=1
                            fi
                        fi
                    else
                        # シミュレーションツール未対応の場合はコンパイルOKとして処理
                        if grep -q "COMPILE_OK" "$expect_file" 2>/dev/null; then
                            echo "COMPILE_OK" > "$output_file"
                        fi
                    fi
                elif [ $exit_code -eq 0 ]; then
                    # SIM_OK/TEST行なし: COMPILE_OKとして処理
                    if grep -q "COMPILE_OK" "$expect_file" 2>/dev/null; then
                        echo "COMPILE_OK" > "$output_file"
                    fi
                fi
            fi
            rm -f "$sv_file"
            ;;
    esac

    # タイムアウト処理
    if [ $exit_code -eq 124 ] || [ $exit_code -eq 143 ]; then
        echo -e "${RED}[FAIL]${NC} $category/$test_name - Timeout (>${test_timeout}s)"
        ((FAILED++))
        return
    fi

    # .errorファイルがある場合（エラーテスト）
    if [ "$is_error_test" = true ]; then
        # エラーが期待される（非ゼロexit codeならPASS）
        # macOS CIではPython timeout wrapperによりexit codeが変わる場合がある
        if [ $exit_code -ne 0 ]; then
            echo -e "${GREEN}[PASS]${NC} $category/$test_name"
            ((PASSED++))
        else
            echo -e "${RED}[FAIL]${NC} $category/$test_name - Expected error but succeeded"
            ((FAILED++))
        fi
    # エラーファイルに期待される出力がある場合（コンパイルエラーテスト等）
    elif grep -q "error\|Error\|エラー" "$expect_file" 2>/dev/null; then
        # エラーが期待される場合
        if [ $exit_code -ne 0 ]; then
            # エラー出力を比較
            if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
                echo -e "${GREEN}[PASS]${NC} $category/$test_name"
                ((PASSED++))
            else
                echo -e "${RED}[FAIL]${NC} $category/$test_name - Error output mismatch"
                echo "  Expected:"
                head -n 5 "$expect_file" 2>/dev/null | sed 's/^/    /'
                echo "  Got:"
                head -n 5 "$output_file" 2>/dev/null | sed 's/^/    /'
                ((FAILED++))
            fi
        else
            echo -e "${RED}[FAIL]${NC} $category/$test_name - Expected error but succeeded"
            ((FAILED++))
        fi
    else
        # 正常終了が期待される場合
        if [ $exit_code -ne 0 ]; then
            echo -e "${RED}[FAIL]${NC} $category/$test_name - Runtime error (exit code: $exit_code)"
            echo "  Output (first 10 lines):"
            head -n 10 "$output_file" 2>/dev/null | sed 's/^/    /'
            ((FAILED++))
        else
            # 出力を比較
            if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
                echo -e "${GREEN}[PASS]${NC} $category/$test_name"
                ((PASSED++))
            else
                echo -e "${RED}[FAIL]${NC} $category/$test_name - Output mismatch"
                echo "  Expected:"
                head -n 5 "$expect_file" 2>/dev/null | sed 's/^/    /'
                echo "  Got:"
                head -n 5 "$output_file" 2>/dev/null | sed 's/^/    /'
                echo "  Diff:"
                diff -u "$expect_file" "$output_file" 2>/dev/null | head -n 15 | sed 's/^/    /'
                ((FAILED++))
            fi
        fi
    fi
}
