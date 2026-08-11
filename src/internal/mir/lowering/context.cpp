// MIR Lowering コンテキスト (context.hpp) の実装

#include "context.hpp"

#include "internal/base/target.hpp"
#include "internal/syntax/ast/convkind.hpp"
#include "internal/syntax/ast/typekey.hpp"
#include "layout.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// 新しいブロックを作成
BlockId LoweringContext::new_block() {
    BlockId id = func->add_block();
    if (func->name == "main") {
        debug_msg("mir_new_block", "[MIR] Created new block " + std::to_string(id) + " in main");
    }
    return id;
}

// 新しいローカル変数を作成（typedefを解決）
LocalId LoweringContext::new_local(const std::string& name, hir::TypePtr type, bool is_mutable,
                                   bool is_user, bool is_static, bool is_global) {
    auto resolved_type = resolve_typedef(type);
    return func->add_local(name, resolved_type, is_mutable, is_user, is_static, is_global);
}

// 新しい一時変数を作成（typedefを解決）
LocalId LoweringContext::new_temp(hir::TypePtr type) {
    std::string name = "_t" + std::to_string(next_temp_id++);
    auto resolved_type = resolve_typedef(type);
    return func->add_local(name, resolved_type, true, true, false);
}

// 文を現在のブロックに追加
void LoweringContext::push_statement(MirStatementPtr stmt) {
    auto* block = get_current_block();
    if (block) {
        // must{}ブロック内の文は最適化禁止
        if (in_must_block) {
            stmt->no_opt = true;
        }
        // デバッグ: ステートメント追加前の状態
        if (current_block == 0 && stmt->kind == MirStatement::Assign) {
            auto& assign = std::get<MirStatement::AssignData>(stmt->data);
            debug_msg("mir_bb0_stmt",
                      "[MIR] Adding to bb0: assign to local " + std::to_string(assign.place.local) +
                          ", bb0 currently has " + std::to_string(block->statements.size()) +
                          " statements" +
                          ", block ptr: " + std::to_string(reinterpret_cast<uintptr_t>(block)));
        }
        block->add_statement(std::move(stmt));
    }
}

// ターミネータを設定
void LoweringContext::set_terminator(MirTerminatorPtr term) {
    auto* block = get_current_block();
    if (block && !block->terminator) {
        block->set_terminator(std::move(term));
    }
}

// ループコンテキストをプッシュ（forループ用、continueターゲット指定）
void LoweringContext::push_loop(BlockId header, BlockId exit, BlockId continue_target) {
    loop_stack.emplace(header, exit, continue_target);
}

// ループコンテキストをポップ
void LoweringContext::pop_loop() {
    if (!loop_stack.empty()) {
        loop_stack.pop();
    }
}

// enum値を取得
std::optional<int64_t> LoweringContext::get_enum_value(const std::string& enum_name,
                                                       const std::string& member_name) {
    if (!enum_defs)
        return std::nullopt;

    auto enum_it = enum_defs->find(enum_name);
    if (enum_it == enum_defs->end())
        return std::nullopt;

    auto member_it = enum_it->second.find(member_name);
    if (member_it == enum_it->second.end())
        return std::nullopt;

    return member_it->second;
}

// スコープ管理
void LoweringContext::push_scope() {
    scopes.emplace_back();
    defer_stacks.emplace_back();
    destructor_vars.emplace_back();
}

void LoweringContext::pop_scope() {
    if (!scopes.empty()) {
        scopes.pop_back();
    }
    if (!defer_stacks.empty()) {
        defer_stacks.pop_back();
    }
    if (!destructor_vars.empty()) {
        destructor_vars.pop_back();
    }
}

// 現在のスコープにdefer文を追加
void LoweringContext::add_defer(const hir::HirStmt* stmt) {
    if (!defer_stacks.empty()) {
        defer_stacks.back().push_back(stmt);
    }
}

// 現在のスコープのdefer文を取得（逆順）
std::vector<const hir::HirStmt*> LoweringContext::get_defer_stmts() {
    if (!defer_stacks.empty()) {
        auto stmts = defer_stacks.back();
        std::reverse(stmts.begin(), stmts.end());  // 逆順にする
        return stmts;
    }
    return {};
}

// デストラクタを持つ変数を登録
void LoweringContext::register_destructor_var(LocalId id, const std::string& type_name) {
    if (!destructor_vars.empty()) {
        destructor_vars.back().push_back({id, type_name});
    }
}

// move済み変数のデストラクタ登録を全スコープから解除する（moved-outの二重解放防止）
void LoweringContext::unregister_destructor_var(LocalId id) {
    for (auto& scope_vars : destructor_vars) {
        for (auto it = scope_vars.begin(); it != scope_vars.end();) {
            if (it->first == id) {
                it = scope_vars.erase(it);
            } else {
                ++it;
            }
        }
    }
}

// 全スコープのデストラクタ変数を取得（内側から外側へ、逆順）
std::vector<std::pair<LocalId, std::string>> LoweringContext::get_all_destructor_vars() {
    std::vector<std::pair<LocalId, std::string>> result;
    // 内側のスコープから外側へ
    for (auto it = destructor_vars.rbegin(); it != destructor_vars.rend(); ++it) {
        // 各スコープ内では逆順（後から宣言された変数が先にデストラクト）
        for (auto var_it = it->rbegin(); var_it != it->rend(); ++var_it) {
            result.push_back(*var_it);
        }
    }
    return result;
}

// 現在のスコープのデストラクタ変数を取得（逆順）
std::vector<std::pair<LocalId, std::string>> LoweringContext::get_current_scope_destructor_vars() {
    std::vector<std::pair<LocalId, std::string>> result;
    if (!destructor_vars.empty()) {
        auto& current = destructor_vars.back();
        for (auto it = current.rbegin(); it != current.rend(); ++it) {
            result.push_back(*it);
        }
    }
    return result;
}

// 型がデストラクタを持つか確認
bool LoweringContext::has_destructor(const std::string& type_name) const {
    // 直接登録されている場合
    if (types_with_destructor.count(type_name) > 0) {
        return true;
    }

    // ジェネリック型の場合、元テンプレート名を抽出してチェック。
    // dtor登録名は関数名ドメイン（base__argkey…。argkeyに$を含みうる）のため最初の__を基底区切りとして優先し、
    // __を含まない$構造体キーはtypekeyの正準抽出で基底を取る
    std::string base_template_name;
    {
        auto us = type_name.find("__");
        if (us != std::string::npos && us > 0) {
            base_template_name = type_name.substr(0, us);
        } else {
            base_template_name = ast::typekey::spec_base_name(type_name);
        }
    }
    if (base_template_name != type_name) {
        const std::string& base_template = base_template_name;
        // Vector<T> の形式で登録されているかチェック
        std::string generic_name = base_template + "<T>";
        if (types_with_destructor.count(generic_name) > 0) {
            return true;
        }
        // Vector<K, V> の形式もチェック
        generic_name = base_template + "<K, V>";
        if (types_with_destructor.count(generic_name) > 0) {
            return true;
        }
        // 単なるベース名でもチェック
        if (types_with_destructor.count(base_template) > 0) {
            return true;
        }
    }

    // ベース名で渡された場合（例：Vector）、ジェネリックテンプレートをチェック（$エンコード名は上の基底抽出経路が処理済み）
    if (type_name.find('<') == std::string::npos && type_name.find("__") == std::string::npos &&
        !ast::typekey::is_encoded_key(type_name)) {
        std::string generic_name = type_name + "<T>";
        if (types_with_destructor.count(generic_name) > 0) {
            return true;
        }
        generic_name = type_name + "<K, V>";
        if (types_with_destructor.count(generic_name) > 0) {
            return true;
        }
    }

    return false;
}

// ジェネリック型パラメータを解決（sizeof_for_T用）
hir::TypePtr LoweringContext::resolve_type_param(const std::string& param_name) const {
    auto it = type_param_map.find(param_name);
    if (it != type_param_map.end()) {
        return it->second;
    }
    return nullptr;
}

// 型サイズを計算（sizeof_for_Tマーカー処理用）
int64_t LoweringContext::calculate_type_size(const hir::TypePtr& type) const {
    // 型サイズ一本化: 見積もり実装（フィールド数×8・__ベース名逆算）を廃止し、真実のlayout_size 1系統へ委譲する
    return layout_size(type);
}

// 変数を現在のスコープに登録
void LoweringContext::register_variable(const std::string& name, LocalId id) {
    if (!scopes.empty()) {
        scopes.back()[name] = id;
    }
}

// 変数名からLocalIdを解決（スコープチェーンを遡る）
std::optional<LocalId> LoweringContext::resolve_variable(const std::string& name) {
    // 内側のスコープから外側へ向かって検索
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto var_it = it->find(name);
        if (var_it != it->end()) {
            return var_it->second;
        }
    }
    return std::nullopt;
}

// const変数の値を登録
void LoweringContext::register_const_value(const std::string& name, const MirConstant& value) {
    const_values[name] = value;
}

// const変数の値を取得（関数ローカルとグローバルの両方をチェック）
std::optional<MirConstant> LoweringContext::get_const_value(const std::string& name) {
    // まず関数ローカルのconst変数をチェック
    auto it = const_values.find(name);
    if (it != const_values.end()) {
        return it->second;
    }

    // 次にグローバルconst変数をチェック
    if (global_const_values) {
        auto global_it = global_const_values->find(name);
        if (global_it != global_const_values->end()) {
            return global_it->second;
        }
    }

    return std::nullopt;
}

// 構造体のフィールドインデックスを取得
std::optional<FieldId> LoweringContext::get_field_index(const std::string& struct_name,
                                                        const std::string& field_name) {
    if (!struct_defs || struct_defs->find(struct_name) == struct_defs->end()) {
        return std::nullopt;
    }

    const auto* struct_def = struct_defs->at(struct_name);
    for (size_t i = 0; i < struct_def->fields.size(); ++i) {
        if (struct_def->fields[i].name == field_name) {
            return static_cast<FieldId>(i);
        }
    }
    return std::nullopt;
}

// typedefとenumを解決（必要に応じて再帰的に）
hir::TypePtr LoweringContext::resolve_typedef(const hir::TypePtr& type) {
    if (!type) {
        return type;
    }

    // Structタイプの場合、typedef/enum定義を確認
    if (type->kind == hir::TypeKind::Struct || type->kind == hir::TypeKind::TypeAlias) {
        // まずtypedef定義を確認
        if (typedef_defs) {
            if (auto it = typedef_defs->find(type->name); it != typedef_defs->end()) {
                // 再帰的に解決
                return resolve_typedef(it->second);
            }
        }

        // enum定義を確認
        if (enum_defs) {
            auto it = enum_defs->find(type->name);

            // モノモーフ化された型名（例: Result__ulong__long・Result$2$...）の場合、正準関数で基底名（Result）を取りenum_defsをフォールバック検索
            if (it == enum_defs->end()) {
                const std::string base_name = ast::typekey::spec_base_name(type->name);
                if (base_name != type->name) {
                    it = enum_defs->find(base_name);
                }
            }

            if (it != enum_defs->end()) {
                // Tagged Union enum（ペイロード付き）は__TaggedUnion_構造体として扱う
                std::string base_enum_name = it->first;
                if (tagged_union_names && (tagged_union_names->count(type->name) ||
                                           tagged_union_names->count(base_enum_name))) {
                    auto tagged_union_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                    tagged_union_type->name = "__TaggedUnion_" + type->name;
                    // 元の型引数を保持（ペイロード型推論に使用）
                    tagged_union_type->type_args = type->type_args;
                    return tagged_union_type;
                }
                // 通常のenum（値のみ）はintとして扱う
                return hir::make_int();
            }
        }
    }

    // ポインタ型の場合、要素型を再帰的に解決
    if (type->kind == hir::TypeKind::Pointer || type->kind == hir::TypeKind::Reference) {
        auto resolved_elem = resolve_typedef(type->element_type);
        if (resolved_elem != type->element_type) {
            auto result = std::make_shared<hir::Type>(*type);
            result->element_type = resolved_elem;
            return result;
        }
    }

    // 配列型の場合、要素型を再帰的に解決
    if (type->kind == hir::TypeKind::Array) {
        auto resolved_elem = resolve_typedef(type->element_type);
        if (resolved_elem != type->element_type) {
            auto result = std::make_shared<hir::Type>(*type);
            result->element_type = resolved_elem;
            return result;
        }
    }

    // LiteralUnion型の場合、基底型（string/int/double）に変換
    if (type->kind == hir::TypeKind::LiteralUnion) {
        auto* lit_union = static_cast<ast::LiteralUnionType*>(type.get());
        if (lit_union && !lit_union->literals.empty()) {
            const auto& first = lit_union->literals[0].value;
            if (std::holds_alternative<std::string>(first)) {
                return hir::make_string();
            } else if (std::holds_alternative<int64_t>(first)) {
                return hir::make_int();
            } else if (std::holds_alternative<double>(first)) {
                return hir::make_double();
            }
        }
        return hir::make_int();  // フォールバック
    }

    return type;
}

// 浮動小数が絡む数値文脈の暗黙変換としてCastを挿入する（B2/Z5）。
// 整数値→浮動小数宛先はsitofp/uitofp相当（B2: 整数ビットのdouble再解釈で5e-324になる誤りの修正）、
// float/double間の幅違いはfpext/fptrunc相当、浮動小数値→整数宛先はfptosi/fptoui相当のCastで揃える
// （Z5: 受理された暗黙変換に変換命令が挿入されず、let/引数/returnのdouble→intがビット再解釈のゴミ値やLLVM検証エラーになっていた）。
// 変換不要ならvalueをそのまま返す
LocalId LoweringContext::coerce_numeric_context(LocalId value, const hir::TypePtr& target_type) {
    if (!target_type || value >= func->locals.size()) {
        return value;
    }
    auto is_float_kind = [](hir::TypeKind k) {
        return k == hir::TypeKind::Float || k == hir::TypeKind::Double ||
               k == hir::TypeKind::UFloat || k == hir::TypeKind::UDouble;
    };
    auto is_int_kind = [](hir::TypeKind k) {
        return k == hir::TypeKind::Tiny || k == hir::TypeKind::Short || k == hir::TypeKind::Int ||
               k == hir::TypeKind::Long || k == hir::TypeKind::UTiny ||
               k == hir::TypeKind::UShort || k == hir::TypeKind::UInt ||
               k == hir::TypeKind::ULong || k == hir::TypeKind::ISize || k == hir::TypeKind::USize;
    };
    // f32/f64の実表現幅で比較する（UFloat/Floatのような符号制約のみの違いは変換不要）
    auto float_width = [](hir::TypeKind k) {
        return (k == hir::TypeKind::Float || k == hir::TypeKind::UFloat) ? 32 : 64;
    };
    auto target = resolve_typedef(target_type);
    if (!target) {
        return value;
    }
    auto value_type = resolve_typedef(func->locals[value].type);
    if (!value_type) {
        return value;
    }
    bool needs_cast = false;
    if (is_float_kind(target->kind)) {
        const bool needs_int_to_float = is_int_kind(value_type->kind);
        const bool needs_float_resize = is_float_kind(value_type->kind) &&
                                        float_width(value_type->kind) != float_width(target->kind);
        needs_cast = needs_int_to_float || needs_float_resize;
    } else if (is_int_kind(target->kind)) {
        // 浮動小数→整数宛先はfptosi/fptoui相当のCastが必須（未挿入だと型不一致のIRになる）。
        // 整数同士の幅違いは既存のコード生成幅合わせが機能しているためここでは変換しない
        needs_cast = is_float_kind(value_type->kind);
    }
    if (!needs_cast) {
        return value;
    }
    LocalId casted = new_temp(target);
    push_statement(MirStatement::assign(
        MirPlace{casted}, MirRvalue::cast(MirOperand::copy(MirPlace{value}), target)));
    return casted;
}

// 宛先型がユニオンで値が変種型の場合、ユニオン構築Cast（タグ+ペイロード書き込み）を経由した一時を返す（Y1〜Y3）。
// let初期化・単純代入は宛先placeへ直接Castするため本ヘルパを使わないが、意味論は同一である
LocalId LoweringContext::coerce_to_union(LocalId value, const hir::TypePtr& dest_type) {
    if (!dest_type || value >= func->locals.size()) {
        return value;
    }
    hir::TypePtr dest = resolve_typedef(dest_type);
    if (!dest || dest->kind != hir::TypeKind::Union) {
        return value;
    }
    hir::TypePtr src = resolve_typedef(func->locals[value].type);
    if (src && src->kind == hir::TypeKind::Union) {
        return value;
    }
    LocalId casted = new_temp(dest);
    push_statement(MirStatement::assign(MirPlace{casted},
                                        MirRvalue::cast(MirOperand::copy(MirPlace{value}), dest)));
    return casted;
}

// 暗黙変換の統一ドライバ（変換統一ドライバ第1段）。従来は消費サイトごとにヘルパ3種を手組みで連鎖しており、
// 「受理されるのに変換が挿入されないサイトがある」バグ族（B2・Y1〜Y3・Y5・Z5・Q3）の温床だった。
// 宛先がユニオンの場合の変種解決は保守的で、値の型に一致する変種があれば事前coerceせずwrapする（既存挙動の維持）。
// 一致変種が無い場合のみ、固定長配列→唯一のスライス変種、数値→唯一の数値変種の事前coerceを行ってからwrapする
LocalId LoweringContext::coerce_to_expected(LocalId value, const hir::TypePtr& expected) {
    if (!expected || value >= func->locals.size()) {
        return value;
    }
    hir::TypePtr dest = resolve_typedef(expected);
    if (!dest) {
        return value;
    }
    // 変換種のディスパッチは受理側（checkerのtypes_compatible）と同じ分類表から導く（受理と挿入の同表化）
    ast::convkind::Env conv_env;
    conv_env.resolve = [this](const hir::TypePtr& t) { return resolve_typedef(t); };
    conv_env.is_interface = [this](const std::string& n) {
        return interface_names && interface_names->count(n) > 0;
    };
    const auto conv_kind = ast::convkind::classify(dest, func->locals[value].type, conv_env);
    if (dest->kind == hir::TypeKind::Union) {
        hir::TypePtr src = resolve_typedef(func->locals[value].type);
        if (conv_kind == ast::convkind::Kind::UnionWrap && src) {
            const auto variants = ast::union_variant_types(dest);
            auto is_numeric = [](hir::TypeKind k) {
                return k == hir::TypeKind::Tiny || k == hir::TypeKind::UTiny ||
                       k == hir::TypeKind::Short || k == hir::TypeKind::UShort ||
                       k == hir::TypeKind::Int || k == hir::TypeKind::UInt ||
                       k == hir::TypeKind::Long || k == hir::TypeKind::ULong ||
                       k == hir::TypeKind::Float || k == hir::TypeKind::UFloat ||
                       k == hir::TypeKind::Double || k == hir::TypeKind::UDouble;
            };
            bool exact = false;
            hir::TypePtr slice_variant = nullptr;
            hir::TypePtr numeric_variant = nullptr;
            int slice_count = 0;
            int numeric_count = 0;
            // nullリテラルはcheckerでvoid型が付くため、Null変種との照合ではNull扱いにする
            const hir::TypeKind src_kind_for_match =
                src->kind == hir::TypeKind::Void ? hir::TypeKind::Null : src->kind;
            for (const auto& v : variants) {
                auto rv = resolve_typedef(v);
                if (!rv) {
                    continue;
                }
                if (rv->kind == src_kind_for_match &&
                    (rv->kind != hir::TypeKind::Struct || rv->name == src->name) &&
                    (rv->kind != hir::TypeKind::Array ||
                     rv->array_size.has_value() == src->array_size.has_value())) {
                    // Arrayは動的/固定長の別まで一致して初めて完全一致（固定長配列がスライス変種にkindだけで
                    // 一致扱いされると実体化がスキップされ、生データのままwrapされてしまう）
                    exact = true;
                    break;
                }
                if (rv->kind == hir::TypeKind::Array && !rv->array_size.has_value()) {
                    slice_variant = rv;
                    slice_count++;
                }
                if (is_numeric(rv->kind)) {
                    numeric_variant = rv;
                    numeric_count++;
                }
            }
            if (!exact) {
                if (src->kind == hir::TypeKind::Array && src->array_size.has_value() &&
                    slice_count == 1) {
                    // ユニオンofスライス変種への固定長配列: まずスライスへ実体化してからwrapする
                    value = coerce_fixed_array_to_slice(value, slice_variant);
                } else if (is_numeric(src->kind) && numeric_count == 1) {
                    // 唯一の数値変種への正規化（int→double変種等。一致変種が無い場合のみ）
                    value = coerce_numeric_context(value, numeric_variant);
                }
            }
        }
        return coerce_to_union(value, dest);
    }
    // インターフェースupcast（値）: fat pointer構築をMIRの構築物（iface_upcast Cast）として発行する。
    // ペイロードはヒープへ実体化（boxed）してから包む（戻り値経由でスタックローカルを指したまま
    // ダングリングする分裂の恒久修正）
    if (conv_kind == ast::convkind::Kind::IfaceValueUpcast) {
        hir::TypePtr src = resolve_typedef(func->locals[value].type);
        if (src) {
            LocalId fat = new_temp(dest);
            push_statement(MirStatement::assign(
                MirPlace{fat}, MirRvalue::iface_upcast(MirOperand::copy(MirPlace{value}), dest,
                                                       src->name, false, true)));
            return fat;
        }
        return value;
    }
    // インターフェースupcast（ポインタ）: 指し先アドレスをdataとするfat pointerを構築する
    // （boxingなし＝既存ストレージを指す）
    if (conv_kind == ast::convkind::Kind::IfacePtrUpcast) {
        hir::TypePtr src = resolve_typedef(func->locals[value].type);
        auto src_elem = (src && src->element_type) ? resolve_typedef(src->element_type) : nullptr;
        if (src_elem) {
            LocalId fat = new_temp(dest);
            push_statement(MirStatement::assign(
                MirPlace{fat}, MirRvalue::iface_upcast(MirOperand::copy(MirPlace{value}), dest,
                                                       src_elem->name, true, false)));
            return fat;
        }
        return value;
    }
    if (conv_kind == ast::convkind::Kind::NumericImplicit) {
        return coerce_numeric_context(value, dest);
    }
    if (conv_kind == ast::convkind::Kind::ArrayToSlice) {
        return coerce_fixed_array_to_slice(value, dest);
    }
    return value;
}

// 宛先型がスライスで値が固定長配列の場合、cm_array_to_sliceでヒープスライスへ実体化した一時を返す（Y5）。
// 要素ストライドはlayout API（array_elem_stride相当）で計算する。メソッドレシーバ（HIRのneeds_array_to_slice）と同じ意味論
LocalId LoweringContext::coerce_fixed_array_to_slice(LocalId value, const hir::TypePtr& dest_type) {
    if (!dest_type || value >= func->locals.size()) {
        return value;
    }
    hir::TypePtr dest = resolve_typedef(dest_type);
    if (!dest || dest->kind != hir::TypeKind::Array || dest->array_size.has_value()) {
        return value;
    }
    hir::TypePtr src = resolve_typedef(func->locals[value].type);
    if (!src || src->kind != hir::TypeKind::Array || !src->array_size.has_value()) {
        return value;
    }
    const int64_t array_size = static_cast<int64_t>(src->array_size.value_or(0));
    const int64_t elem_stride = layout::array_elem_stride(*this, src->element_type);

    LocalId addr_local = new_temp(hir::make_pointer(src->element_type));
    push_statement(
        MirStatement::assign(MirPlace{addr_local}, MirRvalue::ref(MirPlace{value}, false)));

    LocalId size_local = new_temp(hir::make_long());
    MirConstant size_const;
    size_const.value = array_size;
    size_const.type = hir::make_long();
    push_statement(MirStatement::assign(MirPlace{size_local},
                                        MirRvalue::use(MirOperand::constant(size_const))));

    LocalId stride_local = new_temp(hir::make_long());
    MirConstant stride_const;
    stride_const.value = elem_stride;
    stride_const.type = hir::make_long();
    push_statement(MirStatement::assign(MirPlace{stride_local},
                                        MirRvalue::use(MirOperand::constant(stride_const))));

    LocalId slice_local = new_temp(dest);
    BlockId success_block = new_block();
    std::vector<MirOperandPtr> conv_args;
    conv_args.push_back(MirOperand::copy(MirPlace{addr_local}));
    conv_args.push_back(MirOperand::copy(MirPlace{size_local}));
    conv_args.push_back(MirOperand::copy(MirPlace{stride_local}));
    auto conv_term = std::make_unique<MirTerminator>();
    conv_term->kind = MirTerminator::Call;
    conv_term->data = MirTerminator::CallData{MirOperand::function_ref("cm_array_to_slice"),
                                              std::move(conv_args),
                                              MirPlace{slice_local},
                                              success_block,
                                              std::nullopt,
                                              "",
                                              "",
                                              false};
    set_terminator(std::move(conv_term));
    switch_to_block(success_block);
    return slice_local;
}

// LLVMのDataLayout（自然アライメント・パッキングなし）と一致するアライメントを計算する
int64_t LoweringContext::layout_align(const hir::TypePtr& type) const {
    if (!type) {
        return 8;
    }
    auto t = const_cast<LoweringContext*>(this)->resolve_typedef(type);
    if (!t) {
        return 8;
    }
    switch (t->kind) {
        case hir::TypeKind::Bool:
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
        case hir::TypeKind::Char:
            return 1;
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return 2;
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
        case hir::TypeKind::Float:
        case hir::TypeKind::UFloat:
            return 4;
        case hir::TypeKind::Struct: {
            // 構造体のアライメント = フィールドの最大アライメント
            if (struct_defs && struct_defs->count(t->name)) {
                const auto* st = struct_defs->at(t->name);
                int64_t max_align = 1;
                for (const auto& f : st->fields) {
                    max_align = std::max(max_align, layout_align(f.type));
                }
                return max_align;
            }
            return 8;
        }
        case hir::TypeKind::Array:
            return layout_align(t->element_type);
        case hir::TypeKind::Union:
            // tagged union {i32 tag, [N x i8]} のアライメントは4
            return 4;
        case hir::TypeKind::Pointer:
        case hir::TypeKind::String:
            // ポインタ幅はターゲット依存（wasm32/baremetal-armは4）
            return cm::target_pointer_size();
        default:
            return 8;  // long/double等
    }
}

// LLVMのDataLayout（自然アライメント・パッキングなし）と一致する型サイズを計算する
// スライスのblob要素サイズ算出用。codegenの実レイアウトと一致することが前提
int64_t LoweringContext::layout_size(const hir::TypePtr& type) const {
    if (!type) {
        return 8;
    }
    auto t = const_cast<LoweringContext*>(this)->resolve_typedef(type);
    if (!t) {
        return 8;
    }
    auto align_to = [](int64_t offset, int64_t align) {
        return (offset + align - 1) / align * align;
    };
    switch (t->kind) {
        case hir::TypeKind::Bool:
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
        case hir::TypeKind::Char:
            return 1;
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return 2;
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
        case hir::TypeKind::Float:
        case hir::TypeKind::UFloat:
            return 4;
        case hir::TypeKind::Struct: {
            // インターフェイス値はfat pointer（dataポインタ+vtableポインタ）でポインタ2個分（H1）
            if (interface_names && interface_names->count(t->name)) {
                return 2 * cm::target_pointer_size();
            }
            // フィールドを自然アライメントで並べたCレイアウトのサイズ
            if (struct_defs && struct_defs->count(t->name)) {
                const auto* st = struct_defs->at(t->name);
                int64_t offset = 0;
                int64_t max_align = 1;
                for (const auto& f : st->fields) {
                    int64_t fa = layout_align(f.type);
                    max_align = std::max(max_align, fa);
                    offset = align_to(offset, fa) + layout_size(f.type);
                }
                int64_t size = align_to(offset, max_align);
                return size > 0 ? size : 8;
            }
            return 8;
        }
        case hir::TypeKind::Array:
            if (t->element_type && t->array_size.has_value()) {
                return layout_size(t->element_type) * static_cast<int64_t>(t->array_size.value());
            }
            return 8;
        case hir::TypeKind::Union: {
            // tagged union {i32 tag, [N x i8] payload} のallocサイズと一致させる
            // ペイロード = 最大バリアントサイズ（構造体バリアント含む・最低8バイト）
            int64_t payload = 8;
            auto variants = ast::union_variant_types(t);
            if (!variants.empty()) {
                payload = 0;
                for (const auto& variant : variants) {
                    payload = std::max(payload, layout_size(variant));
                }
                payload = std::max<int64_t>(payload, 8);
            }
            return align_to(4 + payload, 4);
        }
        case hir::TypeKind::Pointer:
        case hir::TypeKind::Reference:
        case hir::TypeKind::String:
            // ポインタ幅はターゲット依存（wasm32/baremetal-armは4）
            return cm::target_pointer_size();
        default:
            return 8;  // long/double等
    }
}

}  // namespace cm::mir
