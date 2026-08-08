// MIR Lowering 基底クラス (base.hpp) の実装

#include "base.hpp"

#include "mono/typekey.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace cm::mir {

// 構造体を登録
void MirLoweringBase::register_struct(const hir::HirStruct& st) {
    struct_defs[st.name] = &st;
    // 初期化メソッドがある場合は記録
    if (!st.has_explicit_constructor) {
        // デフォルトコンストラクタがある
        impl_info[st.name]["@init"] = st.name + "__init";
    }
}

// インポートを処理
void MirLoweringBase::process_imports(const hir::HirProgram& hir_program) {
    for (const auto& decl : hir_program.declarations) {
        if (auto* imp = std::get_if<std::unique_ptr<hir::HirImport>>(&decl->kind)) {
            auto mir_import = std::make_unique<MirImport>();
            mir_import->path = (*imp)->path;
            mir_import->package_name = (*imp)->package_name;
            mir_import->alias = (*imp)->alias;

            // モジュール名を構築（例: ["std", "io"] -> "std::io"）
            std::string module_name;
            for (size_t i = 0; i < mir_import->path.size(); ++i) {
                if (i > 0)
                    module_name += "::";
                module_name += mir_import->path[i];
            }

            // インポートマップに追加
            if (!mir_import->alias.empty()) {
                imported_modules[mir_import->alias] = module_name;
            } else {
                // エイリアスがない場合は最後のセグメントを使用
                if (!mir_import->path.empty()) {
                    imported_modules[mir_import->path.back()] = module_name;
                }
            }

            mir_program.imports.push_back(std::move(mir_import));
        }
    }
}

// typedefとenumを解決（エイリアスを実際の型に展開）
hir::TypePtr MirLoweringBase::resolve_typedef(hir::TypePtr type) {
    if (!type)
        return type;

    // TypeAlias型の場合、typedefかチェック
    if (type->kind == hir::TypeKind::TypeAlias || type->kind == hir::TypeKind::Struct) {
        // まずtypedef定義を確認
        auto it = typedef_defs.find(type->name);
        if (it != typedef_defs.end()) {
            // 再帰的に解決（typedef chainがある場合）
            return resolve_typedef(it->second);
        }

        // enum定義を確認
        auto enum_it = enum_defs.find(type->name);

        // モノモーフ化された型名（例: Result__ulong__long・Result$2$...）の場合、正準関数で基底名（Result）を取りenum_defsをフォールバック検索
        if (enum_it == enum_defs.end()) {
            const std::string base_name = typekey::spec_base_name(type->name);
            if (base_name != type->name) {
                enum_it = enum_defs.find(base_name);
            }
        }

        if (enum_it != enum_defs.end()) {
            // Tagged Union enum（ペイロード付き）は__TaggedUnion_構造体として扱う
            // モノモーフ化された型名の場合はベース名でtagged_union_namesを確認
            std::string base_enum_name = enum_it->first;
            if (tagged_union_names.count(type->name) || tagged_union_names.count(base_enum_name)) {
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

// enum定義を登録（MirEnumも作成してmir_programに追加）
void MirLoweringBase::register_enum(const hir::HirEnum& e) {
    // enum_defsに登録（値マッピング用）
    bool has_data = false;
    for (const auto& member : e.members) {
        enum_defs[e.name][member.name] = member.value;
        if (!member.fields.empty()) {
            has_data = true;
        }
    }

    // Tagged Union（ペイロード付きenum）の場合、名前を記録
    if (has_data) {
        tagged_union_names.insert(e.name);
    }

    // MirEnumを作成してmir_programに追加
    auto mir_enum = std::make_unique<MirEnum>();
    mir_enum->name = e.name;
    mir_enum->is_export = e.is_export;

    for (const auto& member : e.members) {
        MirEnumMember mir_member;
        mir_member.name = member.name;
        mir_member.tag_value = member.value;
        // Associated dataフィールドをコピー
        for (const auto& [field_name, field_type] : member.fields) {
            mir_member.fields.emplace_back(field_name, field_type);
        }
        mir_enum->members.push_back(std::move(mir_member));
    }

    mir_program.enums.push_back(std::move(mir_enum));
}

// グローバル変数を登録
void MirLoweringBase::register_global_var(const hir::HirGlobalVar& gv) {
    // const変数のみ定数評価（文字列補間で使用）
    if (gv.is_const && gv.init) {
        // コンパイル時定数評価を試行
        auto const_val = try_global_const_eval(*gv.init);
        if (const_val) {
            const_val->type = gv.type ? gv.type : const_val->type;
            global_const_values[gv.name] = *const_val;
            // SVバックエンドではlocalparam出力のため、global_varsにも登録する（returnせずフォールスルーで下のMirGlobalVar登録へ進む）
        }
    }

    // 非constグローバル変数のみMirGlobalVarとして登録
    auto mir_gv = std::make_unique<MirGlobalVar>();
    mir_gv->name = gv.name;
    mir_gv->type = gv.type;
    mir_gv->is_const = gv.is_const;
    mir_gv->is_assign = gv.is_assign;
    mir_gv->is_export = gv.is_export;
    mir_gv->attributes = gv.attributes;  // SV用属性を伝搬（input/output等）

    // 初期値を設定
    if (gv.init) {
        auto const_val = try_global_const_eval(*gv.init);
        if (const_val) {
            mir_gv->init_value = std::make_unique<MirConstant>(*const_val);
        } else {
            // 定数評価できない初期化式はHIR式のまま保持する（assign文/const定数のほか、配列リテラル初期値のinitial出力等でSVコードジェネレータが使用する）
            mir_gv->init_expr = gv.init.get();
        }

        // 構造体リテラル初期化（extern structインスタンスのポート接続用）:
        // `alu a0 = alu { a: x, b: y };` のフィールド値を文字列/定数として保持する
        if (const auto* struct_lit =
                std::get_if<std::unique_ptr<hir::HirStructLiteral>>(&gv.init->kind)) {
            if (*struct_lit) {
                for (const auto& field : (*struct_lit)->fields) {
                    if (!field.value) {
                        continue;
                    }
                    MirConstant field_const;
                    if (const auto* var_ref =
                            std::get_if<std::unique_ptr<hir::HirVarRef>>(&field.value->kind)) {
                        // 信号名接続（識別子）
                        if (*var_ref) {
                            field_const.type = hir::make_string();
                            field_const.value = (*var_ref)->name;
                            mir_gv->struct_field_inits.emplace_back(field.name, field_const);
                        }
                    } else if (const auto* member = std::get_if<std::unique_ptr<hir::HirMember>>(
                                   &field.value->kind)) {
                        // メンバアクセス接続（io.x 等のIOインスタンスフィールド）:
                        // "base.member" として保持し、SVコード生成側でポート名へ写像する
                        if (*member && (*member)->object) {
                            if (const auto* base_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(
                                    &(*member)->object->kind)) {
                                if (*base_ref) {
                                    field_const.type = hir::make_string();
                                    field_const.value = (*base_ref)->name + "." + (*member)->member;
                                    mir_gv->struct_field_inits.emplace_back(field.name,
                                                                            field_const);
                                }
                            }
                        }
                    } else if (auto fc = try_global_const_eval(*field.value)) {
                        // 定数（パラメータ値等）
                        mir_gv->struct_field_inits.emplace_back(field.name, *fc);
                    }
                }
            }
        }
    }

    // グローバル変数名を追跡
    global_var_names.insert(gv.name);

    mir_program.global_vars.push_back(std::move(mir_gv));
}

// 型の幅を数値化（二項演算の結果型決定用）
static int global_type_width(const hir::TypePtr& type) {
    if (!type)
        return 32;
    switch (type->kind) {
        case hir::TypeKind::ULong:
            return 65;
        case hir::TypeKind::Long:
            return 64;
        case hir::TypeKind::UInt:
            return 33;
        default:
            return 32;
    }
}

// LHS/RHSの型のうち広い方を返す
static hir::TypePtr global_wider_type(const hir::TypePtr& lhs, const hir::TypePtr& rhs) {
    return global_type_width(lhs) >= global_type_width(rhs) ? lhs : rhs;
}

// グローバルconst用のコンパイル時定数評価
std::optional<MirConstant> MirLoweringBase::try_global_const_eval(const hir::HirExpr& expr) {
    // リテラルの場合
    if (auto* lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&expr.kind)) {
        if (*lit) {
            MirConstant c;
            c.type = expr.type ? expr.type : hir::make_int();
            c.value = (*lit)->value;
            return c;
        }
    }

    // 変数参照の場合（既登録のグローバルconst変数を伝搬）
    if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&expr.kind)) {
        if (*var) {
            auto it = global_const_values.find((*var)->name);
            if (it != global_const_values.end())
                return it->second;
        }
    }

    // 単項マイナスの場合
    if (auto* unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&expr.kind)) {
        if (*unary && (*unary)->op == hir::HirUnaryOp::Neg && (*unary)->operand) {
            auto inner = try_global_const_eval(*(*unary)->operand);
            if (inner && std::holds_alternative<int64_t>(inner->value)) {
                MirConstant c;
                c.type = inner->type;
                c.value = -std::get<int64_t>(inner->value);
                return c;
            }
        }
    }

    // 二項演算の場合（ビット演算、算術演算）
    if (auto* bin = std::get_if<std::unique_ptr<hir::HirBinary>>(&expr.kind)) {
        if (*bin && (*bin)->lhs && (*bin)->rhs) {
            auto lval = try_global_const_eval(*(*bin)->lhs);
            auto rval = try_global_const_eval(*(*bin)->rhs);
            if (lval && rval && std::holds_alternative<int64_t>(lval->value) &&
                std::holds_alternative<int64_t>(rval->value)) {
                int64_t l = std::get<int64_t>(lval->value);
                int64_t r = std::get<int64_t>(rval->value);
                int64_t result = 0;
                bool ok = true;
                switch ((*bin)->op) {
                    case hir::HirBinaryOp::Add:
                        result = l + r;
                        break;
                    case hir::HirBinaryOp::Sub:
                        result = l - r;
                        break;
                    case hir::HirBinaryOp::Mul:
                        result = l * r;
                        break;
                    case hir::HirBinaryOp::Div:
                        if (r != 0)
                            result = l / r;
                        else
                            ok = false;
                        break;
                    case hir::HirBinaryOp::Mod:
                        if (r != 0)
                            result = l % r;
                        else
                            ok = false;
                        break;
                    case hir::HirBinaryOp::BitAnd:
                        result = l & r;
                        break;
                    case hir::HirBinaryOp::BitOr:
                        result = l | r;
                        break;
                    case hir::HirBinaryOp::BitXor:
                        result = l ^ r;
                        break;
                    case hir::HirBinaryOp::Shl:
                        result = l << r;
                        break;
                    case hir::HirBinaryOp::Shr:
                        result = l >> r;
                        break;
                    default:
                        ok = false;
                        break;
                }
                if (ok) {
                    MirConstant c;
                    // LHS/RHSの型のうち広い方を結果型とする
                    c.type = global_wider_type(lval->type, rval->type);
                    c.value = result;
                    return c;
                }
            }
        }
    }

    return std::nullopt;
}

// 構造体のフィールドインデックスを取得
std::optional<FieldId> MirLoweringBase::get_field_index(const std::string& struct_name,
                                                        const std::string& field_name) {
    auto it = struct_defs.find(struct_name);
    if (it == struct_defs.end()) {
        return std::nullopt;
    }

    const auto* st = it->second;
    for (size_t i = 0; i < st->fields.size(); ++i) {
        if (st->fields[i].name == field_name) {
            return i;
        }
    }
    return std::nullopt;
}

// MIR構造体を生成
// 注: レイアウト（サイズ・アライメント・オフセット）はここでは計算しない。
// かつてプリミティブのみのswitchで手計算していたが、Array・ネスト構造体・スライスがdefault 8/8へ落ちる誤計算であり、
// 正しい計算を持つLoweringContext::layout_size/layout_alignとの二重管理の地雷だったため撤去した（M13）
MirStruct MirLoweringBase::create_mir_struct(const hir::HirStruct& st) {
    MirStruct mir_struct;
    mir_struct.name = st.name;
    mir_struct.is_css = st.is_css;
    mir_struct.is_extern = st.is_extern;

    for (const auto& field : st.fields) {
        MirStructField mir_field;
        mir_field.name = field.name;
        // フィールドの型をtypedef/enum解決
        mir_field.type = resolve_typedef(field.type);
        // フィールド属性を伝播
        mir_field.attributes = field.attributes;
        // フィールドデフォルト値を伝播（SV用）
        mir_field.default_value_str = field.default_value_str;
        mir_struct.fields.push_back(mir_field);
    }

    return mir_struct;
}

// ソースファイルパスの解決（module_rangesから）
std::string MirLoweringBase::resolve_source_file(uint32_t offset) const {
    if (!module_ranges_ || module_ranges_->empty()) {
        return "";
    }

    // module_rangesをバイトオフセット範囲で検索
    for (const auto& range : *module_ranges_) {
        if (offset >= range.start_offset && offset < range.end_offset) {
            return range.file_path;
        }
    }

    return "";
}

}  // namespace cm::mir
