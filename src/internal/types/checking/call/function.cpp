// ============================================================
// TypeChecker 実装 - 関数呼び出し（ビルトイン・ジェネリック・関数ポインタ・静的メソッド・enumコンストラクタ）の型推論
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/types/type_checker.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace cm {

ast::TypePtr TypeChecker::infer_call(ast::CallExpr& call) {
    if (auto* ident = call.callee->as<ast::IdentExpr>()) {
        // 関数ポインタ・ラムダを保持する変数経由の呼び出しを使用としてマークする（W001未使用の誤検出防止。関数名の場合はlookup対象外なので影響しない）
        scopes_.current().mark_used(ident->name);

        // R7: #[deprecated]が付いた関数の呼び出しを警告する
        if (deprecated_functions_.count(ident->name) > 0) {
            warning(current_span_, i18n::msgf(i18n::MsgId::TcDeprecatedFunctionCall, ident->name));
        }

        // __asm__ / __llvm__ intrinsic - インラインアセンブリ
        // __asm__: ネイティブアセンブリ（x86, ARM64等）- 推奨
        // __llvm__: 後方互換性のため残す（将来はLLVM IR対応予定）
        if (ident->name == "__asm__" || ident->name == "__llvm__") {
            if (call.args.size() != 1) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcRequiresExactly1ArgumentAssembly, ident->name));
                return ast::make_error();
            }
            // 引数が文字列リテラルであることを確認
            if (auto* lit = call.args[0]->as<ast::LiteralExpr>()) {
                if (!std::holds_alternative<std::string>(lit->value)) {
                    error(current_span_,
                          i18n::msgf(i18n::MsgId::TcArgumentMustStringLiteral, ident->name));
                    return ast::make_error();
                }
                // ${制約:変数名} で参照される変数を使用・変更・初期化済みとしてマークする（=r/+r制約はasmが書き込むため、Lintの誤検出を防ぐ）
                const std::string& asm_text = std::get<std::string>(lit->value);
                static const std::regex asm_operand_re(R"(\$\{[^:}]*:([A-Za-z_][A-Za-z0-9_]*)\})");
                for (std::sregex_iterator it(asm_text.begin(), asm_text.end(), asm_operand_re), end;
                     it != end; ++it) {
                    const std::string var_name = (*it)[1].str();
                    scopes_.current().mark_used(var_name);
                    mark_variable_modified(var_name);
                    mark_variable_initialized(var_name);
                }
            } else {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcArgumentMustStringLiteral, ident->name));
                return ast::make_error();
            }
            return ast::make_void();
        }

        // 組み込み関数の特別処理（printlnはstd::io::printlnからインポート推奨だが互換性のため残す）
        if (ident->name == "println" || ident->name == "print") {
            // println() は引数なしでも許可（空行出力）
            if (ident->name == "print" && call.args.empty()) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcRequiresAtLeast1Argument, ident->name));
                return ast::make_error();
            }
            if (call.args.size() > 1) {
                error(current_span_, i18n::msgf(i18n::MsgId::TcTakesOnly1Argument, ident->name,
                                                std::to_string(call.args.size())));
                return ast::make_error();
            }

            for (auto& arg : call.args) {
                infer_type(*arg);
            }

            // 補間プレースホルダのスコープ・move検査は、infer_literalの脱糖（desugar_interpolation_parts）後の通常推論が行う（第4段b）

            return ast::make_void();
        }

        // ジェネリック関数かチェック
        auto gen_it = generic_functions_.find(ident->name);
        if (gen_it != generic_functions_.end()) {
            return infer_generic_call(call, ident->name, gen_it->second);
        }

        // 明示的型引数付きジェネリック関数呼び出し: size_of<WorkerArg>()
        // パーサーが "size_of<WorkerArg>" という識別子名を生成するケース
        size_t lt_pos = ident->name.find('<');
        if (lt_pos != std::string::npos && ident->name.back() == '>') {
            std::string base_name = ident->name.substr(0, lt_pos);
            auto base_gen_it = generic_functions_.find(base_name);
            if (base_gen_it != generic_functions_.end()) {
                // 型引数文字列を抽出: "WorkerArg" from "size_of<WorkerArg>"
                std::string type_args_str = ident->name.substr(lt_pos + 1);
                type_args_str =
                    type_args_str.substr(0, type_args_str.size() - 1);  // 末尾の > を削除

                // 型引数をパースしてcallに設定
                std::vector<ast::TypePtr> explicit_type_args;
                std::istringstream iss(type_args_str);
                std::string type_arg_name;
                while (std::getline(iss, type_arg_name, ',')) {
                    // 空白をトリム
                    size_t start = type_arg_name.find_first_not_of(" ");
                    size_t end = type_arg_name.find_last_not_of(" ");
                    if (start != std::string::npos && end != std::string::npos) {
                        type_arg_name = type_arg_name.substr(start, end - start + 1);
                    }
                    explicit_type_args.push_back(ast::make_named(type_arg_name));
                }

                // H15: 明示的型引数の個数がジェネリックパラメータ数と一致するか検証する
                if (explicit_type_args.size() != base_gen_it->second.size()) {
                    error(current_span_,
                          i18n::msgf(i18n::MsgId::TypeGenericFunctionArgumentCountMismatch,
                                     base_name, std::to_string(base_gen_it->second.size()),
                                     std::to_string(explicit_type_args.size())));
                }

                // 明示的型引数を設定
                call.ordered_type_args = explicit_type_args;
                std::unordered_map<std::string, ast::TypePtr> inferred;
                for (size_t i = 0; i < base_gen_it->second.size() && i < explicit_type_args.size();
                     ++i) {
                    inferred[base_gen_it->second[i]] = explicit_type_args[i];
                }
                call.inferred_type_args = inferred;

                // 呼び出し名をベース名に変更してモノモーフィゼーションエンジンに委ねる
                ident->name = base_name;
                return infer_generic_call(call, base_name, base_gen_it->second);
            }
        }

        // 構造体のコンストラクタ呼び出しかチェック
        if (get_struct(ident->name) != nullptr) {
            for (auto& arg : call.args) {
                infer_type(*arg);
            }
            return ast::make_named(ident->name);
        }

        // exit(code) ビルトイン: プロセスを終了する（HIRビルトインとして各バックエンドが処理する。std::debug::assert等が使用）
        if (ident->name == "exit") {
            if (call.args.size() != 1) {
                error(current_span_, i18n::msg(i18n::MsgId::TypeExitMustBeUsedAs));
                return ast::make_void();
            }
            auto code_type = infer_type(*call.args[0]);
            if (code_type && code_type->kind != ast::TypeKind::Bool && !code_type->is_integer()) {
                error(current_span_, i18n::msg(i18n::MsgId::TypeTheExitCodeForExit));
            }
            return ast::make_void();
        }

        // step(n): テスト関数（#[test]、SVプラットフォーム）専用の組み込み。
        // nクロック進める（SVでは repeat(n) @(posedge clk) に変換される）
        if (ident->name == "step") {
            if (call.args.size() != 1) {
                error(current_span_, i18n::msg(i18n::MsgId::TypeStepMustBeUsedAs));
                return ast::make_void();
            }
            auto step_arg = infer_type(*call.args[0]);
            if (!step_arg || !step_arg->is_integer()) {
                error(current_span_, i18n::msg(i18n::MsgId::TypeTheArgumentToStepMust));
            }
            return ast::make_void();
        }

        // SVバックエンド用ビルトイン関数のバイパス
        if (ident->name == "__builtin_concat" || ident->name == "__builtin_replicate") {
            if (ident->name == "__builtin_replicate") {
                // __builtin_replicate(count, expr): count * expr のビット幅
                ast::TypePtr result_type = nullptr;
                int64_t count = 1;
                for (size_t i = 0; i < call.args.size(); ++i) {
                    auto t = infer_type(*call.args[i]);
                    if (i == 0) {
                        // 最初の引数は繰り返し回数
                        if (auto* lit = call.args[i]->as<ast::LiteralExpr>()) {
                            if (auto* ival = std::get_if<int64_t>(&lit->value)) {
                                count = *ival;
                            }
                        }
                    } else if (i == 1) {
                        // 2番目の引数が複製対象
                        if (t && t->kind == ast::TypeKind::Array && t->element_type &&
                            t->element_type->kind == ast::TypeKind::Bit && t->array_size) {
                            // bit[N] → bit[N * count]
                            uint32_t new_size = static_cast<uint32_t>(*t->array_size * count);
                            result_type = ast::make_array(ast::make_bit(), new_size);
                        } else if (t && t->kind == ast::TypeKind::Bit) {
                            // 単一bit → bit[count]
                            result_type =
                                ast::make_array(ast::make_bit(), static_cast<uint32_t>(count));
                        } else {
                            result_type = t;
                        }
                    }
                }
                return result_type ? result_type : ast::make_void();
            } else {
                // __builtin_concat: 全引数のビット幅を合算
                std::vector<ast::TypePtr> arg_types;
                uint32_t total_bits = 0;
                bool all_bit_types = true;

                for (auto& arg : call.args) {
                    auto t = infer_type(*arg);
                    arg_types.push_back(t);
                    if (t && t->kind == ast::TypeKind::Array && t->element_type &&
                        t->element_type->kind == ast::TypeKind::Bit && t->array_size) {
                        // bit[N] 型
                        total_bits += *t->array_size;
                    } else if (t && t->kind == ast::TypeKind::Bit) {
                        // 単一bit
                        total_bits += 1;
                    } else {
                        all_bit_types = false;
                    }
                }

                if (call.args.empty()) {
                    // 空の連接は void (または 0ビット)
                    return ast::make_void();
                }

                if (all_bit_types && total_bits > 0) {
                    // bit[N] 同士の連接 → bit[合計ビット幅]
                    return ast::make_array(ast::make_bit(), total_bits);
                }

                // それ以外は最初の引数の型をフォールバック
                return arg_types.empty() ? ast::make_void() : arg_types[0];
            }
        }

        // 通常の関数はシンボルテーブルから検索
        auto sym = scopes_.current().lookup(ident->name);
        if (!sym && !current_namespace_.empty() && ident->name.find("::") == std::string::npos) {
            // 名前空間内の非修飾呼び出しは「現在の名前空間::名前」として解決する（内側から外側へ探索。解決できた場合は呼び出し名を修飾名へ書き換え、HIR/コード生成が一貫した名前を見るようにする）
            std::string ns = current_namespace_;
            while (!ns.empty()) {
                std::string qualified = ns + "::" + ident->name;
                if (auto ns_sym = scopes_.current().lookup(qualified)) {
                    if (ns_sym->is_function) {
                        ident->name = qualified;
                        sym = ns_sym;
                        break;
                    }
                }
                auto pos = ns.rfind("::");
                if (pos == std::string::npos) {
                    break;
                }
                ns = ns.substr(0, pos);
            }
        }
        if (!sym) {
            // 静的メソッド呼び出しの可能性をチェック: Type::method
            size_t last_colon = ident->name.rfind("::");
            if (last_colon != std::string::npos) {
                std::string type_name = ident->name.substr(0, last_colon);
                std::string method_name = ident->name.substr(last_colon + 2);

                // 型名からジェネリック型パラメータを抽出（Vec<int>など）
                // まず直接検索を試みる
                auto it = type_methods_.find(type_name);
                if (it == type_methods_.end()) {
                    // ジェネリック型の場合: Vec<int> -> Vec<T> の定義キーへ正準関数で変換して検索（method.cppと同一実装の複製を排除）
                    size_t lt_pos = type_name.find('<');
                    if (lt_pos != std::string::npos) {
                        std::string base_name = type_name.substr(0, lt_pos);
                        if (generic_structs_.count(base_name) > 0) {
                            it = type_methods_.find(generic_def_method_key(base_name));
                        }
                    }
                }

                if (it != type_methods_.end()) {
                    auto method_it = it->second.find(method_name);
                    if (method_it != it->second.end()) {
                        const auto& method_info = method_it->second;

                        // 静的メソッドかチェック
                        if (!method_info.is_static) {
                            error(current_span_,
                                  i18n::msgf(i18n::MsgId::TcMethodTypeNotStaticMethod, method_name,
                                             type_name));
                            return ast::make_error();
                        }

                        // 型名の型引数（Mutex<int>のint等）を抽出する。プリミティブ名は正しいTypeKindで
                        // 作らないと型同一性が壊れる（make_namedはStruct kindになり、Mutex<int>同士の
                        // 比較がkind不一致で失敗し「expected Mutex<int>, got Mutex<int>」になる。R22）
                        // 注: 文字列切り出しはmethod-resolution-unification（resolve_method API）で構造化予定
                        std::vector<ast::TypePtr> concrete_type_args;
                        std::vector<std::string> generic_param_names;
                        size_t lt_pos = type_name.find('<');
                        if (lt_pos != std::string::npos) {
                            std::string base_name = type_name.substr(0, lt_pos);
                            auto gen_it = generic_structs_.find(base_name);
                            if (gen_it != generic_structs_.end()) {
                                generic_param_names = gen_it->second;
                                std::string type_args_str = type_name.substr(lt_pos + 1);
                                type_args_str = type_args_str.substr(0, type_args_str.size() - 1);
                                auto make_type_arg = [](const std::string& n) -> ast::TypePtr {
                                    if (n == "int")
                                        return ast::make_int();
                                    if (n == "uint")
                                        return ast::make_uint();
                                    if (n == "long")
                                        return ast::make_long();
                                    if (n == "ulong")
                                        return ast::make_ulong();
                                    if (n == "short")
                                        return ast::make_short();
                                    if (n == "ushort")
                                        return ast::make_ushort();
                                    if (n == "tiny")
                                        return ast::make_tiny();
                                    if (n == "utiny")
                                        return ast::make_utiny();
                                    if (n == "isize")
                                        return ast::make_isize();
                                    if (n == "usize")
                                        return ast::make_usize();
                                    if (n == "float")
                                        return ast::make_float();
                                    if (n == "double")
                                        return ast::make_double();
                                    if (n == "bool")
                                        return ast::make_bool();
                                    if (n == "char")
                                        return ast::make_char();
                                    if (n == "string")
                                        return ast::make_string();
                                    return ast::make_named(n);
                                };
                                std::istringstream iss(type_args_str);
                                std::string type_arg_name;
                                while (std::getline(iss, type_arg_name, ',')) {
                                    size_t start = type_arg_name.find_first_not_of(" ");
                                    size_t end = type_arg_name.find_last_not_of(" ");
                                    if (start != std::string::npos && end != std::string::npos) {
                                        type_arg_name =
                                            type_arg_name.substr(start, end - start + 1);
                                    }
                                    concrete_type_args.push_back(make_type_arg(type_arg_name));
                                }
                            }
                        }

                        // パラメータ型へ型引数を代入する（従来は未置換のTと比較して
                        // 「expected T, got int」の誤診断だった。R22）
                        auto substituted_param = [&](size_t i) -> ast::TypePtr {
                            if (concrete_type_args.empty()) {
                                return method_info.param_types[i];
                            }
                            return substitute_generic_type(method_info.param_types[i],
                                                           generic_param_names, concrete_type_args);
                        };

                        // 引数の型チェック
                        if (call.args.size() != method_info.param_types.size()) {
                            error(
                                current_span_,
                                i18n::msgf(i18n::MsgId::TcStaticMethodExpectsArguments, ident->name,
                                           std::to_string(method_info.param_types.size()),
                                           std::to_string(call.args.size())));
                        } else {
                            for (size_t i = 0; i < call.args.size(); ++i) {
                                ast::TypePtr expected_type = substituted_param(i);
                                propagate_literal_expected_type(*call.args[i], expected_type);
                                auto arg_type = infer_type(*call.args[i]);
                                if (!types_compatible(expected_type, arg_type)) {
                                    std::string expected = ast::type_to_string(*expected_type);
                                    std::string actual = ast::type_to_string(*arg_type);
                                    error(
                                        current_span_,
                                        i18n::msgf(i18n::MsgId::TcArgumentTypeMismatchCallExpected,
                                                   ident->name, expected, actual));
                                }
                            }
                        }

                        // 戻り値型を返す（ジェネリック型パラメータを具体化する）
                        auto return_type = method_info.return_type;
                        if (!concrete_type_args.empty() && return_type) {
                            return_type = substitute_generic_type(return_type, generic_param_names,
                                                                  concrete_type_args);
                        }

                        debug::tc::log(debug::tc::Id::Resolved,
                                       "Static method call: " + ident->name +
                                           "() : " + ast::type_to_string(*return_type),
                                       debug::Level::Debug);
                        return return_type;
                    }
                }

                // ============================================================
                // enum constructor呼び出しのチェック: Result::Ok(value)など
                // ============================================================

                auto enum_it = generic_enums_.find(type_name);
                if (enum_it != generic_enums_.end()) {
                    // ジェネリックenumのコンストラクタ呼び出し
                    auto enum_def_it = enum_defs_.find(type_name);
                    if (enum_def_it != enum_defs_.end() && enum_def_it->second) {
                        const ast::EnumDecl* enum_decl = enum_def_it->second;

                        // メンバ（バリアント）が存在するか確認
                        for (const auto& member : enum_decl->members) {
                            if (member.name == method_name) {
                                // current_return_type_から型引数を推論
                                ast::TypePtr result_type = nullptr;

                                // typedefを解決してから比較
                                ast::TypePtr resolved_return_type = nullptr;
                                if (current_return_type_) {
                                    resolved_return_type = resolve_typedef(current_return_type_);
                                }

                                if (resolved_return_type &&
                                    resolved_return_type->name == type_name &&
                                    !resolved_return_type->type_args.empty()) {
                                    // 戻り値型から型引数を取得
                                    result_type = resolved_return_type;

                                    // 引数の型チェック（バリアントにデータがある場合）
                                    if (member.has_data() && !call.args.empty()) {
                                        auto arg_type = infer_type(*call.args[0]);
                                        // バリアントのデータ型をジェネリックパラメータから解決
                                        auto& type_params = enum_it->second;
                                        auto& type_args = resolved_return_type->type_args;

                                        // member.fieldsからデータ型を取得（1フィールドのみサポート）
                                        if (!member.fields.empty() && member.fields[0].second) {
                                            auto expected_type = substitute_generic_type(
                                                member.fields[0].second, type_params, type_args);
                                            if (!types_compatible(expected_type, arg_type)) {
                                                error(current_span_,
                                                      i18n::msgf(
                                                          i18n::MsgId::
                                                              TcArgumentTypeMismatchEnumConstructor,
                                                          ident->name,
                                                          ast::type_to_string(*expected_type),
                                                          ast::type_to_string(*arg_type)));
                                            }
                                        }
                                    }
                                } else {
                                    // 引数から型を推論（Result::Ok(5)のように）
                                    if (!call.args.empty()) {
                                        infer_type(*call.args[0]);
                                    }
                                    // enum型を返す（型引数なし）
                                    result_type = ast::make_named(type_name);
                                }

                                debug::tc::log(debug::tc::Id::Resolved,
                                               "Enum constructor: " + ident->name + "() : " +
                                                   (result_type ? ast::type_to_string(*result_type)
                                                                : type_name),
                                               debug::Level::Debug);
                                return result_type;
                            }
                        }
                    } else {
                        // 組み込み型（Result, Option）: enum_defs_にはないがgeneric_enums_にある
                        // enum_values_でバリアントを確認
                        std::string full_variant = type_name + "::" + method_name;
                        if (enum_values_.count(full_variant) > 0) {
                            // current_return_type_から型引数を推論
                            ast::TypePtr result_type = nullptr;
                            ast::TypePtr resolved_return_type = nullptr;
                            if (current_return_type_) {
                                resolved_return_type = resolve_typedef(current_return_type_);
                            }

                            if (resolved_return_type && resolved_return_type->name == type_name &&
                                !resolved_return_type->type_args.empty()) {
                                result_type = resolved_return_type;

                                // 引数の型チェック
                                if (!call.args.empty()) {
                                    auto arg_type = infer_type(*call.args[0]);
                                    auto& type_params = enum_it->second;
                                    (void)type_params;  // 将来使用予定
                                    auto& type_args = resolved_return_type->type_args;

                                    // Ok(T) -> type_args[0], Err(E) -> type_args[1]
                                    // Some(T) -> type_args[0]
                                    size_t param_idx = 0;
                                    if (type_name == "Result" && method_name == "Err") {
                                        param_idx = 1;  // E is the second type param
                                    }
                                    if (param_idx < type_args.size()) {
                                        auto expected_type = type_args[param_idx];
                                        if (!types_compatible(expected_type, arg_type)) {
                                            error(current_span_,
                                                  i18n::msgf(
                                                      i18n::MsgId::TcArgumentTypeMismatchExpected,
                                                      ident->name,
                                                      ast::type_to_string(*expected_type),
                                                      ast::type_to_string(*arg_type)));
                                        }
                                    }
                                }
                            } else {
                                // 引数から型を推論
                                if (!call.args.empty()) {
                                    infer_type(*call.args[0]);
                                }
                                result_type = ast::make_named(type_name);
                            }

                            debug::tc::log(
                                debug::tc::Id::Resolved,
                                "Builtin enum constructor: " + ident->name + "() : " +
                                    (result_type ? ast::type_to_string(*result_type) : type_name),
                                debug::Level::Debug);
                            return result_type;
                        }
                    }
                }

                // 非ジェネリックenumもチェック
                if (enum_names_.count(type_name)) {
                    auto enum_def_it = enum_defs_.find(type_name);
                    if (enum_def_it != enum_defs_.end() && enum_def_it->second) {
                        const ast::EnumDecl* enum_decl = enum_def_it->second;
                        for (const auto& member : enum_decl->members) {
                            if (member.name == method_name) {
                                // 引数チェック
                                if (member.has_data() && !call.args.empty()) {
                                    infer_type(*call.args[0]);
                                }

                                auto result_type = ast::make_named(type_name);
                                debug::tc::log(debug::tc::Id::Resolved,
                                               "Enum constructor: " + ident->name +
                                                   "() : " + ast::type_to_string(*result_type),
                                               debug::Level::Debug);
                                return result_type;
                            }
                        }
                    }
                }
            }

            error(current_span_, i18n::msgf(i18n::MsgId::TcNotFunction, ident->name));
            return ast::make_error();
        }

        // 関数ポインタ型の変数からの呼び出しをチェック
        if (!sym->is_function && sym->type && sym->type->kind == ast::TypeKind::Function) {
            auto fn_type = sym->type;
            size_t arg_count = call.args.size();
            size_t param_count = fn_type->param_types.size();

            if (arg_count != param_count) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcFunctionPointerExpectsArguments, ident->name,
                                 std::to_string(param_count), std::to_string(arg_count)));
            } else {
                for (size_t i = 0; i < arg_count; ++i) {
                    // パラメータ型を期待型として引数へ渡す（無名リテラル引数の型決定を一元化）
                    auto arg_type = infer_type_expecting(*call.args[i], fn_type->param_types[i]);
                    if (!types_compatible(fn_type->param_types[i], arg_type)) {
                        std::string expected = ast::type_to_string(*fn_type->param_types[i]);
                        std::string actual = ast::type_to_string(*arg_type);
                        error(current_span_,
                              i18n::msgf(i18n::MsgId::TcArgumentTypeMismatchCallFunction,
                                         ident->name, expected, actual));
                    }
                }
            }

            return fn_type->return_type ? fn_type->return_type : ast::make_void();
        }

        if (!sym->is_function) {
            error(current_span_, i18n::msgf(i18n::MsgId::TcNotFunction, ident->name));
            return ast::make_error();
        }

        // 通常の関数の引数チェック（デフォルト引数と可変長引数を考慮）
        size_t arg_count = call.args.size();
        size_t param_count = sym->param_types.size();
        size_t required_count = sym->required_params;

        // 可変長引数の場合は最低限の引数数をチェック
        if (sym->is_variadic) {
            if (arg_count < param_count) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcVariadicFunctionRequiresAtLeast, ident->name,
                                 std::to_string(param_count), std::to_string(arg_count)));
            } else {
                // 固定引数の型チェック
                for (size_t i = 0; i < param_count; ++i) {
                    // パラメータ型を期待型として引数へ渡す（無名リテラル引数の型決定を一元化）
                    auto arg_type = infer_type_expecting(*call.args[i], sym->param_types[i]);
                    if (!types_compatible(sym->param_types[i], arg_type)) {
                        std::string expected = ast::type_to_string(*sym->param_types[i]);
                        std::string actual = ast::type_to_string(*arg_type);
                        error(current_span_,
                              i18n::msgf(i18n::MsgId::TcArgumentTypeMismatchCallExpected,
                                         ident->name, expected, actual));
                    }
                    // キャプチャ付きクロージャの関数引数渡しは環境喪失でゴミ値になるため拒否（V5）
                    if (sym->param_types[i] &&
                        sym->param_types[i]->kind == ast::TypeKind::Function &&
                        is_capturing_closure_expr(*call.args[i])) {
                        error(current_span_,
                              i18n::msgf(i18n::MsgId::TcCannotPassCapturingClosureFunction,
                                         std::to_string(i + 1), ident->name));
                    }
                }
                // 可変長引数の型は推論のみ
                for (size_t i = param_count; i < arg_count; ++i) {
                    infer_type(*call.args[i]);
                }
            }
        } else if (arg_count < required_count || arg_count > param_count) {
            if (required_count == param_count) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcFunctionExpectsArguments, ident->name,
                                 std::to_string(param_count), std::to_string(arg_count)));
            } else {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcFunctionExpectsArguments2, ident->name,
                                 std::to_string(required_count), std::to_string(param_count),
                                 std::to_string(arg_count)));
            }
        } else {
            for (size_t i = 0; i < arg_count; ++i) {
                // パラメータ型を期待型として引数へ渡す（無名リテラル引数の型決定を一元化）
                auto arg_type = infer_type_expecting(*call.args[i], sym->param_types[i]);
                if (!types_compatible(sym->param_types[i], arg_type)) {
                    std::string expected = ast::type_to_string(*sym->param_types[i]);
                    std::string actual = ast::type_to_string(*arg_type);
                    error(current_span_, i18n::msgf(i18n::MsgId::TcArgumentTypeMismatchCallExpected,
                                                    ident->name, expected, actual));
                }
                // 関数引数の縮小・符号変化もlet/代入/returnと同じ規則で診断する（局所処理調査F系: 従来はこの文脈だけ無診断で値が切り詰まっていた）
                if (sym->param_types[i] && arg_type) {
                    check_numeric_conversion_policy(sym->param_types[i], arg_type,
                                                    call.args[i].get(), call.args[i]->span);
                }
                // キャプチャ付きクロージャの関数引数渡しは環境喪失でゴミ値になるため拒否（V5）
                if (sym->param_types[i] && sym->param_types[i]->kind == ast::TypeKind::Function &&
                    is_capturing_closure_expr(*call.args[i])) {
                    error(current_span_,
                          i18n::msgf(i18n::MsgId::TcCannotPassCapturingClosureFunction,
                                     std::to_string(i + 1), ident->name));
                }
            }
        }

        return sym->return_type;
    }

    // 識別子以外の式を呼び出し先にする間接呼び出し（fs[0](args)・getf()(args) 等。局所処理調査G4）。
    // メンバフィールドの関数ポインタ呼び出し（h.f(args)）は別経路で扱われ全ターゲットで動くため、ここでは添字式・呼び出し結果の被呼び出しのみを対象にする
    if (call.callee && (call.callee->as<ast::IndexExpr>() || call.callee->as<ast::CallExpr>())) {
        auto callee_type = infer_type(*call.callee);
        auto fn_type = resolve_typedef(callee_type);
        // 関数ポインタ型（Function、または Pointer→Function）を取り出す
        if (fn_type && fn_type->kind == ast::TypeKind::Pointer && fn_type->element_type &&
            fn_type->element_type->kind == ast::TypeKind::Function) {
            fn_type = fn_type->element_type;
        }
        if (fn_type && fn_type->kind == ast::TypeKind::Function) {
            // 引数を型検査する（パラメータ型を期待型として渡す）
            for (size_t i = 0; i < call.args.size(); ++i) {
                ast::TypePtr pt =
                    (i < fn_type->param_types.size()) ? fn_type->param_types[i] : nullptr;
                auto at = pt ? infer_type_expecting(*call.args[i], pt) : infer_type(*call.args[i]);
                if (pt && at && !types_compatible(pt, at)) {
                    error(current_span_,
                          i18n::msgf(i18n::MsgId::TcArgumentTypeMismatchCallExpected, "<indirect>",
                                     ast::type_to_string(*pt), ast::type_to_string(*at)));
                }
            }
            // 式値経由の間接呼び出しのコード生成は現状js/ts（構造的lowering）のみ対応。
            // native/jit/wasmは変数へ束ねれば動くため、クラッシュではなく明確な診断で誘導する
            if (!structural_array_lowering_) {
                error(current_span_, i18n::msg(i18n::MsgId::TcIndirectCallExprUnsupported));
            }
            return fn_type->return_type ? fn_type->return_type : ast::make_void();
        }
        // 関数ポインタでない式の呼び出しは診断する（従来は無診断でerror型に落ちていた）
        if (callee_type && callee_type->kind != ast::TypeKind::Error) {
            error(current_span_,
                  i18n::msgf(i18n::MsgId::TcNotFunction, ast::type_to_string(*callee_type)));
        }
    }

    return ast::make_error();
}

}  // namespace cm
