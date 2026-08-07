#!/bin/bash
# unified_test_runner.sh から source される実行ドライバモジュール。
# 順次実行（run_tests_sequential）・並列実行（run_tests_parallel）・並列ワーカー（run_parallel_test）を提供する。

# 順次実行モード
run_tests_sequential() {
    for entry in $CATEGORIES; do
        # platform:category フォーマットをパース
        local platform_dir="${entry%%:*}"
        local category="${entry##*:}"
        local category_dir
        if [ "$platform_dir" = "sv" ]; then
            category_dir="$PROJECT_ROOT/tests/sv/$category"
        else
            category_dir="$PROGRAMS_DIR/$platform_dir/$category"
        fi

        if [ ! -d "$category_dir" ]; then
            log "Warning: Category directory '$platform_dir/$category' not found, skipping"
            continue
        fi

        log "Testing category: $platform_dir/$category"
        log "----------------------------------------"

        for test_file in "$category_dir"/*.cm; do
            if [ -f "$test_file" ]; then
                ((TOTAL++))
                run_single_test "$test_file"
            fi
        done

        log ""
    done
}

# 並列実行モード
run_tests_parallel() {
    local test_files=()
    local results_dir="$TEMP_DIR/parallel_results"
    mkdir -p "$results_dir"
    
    # 全テストファイルを収集
    for entry in $CATEGORIES; do
        local platform_dir="${entry%%:*}"
        local category="${entry##*:}"
        local category_dir
        if [ "$platform_dir" = "sv" ]; then
            category_dir="$PROJECT_ROOT/tests/sv/$category"
        else
            category_dir="$PROGRAMS_DIR/$platform_dir/$category"
        fi
        if [ -d "$category_dir" ]; then
            for test_file in "$category_dir"/*.cm; do
                if [ -f "$test_file" ]; then
                    test_files+=("$test_file")
                fi
            done
        fi
    done
    
    TOTAL=${#test_files[@]}
    log "Running $TOTAL tests in parallel..."
    log ""
    
    # 並列ジョブ数（CPU数×4: コンパイル+リンク+実行でI/Oバウンドのため高並列度が有効）
    # 環境変数CM_TEST_MAX_JOBSでオーバーライド可能
    local ncpu=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
    local max_jobs=${CM_TEST_MAX_JOBS:-$((ncpu * 4))}
    
    # テスト引数を事前計算（ループ内のfork-execオーバーヘッドを除去）
    local result_files=()
    for test_file in "${test_files[@]}"; do
        local test_name="$(basename "${test_file%.cm}")"
        local category="$(dirname "$test_file" | sed "s|^$PROGRAMS_DIR/||")"
        result_files+=("$results_dir/${category//\//_}_${test_name}.result")
    done
    
    # FIFOセマフォ: ポーリングなしで効率的にスロットを管理
    local sem_fifo="$TEMP_DIR/parallel_sem_$$"
    mkfifo "$sem_fifo"
    exec 9<>"$sem_fifo"
    rm -f "$sem_fifo"
    
    # スロット数分のトークンを投入
    for ((i=0; i<max_jobs; i++)); do
        echo "x" >&9
    done
    
    # 各テストを並列実行（FIFOセマフォでスロット制御、ループ内外部コマンドなし）
    local idx=0
    for test_file in "${test_files[@]}"; do
        read -u 9 _token
        
        (
            run_parallel_test "$test_file" "${result_files[$idx]}"
            echo "x" >&9
        ) &
        CHILD_PIDS+=($!)
        ((idx++))
    done
    
    # 全ジョブの完了を待機
    wait
    exec 9>&-
    
    # 結果を集計
    for test_file in "${test_files[@]}"; do
        local test_name="$(basename "${test_file%.cm}")"
        # programs/以下の相対パスをカテゴリとして使用（例: common/thread）
        local category="$(dirname "$test_file" | sed "s|^$PROGRAMS_DIR/||")"
        local result_file="$results_dir/${category//\//_}_${test_name}.result"
        
        if [ -f "$result_file" ]; then
            local result=$(cat "$result_file")
            case "$result" in
                PASS*)
                    echo -e "${GREEN}[PASS]${NC} $category/$test_name"
                    ((PASSED++))
                    ;;
                FAIL*)
                    local reason="${result#FAIL:}"
                    echo -e "${RED}[FAIL]${NC} $category/$test_name - $reason"
                    # エラーファイルがあれば先頭5行を表示
                    local error_file="${result_file}.error"
                    if [ -f "$error_file" ]; then
                        echo "  --- Error output (first 15 lines) ---"
                        head -15 "$error_file" | sed 's/^/  /'
                        echo "  ---"
                    fi
                    ((FAILED++))
                    ;;
                SKIP*)
                    local reason="${result#SKIP:}"
                    echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - $reason"
                    ((SKIPPED++))
                    ;;
            esac
        else
            echo -e "${RED}[FAIL]${NC} $category/$test_name - No result file"
            ((FAILED++))
        fi
    done
}

# 並列テスト実行（子プロセス用）
run_parallel_test() {
    local test_file="$1"
    local result_file="$2"
    local test_name="$(basename "${test_file%.cm}")"
    # programs/以下の相対パスをカテゴリとして使用（例: common/thread）
    local category="$(dirname "$test_file" | sed "s|^$PROGRAMS_DIR/||")"
    local expect_file="${test_file%.cm}.expect"
    local backend_expect_file="${test_file%.cm}.expect.${BACKEND}"
    local error_expect_file="${test_file%.cm}.error"
    local backend_error_file="${test_file%.cm}.error.${BACKEND}"
    local output_file="$TEMP_DIR/${category//\//_}_${test_name}_${BASHPID}_${RANDOM}.out"
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
        echo "SKIP:$platform_skip_reason" > "$result_file"
        return
    fi

    # .skipファイルのチェック
    local skip_file="${test_file%.cm}.skip"
    local category_skip_file="$(dirname "$test_file")/.skip"
    local current_os=$(uname -s | tr '[:upper:]' '[:lower:]')
    local current_arch=$(uname -m)
    local current_opt="o${OPT_LEVEL:-3}"

    # スキップパターンマッチング関数（並列版）
    match_skip_pattern_parallel() {
        local pattern="$1"
        local backend="$2"
        local opt="$3"
        local os="$4"
        local arch="$5"

        local p_backend p_opt p_os p_arch
        if [[ "$pattern" =~ ^([a-z-]+)(-o[0-3])?(:([a-z]+))?(:([a-z0-9_]+))?$ ]]; then
            p_backend="${BASH_REMATCH[1]}"
            p_opt="${BASH_REMATCH[2]#-}"
            p_os="${BASH_REMATCH[4]}"
            p_arch="${BASH_REMATCH[6]}"
        else
            if [[ "$pattern" =~ ^([a-z-]+):([a-z]+)$ ]]; then
                p_backend="${BASH_REMATCH[1]}"
                p_os="${BASH_REMATCH[2]}"
            else
                p_backend="$pattern"
            fi
        fi

        [[ "$p_backend" != "$backend" ]] && return 1
        [[ -n "$p_opt" && "$p_opt" != "$opt" ]] && return 1
        [[ -n "$p_os" && "$p_os" != "$os" ]] && return 1
        [[ -n "$p_arch" && "$p_arch" != "$arch" ]] && return 1
        return 0
    }

    # ファイル固有の.skipファイルがある場合
    if [ -f "$skip_file" ]; then
        if [ -s "$skip_file" ]; then
            local has_pattern=0
            while IFS= read -r line || [[ -n "$line" ]]; do
                [[ "$line" =~ ^[[:space:]]*# ]] && continue
                [[ -z "${line// }" ]] && continue
                line="${line%%#*}"
                line="${line// /}"
                has_pattern=1

                if match_skip_pattern_parallel "$line" "$BACKEND" "$current_opt" "$current_os" "$current_arch"; then
                    echo "SKIP:Skipped for $line" > "$result_file"
                    return
                fi
            done < "$skip_file"
            # パターン行がなくコメント（理由）のみの場合、全バックエンドでスキップ
            if [ "$has_pattern" -eq 0 ]; then
                local skip_reason=$(grep -m1 '^[[:space:]]*#' "$skip_file" | sed 's/^[[:space:]]*#[[:space:]]*//')
                echo "SKIP:${skip_reason:-理由未記載}" > "$result_file"
                return
            fi
        else
            # ファイルが空の場合、すべてのバックエンドでスキップ
            echo "SKIP:Skip file exists (理由未記載)" > "$result_file"
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

                if match_skip_pattern_parallel "$line" "$BACKEND" "$current_opt" "$current_os" "$current_arch"; then
                    echo "SKIP:Category skipped for $line" > "$result_file"
                    return
                fi
            done < "$category_skip_file"
        else
            echo "SKIP:Category skip file exists" > "$result_file"
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

    # バックエンド固有のexpectファイルがあれば優先
    if [ -f "$backend_expect_file" ]; then
        expect_file="$backend_expect_file"
    fi

    # expectファイルもerrorファイルもない場合はスキップ
    if [ ! -f "$expect_file" ] && [ ! -f "$error_expect_file" ]; then
        echo "SKIP:No expect/error file" > "$result_file"
        return
    fi

    local exit_code=0

    # タイムアウト付き実行
    run_with_timeout_silent() {
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
                $TIMEOUT_CMD --kill-after=2 "$test_timeout" "$@"
            fi
        else
            "$@"
        fi
    }

    case "$BACKEND" in
        interpreter|jit)
            # テストファイルのディレクトリに移動して実行（モジュールの相対パス解決のため）
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" run -O$OPT_LEVEL $CACHE_OPTS "$test_basename" > "$output_file" 2>&1) || exit_code=$?
            ;;
        llvm)
            # テストファイルのディレクトリに移動してコンパイル（モジュールの相対パス解決のため）
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            local llvm_exec="$TEMP_DIR/llvm_${category//\//_}_${test_name}_${BASHPID}"
            (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" compile --emit-llvm -O$OPT_LEVEL $CACHE_OPTS "$test_basename" -o "$llvm_exec" > "$output_file" 2>&1) || exit_code=$?
            if [ $exit_code -eq 0 ] && [ -f "$llvm_exec" ]; then
                # テストディレクトリで実行（タイムアウト付き）
                (cd "$test_dir" && run_with_timeout_silent "$llvm_exec" > "$output_file" 2>&1) || exit_code=$?
                
                # セグフォ時にgdbでデバッグ情報を取得（CI環境のみ）
                if [ $exit_code -eq 139 ] && [ -n "$CI" ] && command -v gdb >/dev/null 2>&1; then
                    echo "=== Segmentation fault detected, running gdb ===" >> "$output_file"
                    echo "run" | gdb --batch -ex "set pagination off" -ex "run" -ex "bt" -ex "quit" "$llvm_exec" >> "$output_file" 2>&1 || true
                fi
                
                rm -f "$llvm_exec"
            fi
            ;;
        llvm-wasm)
            local wasm_file="$TEMP_DIR/wasm_${category//\//_}_${test_name}_${BASHPID}.wasm"
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" compile --emit-llvm --target=wasm -O$OPT_LEVEL $CACHE_OPTS "$test_basename" -o "$wasm_file" > "$output_file" 2>&1) || exit_code=$?
            if [ $exit_code -eq 0 ] && [ -f "$wasm_file" ]; then
                if command -v wasmtime >/dev/null 2>&1; then
                    run_with_timeout_silent wasmtime run --dir=. "$wasm_file" > "$output_file" 2>&1 || exit_code=$?
                elif [ -n "$WASM_NODE_WRAPPER" ] && [ -f "$WASM_NODE_WRAPPER" ] && command -v node >/dev/null 2>&1; then
                    # wasmtimeが無い環境: 共有nodeラッパーで実行（旧: 静かにSKIPされ
                    # スイートが緑のまま素通りしていた）
                    run_with_timeout_silent node "$WASM_NODE_WRAPPER" "$wasm_file" > "$output_file" 2>&1 || exit_code=$?
                elif command -v wasmer >/dev/null 2>&1; then
                    run_with_timeout_silent wasmer run "$wasm_file" > "$output_file" 2>&1 || exit_code=$?
                else
                    # 起動時チェックを通過した後にランタイムが消えた異常事態。
                    # SKIPにすると無検証で緑になるため明示的に失敗させる
                    echo "FAIL:No WASM runtime available (wasmtime/node/wasmer)" > "$result_file"
                    rm -f "$wasm_file"
                    return
                fi
                rm -f "$wasm_file"
            fi
            ;;
        js)
            # カテゴリ・ワーカーPIDで修飾する（basenameのみだと同名テスト（basic等）が
            # 並列実行時に同じファイルを取り合い、他テストのコードを実行してしまう。
            # $$は全ワーカー共通の親PIDのため一意にならない。llvm/wasm/svと同じ方式）
            local js_file="$TEMP_DIR/js_${category//\//_}_${test_name}_${BASHPID}_${RANDOM}.js"
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" compile --target=js -O$OPT_LEVEL $CACHE_OPTS "$test_basename" -o "$js_file" > "$output_file" 2>&1) || exit_code=$?
            if [ $exit_code -eq 0 ] && [ ! -f "$js_file" ]; then
                # コンパイル成功なのに生成物が無い場合を「空出力のOutput mismatch」と
                # 混同しないよう明示的に失敗として区別する
                echo "FAIL:JS file missing after successful compile" > "$result_file"
                return
            fi
            if [ $exit_code -eq 0 ] && [ -f "$js_file" ]; then
                if command -v node >/dev/null 2>&1; then
                    # NODE_PATH: テストディレクトリ同梱のnode_modules（FFIフィクスチャ）を解決させる（直列実行側と同一の挙動）
                    run_with_timeout_silent env NODE_PATH="$test_dir/node_modules" node "$js_file" > "$output_file" 2>&1 || exit_code=$?
                    # nodeプロセスがゾンビ化する場合に備えてクリーンアップ
                    kill %% 2>/dev/null || true
                else
                    # 起動時チェック通過後にnodeが消えた異常事態はSKIPせず失敗させる
                    echo "FAIL:Node.js not found" > "$result_file"
                    rm -f "$js_file"
                    return
                fi
                rm -f "$js_file"
            fi
            ;;
        llvm-uefi)
            # UEFI ターゲットへのコンパイルのみ検証
            local uefi_obj="$TEMP_DIR/uefi_${category//\//_}_${test_name}_${BASHPID}_${RANDOM}.efi"
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" compile --emit-llvm --target=uefi -O$OPT_LEVEL $CACHE_OPTS "$test_basename" -o "$uefi_obj" > "$output_file" 2>&1) || exit_code=$?
            if [ $exit_code -eq 0 ]; then
                if grep -q "COMPILE_OK" "$expect_file" 2>/dev/null; then
                    echo "COMPILE_OK" > "$output_file"
                fi
            fi
            rm -f "$uefi_obj"
            ;;
        llvm-baremetal)
            # ベアメタルターゲットへのコンパイルのみ検証
            local baremetal_obj="$TEMP_DIR/baremetal_${category//\//_}_${test_name}_${BASHPID}_${RANDOM}.o"
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" compile --emit-llvm --target=baremetal-x86 -O$OPT_LEVEL $CACHE_OPTS "$test_basename" -o "$baremetal_obj" > "$output_file" 2>&1) || exit_code=$?
            # x86成功時はarmでもコンパイル検証する（armのみの起動コード生成経路がx86ゲートでは露見しないため。エラーテストはx86失敗時点で判定される）
            if [ $exit_code -eq 0 ]; then
                (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" compile --emit-llvm --target=baremetal-arm -O$OPT_LEVEL $CACHE_OPTS "$test_basename" -o "$baremetal_obj" > "$output_file" 2>&1) || exit_code=$?
            fi
            if [ $exit_code -eq 0 ]; then
                if grep -q "COMPILE_OK" "$expect_file" 2>/dev/null; then
                    echo "COMPILE_OK" > "$output_file"
                fi
            fi
            rm -f "$baremetal_obj"
            ;;
        sv)
            # SystemVerilog ターゲット: Cm→SV変換 + verilator lint検証（並列版）
            local sv_file="$TEMP_DIR/sv_${test_name}_${BASHPID}_${RANDOM}.sv"
            rm -f "$sv_file"

            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # Stage 1: Cm → SV コンパイル
            (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" compile \
                --target=sv --test "$test_basename" -o "$sv_file" -O$OPT_LEVEL > "$output_file" 2>&1) || exit_code=$?

            if [ $exit_code -eq 0 ] && [ -f "$sv_file" ]; then
                # Stage 2: SVビルド検証 (Verilator or iverilog)
                if command -v verilator >/dev/null 2>&1; then
                    verilator --lint-only --timing -Wno-fatal "$sv_file" >> "$output_file" 2>&1
                    exit_code=$?
                elif command -v iverilog >/dev/null 2>&1; then
                    iverilog -g2012 -o /dev/null "$sv_file" >> "$output_file" 2>&1
                    exit_code=$?
                fi

                if [ $exit_code -eq 0 ] && grep -qE "^(SIM_OK|TEST |SIM_FAIL_EXPECTED)" "$expect_file" 2>/dev/null; then
                    # Stage 3: シミュレーション実行 (iverilog + vvp)
                    # expectファイルにSIM_OKまたはTEST行がある場合のみ実行
                    local tb_file="${sv_file%.sv}_tb.sv"
                    if [ -f "$tb_file" ] && command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1; then
                        local sim_binary="$TEMP_DIR/sim_${test_name}_${BASHPID}_${RANDOM}"
                        local sim_output="$TEMP_DIR/sim_${test_name}_${BASHPID}_${RANDOM}.log"
                        iverilog -g2012 -o "$sim_binary" "$sv_file" "$tb_file" >> "$output_file" 2>&1
                        if [ $? -eq 0 ]; then
                            vvp "$sim_binary" > "$sim_output" 2>&1
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
                                local sim_test_lines=$(grep "^TEST " "$sim_output" 2>/dev/null)
                                local expect_test_lines=$(grep "^TEST " "$expect_file" 2>/dev/null)
                                if [ -n "$expect_test_lines" ]; then
                                    local sim_test_file="$TEMP_DIR/sim_test_${test_name}_${BASHPID}.txt"
                                    local exp_test_file="$TEMP_DIR/exp_test_${test_name}_${BASHPID}.txt"
                                    grep "^TEST " "$sim_output" > "$sim_test_file" 2>/dev/null
                                    grep "^TEST " "$expect_file" > "$exp_test_file" 2>/dev/null
                                    if diff -q "$exp_test_file" "$sim_test_file" > /dev/null 2>&1; then
                                        echo "SIM_OK" > "$output_file"
                                        cat "$sim_test_file" >> "$output_file"
                                    else
                                        echo "SIM_FAIL" > "$output_file"
                                        echo "--- 期待値 ---" >> "$output_file"
                                        cat "$exp_test_file" >> "$output_file" 2>/dev/null
                                        echo "--- 実際の値 ---" >> "$output_file"
                                        cat "$sim_test_file" >> "$output_file" 2>/dev/null
                                        exit_code=1
                                    fi
                                    rm -f "$sim_test_file" "$exp_test_file"
                                elif grep -q "SIM_OK" "$expect_file" 2>/dev/null; then
                                    echo "SIM_OK" > "$output_file"
                                elif grep -q "COMPILE_OK" "$expect_file" 2>/dev/null; then
                                    echo "COMPILE_OK" > "$output_file"
                                fi
                            else
                                echo "SIM_FAIL (vvp exit=$sim_exit)" >> "$output_file"
                                cat "$sim_output" >> "$output_file" 2>/dev/null
                                exit_code=1
                            fi
                        fi
                        rm -f "$sim_binary" "$sim_output"
                    else
                        # シミュレーションツール未対応: コンパイルOKとして処理
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
        *)
            echo "SKIP:Backend not supported for parallel" > "$result_file"
            return
            ;;
    esac

    # タイムアウト
    if [ $exit_code -eq 124 ] || [ $exit_code -eq 143 ]; then
        echo "FAIL:Timeout (>${test_timeout}s)" > "$result_file"
        rm -f "$output_file"
        return
    fi

    # 結果比較
    # .errorファイルがある場合（エラーテスト）
    if [ "$is_error_test" = true ]; then
        # エラーが期待される（非ゼロexit codeならPASS）
        # macOS CIではPython timeout wrapperによりexit codeが変わる場合がある
        if [ $exit_code -ne 0 ]; then
            echo "PASS" > "$result_file"
        else
            echo "FAIL:Expected error but succeeded" > "$result_file"
        fi
    # エラーファイルに期待される出力がある場合（コンパイルエラーテスト等）
    elif grep -q "error\|Error\|エラー" "$expect_file" 2>/dev/null; then
        if [ $exit_code -ne 0 ]; then
            if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
                echo "PASS" > "$result_file"
            else
                echo "FAIL:Error output mismatch" > "$result_file"
                # 差分を保存（集計時に表示され、CIログから原因を特定できるようにする）
                diff -u "$expect_file" "$output_file" > "${result_file}.error" 2>/dev/null || true
            fi
        else
            echo "FAIL:Expected error but succeeded" > "$result_file"
        fi
    else
        if [ $exit_code -ne 0 ]; then
            # ランタイムエラー: エラー出力を保存
            echo "FAIL:Runtime error (exit code: $exit_code)" > "$result_file"
            # エラー出力を別ファイルに保存（デバッグ用）
            if [ -f "$output_file" ]; then
                cat "$output_file" > "${result_file}.error" 2>/dev/null || true
            fi
        else
            if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
                echo "PASS" > "$result_file"
            else
                echo "FAIL:Output mismatch" > "$result_file"
                # 差分を保存（集計時に表示され、CIログから原因を特定できるようにする）
                diff -u "$expect_file" "$output_file" > "${result_file}.error" 2>/dev/null || true
            fi
        fi
    fi

    rm -f "$output_file"
}
