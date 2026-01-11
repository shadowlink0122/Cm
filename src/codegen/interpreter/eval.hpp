#pragma once

#include "types.hpp"

namespace cm::mir::interp {

/// 式・値の評価を担当するクラス
class Evaluator {
   public:
    /// 型のサイズを取得（フィールドオフセット計算用）
    static size_t get_type_size(const hir::TypePtr& type) {
        if (!type)
            return 8;  // デフォルトは64bit
        switch (type->kind) {
            case hir::TypeKind::Bool:
                return sizeof(bool);
            case hir::TypeKind::Char:
                return sizeof(char);
            case hir::TypeKind::Int:
                return sizeof(int32_t);
            case hir::TypeKind::Long:
                return sizeof(int64_t);
            case hir::TypeKind::Float:
                return sizeof(float);
            case hir::TypeKind::Double:
                return sizeof(double);
            case hir::TypeKind::Pointer:
                return sizeof(void*);
            case hir::TypeKind::Struct:
                // 構造体の場合、各フィールドサイズを8バイトと仮定
                return 8;
            default:
                return sizeof(int64_t);
        }
    }

    /// 定数を値に変換
    static Value constant_to_value(const MirConstant& constant) {
        // 型情報がある場合、それに基づいて値を変換
        if (constant.type) {
            if (constant.type->kind == hir::TypeKind::Char) {
                // char型の場合、int64_tからcharに変換
                if (std::holds_alternative<int64_t>(constant.value)) {
                    return Value(static_cast<char>(std::get<int64_t>(constant.value)));
                } else if (std::holds_alternative<char>(constant.value)) {
                    return Value(std::get<char>(constant.value));
                }
            }
            // Pointer型（null）の場合
            if (constant.type->kind == hir::TypeKind::Pointer) {
                if (std::holds_alternative<std::monostate>(constant.value) ||
                    (std::holds_alternative<int64_t>(constant.value) &&
                     std::get<int64_t>(constant.value) == 0)) {
                    // nullポinterとしてPointerValueを作成
                    PointerValue pv;
                    pv.raw_ptr = nullptr;
                    pv.target_local = static_cast<LocalId>(-1);
                    if (constant.type->element_type) {
                        pv.element_type = constant.type->element_type;
                    }
                    return Value(pv);
                }
            }
        }

        // デフォルトの変換
        return std::visit(
            [](auto&& val) -> Value {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return Value{};
                } else if constexpr (std::is_same_v<T, bool>) {
                    return Value(val);
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    return Value(val);
                } else if constexpr (std::is_same_v<T, double>) {
                    return Value(val);
                } else if constexpr (std::is_same_v<T, char>) {
                    return Value(val);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return Value(val);
                }
                return Value{};
            },
            constant.value);
    }

    /// Place から値をロード
    static Value load_from_place(ExecutionContext& ctx, const MirPlace& place) {
        auto it = ctx.locals.find(place.local);
        if (it == ctx.locals.end()) {
            debug::interp::log(
                debug::interp::Id::Load,
                "load_from_place: local _" + std::to_string(place.local) + " not found",
                debug::Level::Debug);
            return Value{};
        }

        Value result = it->second;

        // プロジェクションを適用
        for (const auto& proj : place.projections) {
            if (proj.kind == ProjectionKind::Field) {
                if (result.type() == typeid(StructValue)) {
                    auto& struct_val = std::any_cast<StructValue&>(result);
                    auto field_it = struct_val.fields.find(proj.field_id);
                    if (field_it != struct_val.fields.end()) {
                        result = field_it->second;
                    } else {
                        return Value{};
                    }
                } else if (result.type() == typeid(PointerValue)) {
                    // 外部構造体へのフィールドアクセス（ptr->fieldのDeref後）
                    auto& ptr = std::any_cast<PointerValue&>(result);
                    if (ptr.is_external() && ptr.element_type &&
                        ptr.element_type->kind == hir::TypeKind::Struct) {
                        // 8バイト固定オフセットでアクセス
                        size_t offset = proj.field_id * 8;
                        void* field_ptr = static_cast<char*>(ptr.raw_ptr) + offset;
                        // 読み取った8バイト値
                        int64_t raw_value = *reinterpret_cast<int64_t*>(field_ptr);

                        // ポinterフィールドの可能性を考慮（ヒューリスティック判定）
                        // 小さな整数（-10000〜10000程度）はint値、それ以外はポinter値と推定
                        void* potential_ptr = reinterpret_cast<void*>(raw_value);
                        if (raw_value == 0) {
                            // null ポinter
                            PointerValue pv;
                            pv.raw_ptr = nullptr;
                            pv.target_local = static_cast<LocalId>(-1);
                            pv.element_type = ptr.element_type;
                            result = Value(pv);
                        } else if (raw_value > 0x1000 && raw_value < 0x7FFFFFFFFFFF) {
                            // ポinterアドレスとして有効な範囲
                            PointerValue pv;
                            pv.raw_ptr = potential_ptr;
                            pv.target_local = static_cast<LocalId>(-1);
                            pv.element_type = ptr.element_type;
                            result = Value(pv);
                        } else {
                            // 通常の整数値
                            result = Value(raw_value);
                        }
                    } else {
                        return Value{};
                    }
                } else {
                    return Value{};
                }
            } else if (proj.kind == ProjectionKind::Index) {
                // 配列インデックスアクセス
                // インデックスをローカル変数から取得
                auto idx_it = ctx.locals.find(proj.index_local);
                if (idx_it == ctx.locals.end()) {
                    return Value{};
                }

                int64_t index = 0;
                if (idx_it->second.type() == typeid(int64_t)) {
                    index = std::any_cast<int64_t>(idx_it->second);
                } else if (idx_it->second.type() == typeid(int)) {
                    index = std::any_cast<int>(idx_it->second);
                }

                if (result.type() == typeid(ArrayValue)) {
                    auto& arr = std::any_cast<ArrayValue&>(result);
                    if (index >= 0 && static_cast<size_t>(index) < arr.elements.size()) {
                        result = arr.elements[index];
                    } else {
                        return Value{};  // 範囲外
                    }
                } else if (result.type() == typeid(PointerValue)) {
                    // ポインタ配列アクセス: ptr[i]
                    auto& ptr = std::any_cast<PointerValue&>(result);
                    if (ptr.is_external() && ptr.raw_ptr && ptr.element_type) {
                        // 外部メモリへのオフセットアクセス
                        size_t elem_size = 4;  // デフォルト4バイト
                        switch (ptr.element_type->kind) {
                            case hir::TypeKind::Int:
                                elem_size = 4;
                                break;
                            case hir::TypeKind::Long:
                                elem_size = 8;
                                break;
                            case hir::TypeKind::Float:
                                elem_size = 4;
                                break;
                            case hir::TypeKind::Double:
                                elem_size = 8;
                                break;
                            case hir::TypeKind::Char:
                                elem_size = 1;
                                break;
                            case hir::TypeKind::Bool:
                                elem_size = 1;
                                break;
                            default:
                                elem_size = 8;
                                break;
                        }
                        void* offset_ptr = static_cast<char*>(ptr.raw_ptr) + (index * elem_size);
                        switch (ptr.element_type->kind) {
                            case hir::TypeKind::Int:
                                result = Value(
                                    static_cast<int64_t>(*reinterpret_cast<int32_t*>(offset_ptr)));
                                break;
                            case hir::TypeKind::Long:
                                result = Value(*reinterpret_cast<int64_t*>(offset_ptr));
                                break;
                            case hir::TypeKind::Float:
                                result = Value(
                                    static_cast<double>(*reinterpret_cast<float*>(offset_ptr)));
                                break;
                            case hir::TypeKind::Double:
                                result = Value(*reinterpret_cast<double*>(offset_ptr));
                                break;
                            case hir::TypeKind::Char:
                                result = Value(
                                    static_cast<int64_t>(*reinterpret_cast<char*>(offset_ptr)));
                                break;
                            case hir::TypeKind::Bool:
                                result = Value(*reinterpret_cast<bool*>(offset_ptr));
                                break;
                            default:
                                return Value{};
                        }
                    } else if (ptr.internal_val_ptr && ptr.element_type) {
                        // internal_val_ptrを使用（関数跨ぎでも参照透過性維持）
                        Value& target = *ptr.internal_val_ptr;
                        int64_t base_offset = ptr.array_index.value_or(0);
                        int64_t total_index = base_offset + index;
                        if (target.type() == typeid(ArrayValue)) {
                            auto& arr = std::any_cast<ArrayValue&>(target);
                            if (total_index >= 0 &&
                                static_cast<size_t>(total_index) < arr.elements.size()) {
                                result = arr.elements[total_index];
                            } else {
                                return Value{};  // 範囲外
                            }
                        } else {
                            return Value{};
                        }
                    } else if (ptr.target_local != 0xFFFFFFFF && ptr.element_type) {
                        // 内部メモリポインタ（同一コンテキスト内ローカル変数への参照）
                        auto target_it = ctx.locals.find(ptr.target_local);
                        if (target_it != ctx.locals.end()) {
                            Value& target = target_it->second;
                            // ポインタのオフセット（array_index）とインデックスを合わせて要素を取得
                            int64_t base_offset = ptr.array_index.value_or(0);
                            int64_t total_index = base_offset + index;
                            if (target.type() == typeid(ArrayValue)) {
                                auto& arr = std::any_cast<ArrayValue&>(target);
                                if (total_index >= 0 &&
                                    static_cast<size_t>(total_index) < arr.elements.size()) {
                                    result = arr.elements[total_index];
                                } else {
                                    return Value{};  // 範囲外
                                }
                            } else {
                                return Value{};
                            }
                        } else {
                            return Value{};
                        }
                    } else {
                        return Value{};
                    }
                } else {
                    return Value{};
                }
            } else if (proj.kind == ProjectionKind::Deref) {
                // ポインタのデリファレンス
                if (result.type() == typeid(PointerValue)) {
                    auto& ptr = std::any_cast<PointerValue&>(result);

                    debug::interp::log(
                        debug::interp::Id::Load,
                        std::string("Deref: internal_val_ptr=") +
                            (ptr.internal_val_ptr ? "set" : "null") +
                            ", target_local=" + std::to_string(ptr.target_local) +
                            ", is_external=" + (ptr.is_external() ? "true" : "false"),
                        debug::Level::Debug);

                    // 外部メモリへのポインタの場合
                    if (ptr.is_external()) {
                        // 型に応じて外部メモリから値を読み取る
                        if (ptr.element_type) {
                            switch (ptr.element_type->kind) {
                                case hir::TypeKind::Int:
                                    result = Value(static_cast<int64_t>(
                                        *reinterpret_cast<int32_t*>(ptr.raw_ptr)));
                                    break;
                                case hir::TypeKind::Long:
                                    result = Value(*reinterpret_cast<int64_t*>(ptr.raw_ptr));
                                    break;
                                case hir::TypeKind::Float:
                                    result = Value(static_cast<double>(
                                        *reinterpret_cast<float*>(ptr.raw_ptr)));
                                    break;
                                case hir::TypeKind::Double:
                                    result = Value(*reinterpret_cast<double*>(ptr.raw_ptr));
                                    break;
                                case hir::TypeKind::Bool:
                                    result = Value(*reinterpret_cast<bool*>(ptr.raw_ptr));
                                    break;
                                case hir::TypeKind::Char:
                                    result = Value(*reinterpret_cast<char*>(ptr.raw_ptr));
                                    break;
                                case hir::TypeKind::Struct: {
                                    // 構造体の場合、PointerValueを保持して後続のフィールドアクセスで処理
                                    // resultをPointerValueのまま保持
                                    // 次のField projectionで処理される
                                    break;
                                }
                                default:
                                    // デフォルトはint64_t
                                    result = Value(*reinterpret_cast<int64_t*>(ptr.raw_ptr));
                                    break;
                            }
                        } else {
                            // 型情報がない場合はint64_tとして読む
                            result = Value(*reinterpret_cast<int64_t*>(ptr.raw_ptr));
                        }
                        continue;
                    }

                    // ターゲットローカル変数から値を取得
                    // internal_val_ptrがあればそれを使用（コンテキスト跨ぎ対応）
                    Value* target_value = nullptr;
                    if (ptr.internal_val_ptr) {
                        target_value = ptr.internal_val_ptr;
                    } else {
                        auto target_it = ctx.locals.find(ptr.target_local);
                        if (target_it != ctx.locals.end()) {
                            target_value = &target_it->second;
                        } else if (ptr.is_external()) {
                            // is_external() checks raw_ptr, handled above loop logic?
                            // No, loop continues if is_external is true at start.
                            // So here we only handle local lookup failure.
                        }
                    }

                    if (target_value) {
                        // 配列要素への参照の場合
                        if (ptr.array_index.has_value()) {
                            if (target_value->type() == typeid(ArrayValue)) {
                                auto& arr = std::any_cast<ArrayValue&>(*target_value);
                                int64_t idx = ptr.array_index.value();
                                if (idx >= 0 && static_cast<size_t>(idx) < arr.elements.size()) {
                                    result = arr.elements[idx];
                                } else {
                                    return Value{};
                                }
                            } else {
                                return Value{};
                            }
                        }
                        // 構造体フィールドへの参照の場合
                        else if (ptr.field_index.has_value()) {
                            if (target_value->type() == typeid(StructValue)) {
                                auto& sv = std::any_cast<StructValue&>(*target_value);
                                auto field_it =
                                    sv.fields.find(static_cast<FieldId>(ptr.field_index.value()));
                                if (field_it != sv.fields.end()) {
                                    result = field_it->second;
                                } else {
                                    return Value{};
                                }
                            } else {
                                return Value{};
                            }
                        } else {
                            result = *target_value;
                            debug::interp::log(
                                debug::interp::Id::Load,
                                std::string("Deref result: type=") +
                                    (result.type() == typeid(StructValue)    ? "StructValue"
                                     : result.type() == typeid(int64_t)      ? "int64_t"
                                     : result.type() == typeid(PointerValue) ? "PointerValue"
                                                                             : "other"),
                                debug::Level::Debug);
                        }
                    } else {
                        return Value{};
                    }
                } else {
                    return Value{};
                }
            }
        }

        return result;
    }

    /// Place に値を格納
    static void store_to_place(ExecutionContext& ctx, const MirPlace& place, Value value) {
        if (place.projections.empty()) {
            // 直接格納
            ctx.locals[place.local] = value;
        } else {
            // プロジェクションがある場合
            auto it = ctx.locals.find(place.local);
            if (it == ctx.locals.end()) {
                // 新しいStructValueまたはArrayValueを作成
                // 最初のプロジェクションの種類で判断
                if (place.projections[0].kind == ProjectionKind::Index) {
                    ArrayValue av;
                    av.elements.resize(100);  // デフォルトサイズ
                    ctx.locals[place.local] = Value(av);
                } else {
                    StructValue sv;
                    ctx.locals[place.local] = Value(sv);
                }
                it = ctx.locals.find(place.local);
            }

            // プロジェクションを辿って格納先を見つける
            Value* current = &it->second;
            for (size_t i = 0; i < place.projections.size() - 1; ++i) {
                const auto& proj = place.projections[i];
                if (proj.kind == ProjectionKind::Field) {
                    if (current->type() == typeid(StructValue)) {
                        auto& sv = std::any_cast<StructValue&>(*current);
                        // フィールドが存在しない場合は空のValueを作成
                        if (sv.fields.find(proj.field_id) == sv.fields.end()) {
                            // 次のプロジェクションの種類で初期値を決定
                            if (i + 1 < place.projections.size()) {
                                if (place.projections[i + 1].kind == ProjectionKind::Index) {
                                    sv.fields[proj.field_id] = Value(ArrayValue{});
                                } else {
                                    sv.fields[proj.field_id] = Value(StructValue{});
                                }
                            } else {
                                sv.fields[proj.field_id] = Value(StructValue{});
                            }
                        }
                        current = &sv.fields[proj.field_id];
                    }
                } else if (proj.kind == ProjectionKind::Index) {
                    auto idx_it = ctx.locals.find(proj.index_local);
                    if (idx_it != ctx.locals.end()) {
                        int64_t index = 0;
                        if (idx_it->second.type() == typeid(int64_t)) {
                            index = std::any_cast<int64_t>(idx_it->second);
                        } else if (idx_it->second.type() == typeid(int)) {
                            index = std::any_cast<int>(idx_it->second);
                        }
                        if (current->type() == typeid(ArrayValue)) {
                            auto& arr = std::any_cast<ArrayValue&>(*current);
                            if (static_cast<size_t>(index) >= arr.elements.size()) {
                                arr.elements.resize(index + 1);
                            }
                            // 配列要素が空の場合、次のプロジェクションの種類で初期値を決定
                            if (!arr.elements[index].has_value() ||
                                arr.elements[index].type() == typeid(void)) {
                                if (i + 1 < place.projections.size()) {
                                    if (place.projections[i + 1].kind == ProjectionKind::Field) {
                                        arr.elements[index] = Value(StructValue{});
                                    } else if (place.projections[i + 1].kind ==
                                               ProjectionKind::Index) {
                                        arr.elements[index] = Value(ArrayValue{});
                                    }
                                }
                            }
                            current = &arr.elements[index];
                        }
                    }
                } else if (proj.kind == ProjectionKind::Deref) {
                    // ポインタのデリファレンス - 参照先に移動
                    if (current->type() == typeid(PointerValue)) {
                        auto& ptr = std::any_cast<PointerValue&>(*current);

                        // 外部ポinter（malloc経由）の場合
                        if (ptr.is_external()) {
                            // 外部ポinterはそのまま保持し、後続のField処理で使用
                            // currentはPointerValueのまま保持される
                            debug::interp::log(
                                debug::interp::Id::Store,
                                "External pointer Deref, raw_ptr=" +
                                    std::to_string(reinterpret_cast<uintptr_t>(ptr.raw_ptr)),
                                debug::Level::Debug);
                            continue;  // 次のプロジェクションへ
                        }

                        debug::interp::log(
                            debug::interp::Id::Store,
                            "Deref: internal_val_ptr=" +
                                std::to_string(reinterpret_cast<uintptr_t>(ptr.internal_val_ptr)) +
                                ", target_local=" + std::to_string(ptr.target_local),
                            debug::Level::Debug);

                        if (ptr.internal_val_ptr) {
                            current = ptr.internal_val_ptr;
                            debug::interp::log(debug::interp::Id::Store,
                                               "Using internal_val_ptr, current type: " +
                                                   std::string(current->type().name()),
                                               debug::Level::Debug);
                        } else {
                            auto target_it = ctx.locals.find(ptr.target_local);
                            if (target_it != ctx.locals.end()) {
                                current = &target_it->second;
                                debug::interp::log(debug::interp::Id::Store,
                                                   "Using target_local, current type: " +
                                                       std::string(current->type().name()),
                                                   debug::Level::Debug);
                            } else {
                                debug::interp::log(debug::interp::Id::Store,
                                                   "ERROR: target_local not found!",
                                                   debug::Level::Error);
                            }
                        }
                    }
                }
            }

            // 最後のプロジェクションで格納
            const auto& last_proj = place.projections.back();
            if (last_proj.kind == ProjectionKind::Field) {
                if (current->type() == typeid(StructValue)) {
                    auto& sv = std::any_cast<StructValue&>(*current);
                    sv.fields[last_proj.field_id] = value;
                } else if (current->type() == typeid(PointerValue)) {
                    // 外部構造体への書き込み
                    auto& ptr = std::any_cast<PointerValue&>(*current);
                    if (ptr.is_external() && ptr.element_type &&
                        ptr.element_type->kind == hir::TypeKind::Struct) {
                        // 8バイト固定オフセットで書き込み
                        size_t offset = last_proj.field_id * 8;
                        void* field_ptr = static_cast<char*>(ptr.raw_ptr) + offset;

                        // int64_tとして書き込み（ポinterも8バイトなので対応）
                        if (value.type() == typeid(int64_t)) {
                            *reinterpret_cast<int64_t*>(field_ptr) = std::any_cast<int64_t>(value);
                        } else if (value.type() == typeid(double)) {
                            *reinterpret_cast<double*>(field_ptr) = std::any_cast<double>(value);
                        } else if (value.type() == typeid(bool)) {
                            *reinterpret_cast<int64_t*>(field_ptr) =
                                std::any_cast<bool>(value) ? 1 : 0;
                        } else if (value.type() == typeid(PointerValue)) {
                            auto& pv = std::any_cast<PointerValue&>(value);
                            *reinterpret_cast<void**>(field_ptr) = pv.raw_ptr;
                        }
                    }
                }
            } else if (last_proj.kind == ProjectionKind::Index) {
                // 配列インデックスへの格納
                auto idx_it = ctx.locals.find(last_proj.index_local);
                if (idx_it != ctx.locals.end()) {
                    int64_t index = 0;
                    if (idx_it->second.type() == typeid(int64_t)) {
                        index = std::any_cast<int64_t>(idx_it->second);
                    } else if (idx_it->second.type() == typeid(int)) {
                        index = std::any_cast<int>(idx_it->second);
                    }
                    if (current->type() == typeid(ArrayValue)) {
                        auto& arr = std::any_cast<ArrayValue&>(*current);
                        if (static_cast<size_t>(index) >= arr.elements.size()) {
                            arr.elements.resize(index + 1);
                        }
                        arr.elements[index] = value;
                    } else if (current->type() == typeid(PointerValue)) {
                        // ポインタ配列への書き込み: ptr[i] = value
                        auto& ptr = std::any_cast<PointerValue&>(*current);
                        if (ptr.is_external() && ptr.raw_ptr && ptr.element_type) {
                            // 外部メモリへのオフセット書き込み
                            size_t elem_size = 4;  // デフォルト4バイト
                            switch (ptr.element_type->kind) {
                                case hir::TypeKind::Int:
                                    elem_size = 4;
                                    break;
                                case hir::TypeKind::Long:
                                    elem_size = 8;
                                    break;
                                case hir::TypeKind::Float:
                                    elem_size = 4;
                                    break;
                                case hir::TypeKind::Double:
                                    elem_size = 8;
                                    break;
                                case hir::TypeKind::Char:
                                    elem_size = 1;
                                    break;
                                case hir::TypeKind::Bool:
                                    elem_size = 1;
                                    break;
                                default:
                                    elem_size = 8;
                                    break;
                            }
                            void* offset_ptr =
                                static_cast<char*>(ptr.raw_ptr) + (index * elem_size);
                            switch (ptr.element_type->kind) {
                                case hir::TypeKind::Int:
                                    if (value.type() == typeid(int64_t)) {
                                        *reinterpret_cast<int32_t*>(offset_ptr) =
                                            static_cast<int32_t>(std::any_cast<int64_t>(value));
                                    }
                                    break;
                                case hir::TypeKind::Long:
                                    if (value.type() == typeid(int64_t)) {
                                        *reinterpret_cast<int64_t*>(offset_ptr) =
                                            std::any_cast<int64_t>(value);
                                    }
                                    break;
                                case hir::TypeKind::Float:
                                    if (value.type() == typeid(double)) {
                                        *reinterpret_cast<float*>(offset_ptr) =
                                            static_cast<float>(std::any_cast<double>(value));
                                    }
                                    break;
                                case hir::TypeKind::Double:
                                    if (value.type() == typeid(double)) {
                                        *reinterpret_cast<double*>(offset_ptr) =
                                            std::any_cast<double>(value);
                                    }
                                    break;
                                case hir::TypeKind::Char:
                                    if (value.type() == typeid(int64_t)) {
                                        *reinterpret_cast<char*>(offset_ptr) =
                                            static_cast<char>(std::any_cast<int64_t>(value));
                                    }
                                    break;
                                case hir::TypeKind::Bool:
                                    if (value.type() == typeid(bool)) {
                                        *reinterpret_cast<bool*>(offset_ptr) =
                                            std::any_cast<bool>(value);
                                    }
                                    break;
                                default:
                                    break;
                            }
                        }
                    }
                }
            } else if (last_proj.kind == ProjectionKind::Deref) {
                // ポインタのデリファレンスへの格納
                if (current->type() == typeid(PointerValue)) {
                    auto& ptr = std::any_cast<PointerValue&>(*current);

                    // 外部メモリへのポインタの場合
                    if (ptr.is_external()) {
                        // 型に応じて外部メモリに値を書き込む
                        if (ptr.element_type) {
                            switch (ptr.element_type->kind) {
                                case hir::TypeKind::Int:
                                    if (value.type() == typeid(int64_t)) {
                                        *reinterpret_cast<int32_t*>(ptr.raw_ptr) =
                                            static_cast<int32_t>(std::any_cast<int64_t>(value));
                                    }
                                    break;
                                case hir::TypeKind::Long:
                                    if (value.type() == typeid(int64_t)) {
                                        *reinterpret_cast<int64_t*>(ptr.raw_ptr) =
                                            std::any_cast<int64_t>(value);
                                    }
                                    break;
                                case hir::TypeKind::Float:
                                    if (value.type() == typeid(double)) {
                                        *reinterpret_cast<float*>(ptr.raw_ptr) =
                                            static_cast<float>(std::any_cast<double>(value));
                                    }
                                    break;
                                case hir::TypeKind::Double:
                                    if (value.type() == typeid(double)) {
                                        *reinterpret_cast<double*>(ptr.raw_ptr) =
                                            std::any_cast<double>(value);
                                    }
                                    break;
                                case hir::TypeKind::Bool:
                                    if (value.type() == typeid(bool)) {
                                        *reinterpret_cast<bool*>(ptr.raw_ptr) =
                                            std::any_cast<bool>(value);
                                    }
                                    break;
                                case hir::TypeKind::Char:
                                    if (value.type() == typeid(char)) {
                                        *reinterpret_cast<char*>(ptr.raw_ptr) =
                                            std::any_cast<char>(value);
                                    }
                                    break;
                                default:
                                    // デフォルトはint64_t
                                    if (value.type() == typeid(int64_t)) {
                                        *reinterpret_cast<int64_t*>(ptr.raw_ptr) =
                                            std::any_cast<int64_t>(value);
                                    }
                                    break;
                            }
                        } else if (value.type() == typeid(int64_t)) {
                            // 型情報がない場合はint64_tとして書く
                            *reinterpret_cast<int64_t*>(ptr.raw_ptr) =
                                std::any_cast<int64_t>(value);
                        }
                        return;
                    }

                    // 配列要素への参照の場合
                    if (ptr.array_index.has_value()) {
                        auto target_it = ctx.locals.find(ptr.target_local);
                        if (target_it != ctx.locals.end() &&
                            target_it->second.type() == typeid(ArrayValue)) {
                            auto& arr = std::any_cast<ArrayValue&>(target_it->second);
                            int64_t idx = ptr.array_index.value();
                            if (idx >= 0) {
                                if (static_cast<size_t>(idx) >= arr.elements.size()) {
                                    arr.elements.resize(idx + 1);
                                }
                                arr.elements[idx] = value;
                            }
                        }
                    }
                    // 構造体フィールドへの参照の場合
                    else if (ptr.field_index.has_value()) {
                        auto target_it = ctx.locals.find(ptr.target_local);
                        if (target_it != ctx.locals.end() &&
                            target_it->second.type() == typeid(StructValue)) {
                            auto& sv = std::any_cast<StructValue&>(target_it->second);
                            sv.fields[static_cast<FieldId>(ptr.field_index.value())] = value;
                        }
                    } else {
                        // internal_val_ptrがある場合はそちらに直接書き込み
                        // （呼び出し元のローカル変数への参照）
                        if (ptr.internal_val_ptr) {
                            *ptr.internal_val_ptr = value;
                        } else {
                            // フォールバック: 現在のコンテキストのローカル変数
                            ctx.locals[ptr.target_local] = value;
                        }
                    }
                }
            }
        }
    }

    /// オペランドを評価
    static Value evaluate_operand(ExecutionContext& ctx, const MirOperand& operand) {
        switch (operand.kind) {
            case MirOperand::Move:
            case MirOperand::Copy: {
                const MirPlace& place = std::get<MirPlace>(operand.data);
                return load_from_place(ctx, place);
            }
            case MirOperand::Constant: {
                const MirConstant& constant = std::get<MirConstant>(operand.data);
                return constant_to_value(constant);
            }
            case MirOperand::FunctionRef: {
                // 関数参照は関数名（文字列）として評価
                const std::string& func_name = std::get<std::string>(operand.data);
                return Value(func_name);
            }
            default:
                return Value{};
        }
    }

    /// 二項演算を評価
    static Value evaluate_binary_op(MirBinaryOp op, const Value& lhs, const Value& rhs) {
        // 整数演算
        if (lhs.type() == typeid(int64_t) && rhs.type() == typeid(int64_t)) {
            int64_t l = std::any_cast<int64_t>(lhs);
            int64_t r = std::any_cast<int64_t>(rhs);

            switch (op) {
                case MirBinaryOp::Add:
                    return Value(l + r);
                case MirBinaryOp::Sub:
                    return Value(l - r);
                case MirBinaryOp::Mul:
                    return Value(l * r);
                case MirBinaryOp::Div:
                    return r != 0 ? Value(l / r) : Value(int64_t(0));
                case MirBinaryOp::Mod:
                    return r != 0 ? Value(l % r) : Value(int64_t(0));
                case MirBinaryOp::BitAnd:
                    return Value(l & r);
                case MirBinaryOp::BitOr:
                    return Value(l | r);
                case MirBinaryOp::BitXor:
                    return Value(l ^ r);
                case MirBinaryOp::Shl:
                    return Value(l << r);
                case MirBinaryOp::Shr:
                    return Value(l >> r);
                case MirBinaryOp::Eq:
                    return Value(l == r);
                case MirBinaryOp::Ne:
                    return Value(l != r);
                case MirBinaryOp::Lt:
                    return Value(l < r);
                case MirBinaryOp::Le:
                    return Value(l <= r);
                case MirBinaryOp::Gt:
                    return Value(l > r);
                case MirBinaryOp::Ge:
                    return Value(l >= r);
                case MirBinaryOp::And:
                    return Value(l && r);
                case MirBinaryOp::Or:
                    return Value(l || r);
            }
        }

        // 浮動小数点演算
        if (lhs.type() == typeid(double) && rhs.type() == typeid(double)) {
            double l = std::any_cast<double>(lhs);
            double r = std::any_cast<double>(rhs);

            switch (op) {
                case MirBinaryOp::Add:
                    return Value(l + r);
                case MirBinaryOp::Sub:
                    return Value(l - r);
                case MirBinaryOp::Mul:
                    return Value(l * r);
                case MirBinaryOp::Div:
                    return Value(l / r);
                case MirBinaryOp::Eq:
                    return Value(l == r);
                case MirBinaryOp::Ne:
                    return Value(l != r);
                case MirBinaryOp::Lt:
                    return Value(l < r);
                case MirBinaryOp::Le:
                    return Value(l <= r);
                case MirBinaryOp::Gt:
                    return Value(l > r);
                case MirBinaryOp::Ge:
                    return Value(l >= r);
                default:
                    break;
            }
        }

        // bool演算
        if (lhs.type() == typeid(bool) && rhs.type() == typeid(bool)) {
            bool l = std::any_cast<bool>(lhs);
            bool r = std::any_cast<bool>(rhs);

            switch (op) {
                case MirBinaryOp::And:
                    return Value(l && r);
                case MirBinaryOp::Or:
                    return Value(l || r);
                case MirBinaryOp::Eq:
                    return Value(l == r);
                case MirBinaryOp::Ne:
                    return Value(l != r);
                default:
                    break;
            }
        }

        // 文字列連結
        if (op == MirBinaryOp::Add && lhs.type() == typeid(std::string) &&
            rhs.type() == typeid(std::string)) {
            return Value(std::any_cast<std::string>(lhs) + std::any_cast<std::string>(rhs));
        }

        // char演算
        if (lhs.type() == typeid(char) && rhs.type() == typeid(char)) {
            char l = std::any_cast<char>(lhs);
            char r = std::any_cast<char>(rhs);

            switch (op) {
                case MirBinaryOp::Eq:
                    return Value(l == r);
                case MirBinaryOp::Ne:
                    return Value(l != r);
                case MirBinaryOp::Lt:
                    return Value(l < r);
                case MirBinaryOp::Le:
                    return Value(l <= r);
                case MirBinaryOp::Gt:
                    return Value(l > r);
                case MirBinaryOp::Ge:
                    return Value(l >= r);
                default:
                    break;
            }
        }

        // ポインタ比較
        if (lhs.type() == typeid(PointerValue) && rhs.type() == typeid(PointerValue)) {
            const auto& l = std::any_cast<const PointerValue&>(lhs);
            const auto& r = std::any_cast<const PointerValue&>(rhs);

            // 外部ポインタの場合はraw_ptrも比較
            bool equal;
            if (l.is_external() || r.is_external()) {
                equal = (l.raw_ptr == r.raw_ptr);
            } else {
                // 同じローカル変数を指し、配列インデックス/フィールドインデックスも同じ場合は等しい
                equal = (l.target_local == r.target_local) && (l.array_index == r.array_index) &&
                        (l.field_index == r.field_index);
            }

            switch (op) {
                case MirBinaryOp::Eq:
                    return Value(equal);
                case MirBinaryOp::Ne:
                    return Value(!equal);
                case MirBinaryOp::Lt:
                case MirBinaryOp::Le:
                case MirBinaryOp::Gt:
                case MirBinaryOp::Ge: {
                    // ポインタの大小比較：raw_ptrまたはarray_indexで比較
                    uintptr_t l_addr = 0;
                    uintptr_t r_addr = 0;

                    if (l.is_external()) {
                        l_addr = reinterpret_cast<uintptr_t>(l.raw_ptr);
                    } else {
                        // 内部ポインタの場合はtarget_localとarray_indexから擬似アドレス生成
                        l_addr = static_cast<uintptr_t>(l.target_local) * 1000000 +
                                 l.array_index.value_or(0);
                    }

                    if (r.is_external()) {
                        r_addr = reinterpret_cast<uintptr_t>(r.raw_ptr);
                    } else {
                        r_addr = static_cast<uintptr_t>(r.target_local) * 1000000 +
                                 r.array_index.value_or(0);
                    }

                    switch (op) {
                        case MirBinaryOp::Lt:
                            return Value(l_addr < r_addr);
                        case MirBinaryOp::Le:
                            return Value(l_addr <= r_addr);
                        case MirBinaryOp::Gt:
                            return Value(l_addr > r_addr);
                        case MirBinaryOp::Ge:
                            return Value(l_addr >= r_addr);
                        default:
                            break;
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // ポインタとnull（空のValue）の比較
        if (lhs.type() == typeid(PointerValue) && !rhs.has_value()) {
            const auto& ptr = std::any_cast<const PointerValue&>(lhs);
            bool is_null = ptr.is_external() ? (ptr.raw_ptr == nullptr)
                                             : (ptr.target_local == static_cast<LocalId>(-1));
            switch (op) {
                case MirBinaryOp::Eq:
                    return Value(is_null);
                case MirBinaryOp::Ne:
                    return Value(!is_null);
                default:
                    break;
            }
        }

        // null（空のValue）とポインタの比較
        if (!lhs.has_value() && rhs.type() == typeid(PointerValue)) {
            const auto& ptr = std::any_cast<const PointerValue&>(rhs);
            bool is_null = ptr.is_external() ? (ptr.raw_ptr == nullptr)
                                             : (ptr.target_local == static_cast<LocalId>(-1));
            switch (op) {
                case MirBinaryOp::Eq:
                    return Value(is_null);
                case MirBinaryOp::Ne:
                    return Value(!is_null);
                default:
                    break;
            }
        }

        // ポインタとnull（int64_t 0）の比較
        if (lhs.type() == typeid(PointerValue) && rhs.type() == typeid(int64_t)) {
            int64_t rhs_val = std::any_cast<int64_t>(rhs);
            if (rhs_val == 0) {  // nullとの比較
                const auto& ptr = std::any_cast<const PointerValue&>(lhs);
                bool is_null = ptr.is_external() ? (ptr.raw_ptr == nullptr)
                                                 : (ptr.target_local == static_cast<LocalId>(-1));
                switch (op) {
                    case MirBinaryOp::Eq:
                        return Value(is_null);
                    case MirBinaryOp::Ne:
                        return Value(!is_null);
                    default:
                        break;
                }
            }
        }

        // null（int64_t 0）とポインタの比較
        if (lhs.type() == typeid(int64_t) && rhs.type() == typeid(PointerValue)) {
            int64_t lhs_val = std::any_cast<int64_t>(lhs);
            if (lhs_val == 0) {  // nullとの比較
                const auto& ptr = std::any_cast<const PointerValue&>(rhs);
                bool is_null = ptr.is_external() ? (ptr.raw_ptr == nullptr)
                                                 : (ptr.target_local == static_cast<LocalId>(-1));
                switch (op) {
                    case MirBinaryOp::Eq:
                        return Value(is_null);
                    case MirBinaryOp::Ne:
                        return Value(!is_null);
                    default:
                        break;
                }
            }
        }

        // ポインタ演算: pointer + int
        if (lhs.type() == typeid(PointerValue) && rhs.type() == typeid(int64_t)) {
            PointerValue ptr = std::any_cast<PointerValue>(lhs);
            int64_t offset = std::any_cast<int64_t>(rhs);

            if (op == MirBinaryOp::Add) {
                if (ptr.array_index.has_value()) {
                    ptr.array_index = ptr.array_index.value() + offset;
                } else {
                    ptr.array_index = offset;
                }
                return Value(ptr);
            }
            if (op == MirBinaryOp::Sub) {
                if (ptr.array_index.has_value()) {
                    ptr.array_index = ptr.array_index.value() - offset;
                } else {
                    ptr.array_index = -offset;
                }
                return Value(ptr);
            }
        }

        // ポインタ演算: int + pointer
        if (lhs.type() == typeid(int64_t) && rhs.type() == typeid(PointerValue)) {
            int64_t offset = std::any_cast<int64_t>(lhs);
            PointerValue ptr = std::any_cast<PointerValue>(rhs);

            if (op == MirBinaryOp::Add) {
                if (ptr.array_index.has_value()) {
                    ptr.array_index = ptr.array_index.value() + offset;
                } else {
                    ptr.array_index = offset;
                }
                return Value(ptr);
            }
        }

        return Value{};
    }

    /// 単項演算を評価
    static Value evaluate_unary_op(MirUnaryOp op, const Value& operand) {
        if (operand.type() == typeid(int64_t)) {
            int64_t val = std::any_cast<int64_t>(operand);
            switch (op) {
                case MirUnaryOp::Neg:
                    return Value(-val);
                case MirUnaryOp::Not:
                    return Value(!val);
                case MirUnaryOp::BitNot:
                    return Value(~val);
            }
        }

        if (operand.type() == typeid(double)) {
            double val = std::any_cast<double>(operand);
            switch (op) {
                case MirUnaryOp::Neg:
                    return Value(-val);
                default:
                    break;
            }
        }

        if (operand.type() == typeid(bool)) {
            bool val = std::any_cast<bool>(operand);
            switch (op) {
                case MirUnaryOp::Not:
                    return Value(!val);
                default:
                    break;
            }
        }

        return Value{};
    }

    /// Rvalue を評価
    static Value evaluate_rvalue(ExecutionContext& ctx, const MirRvalue& rvalue) {
        switch (rvalue.kind) {
            case MirRvalue::Use: {
                auto& data = std::get<MirRvalue::UseData>(rvalue.data);
                return data.operand ? evaluate_operand(ctx, *data.operand) : Value{};
            }
            case MirRvalue::BinaryOp: {
                auto& data = std::get<MirRvalue::BinaryOpData>(rvalue.data);
                Value lhs = evaluate_operand(ctx, *data.lhs);
                Value rhs = evaluate_operand(ctx, *data.rhs);
                return evaluate_binary_op(data.op, lhs, rhs);
            }
            case MirRvalue::UnaryOp: {
                auto& data = std::get<MirRvalue::UnaryOpData>(rvalue.data);
                Value operand = evaluate_operand(ctx, *data.operand);
                return evaluate_unary_op(data.op, operand);
            }
            case MirRvalue::Ref: {
                // アドレス取得：PointerValueを作成
                auto& data = std::get<MirRvalue::RefData>(rvalue.data);
                PointerValue ptr;
                ptr.target_local = data.place.local;

                // プロジェクションがある場合（配列要素や構造体フィールドへの参照など）
                if (!data.place.projections.empty()) {
                    for (const auto& proj : data.place.projections) {
                        if (proj.kind == ProjectionKind::Index) {
                            // インデックスをローカル変数から取得
                            auto idx_it = ctx.locals.find(proj.index_local);
                            if (idx_it != ctx.locals.end()) {
                                if (idx_it->second.type() == typeid(int64_t)) {
                                    ptr.array_index = std::any_cast<int64_t>(idx_it->second);
                                } else if (idx_it->second.type() == typeid(int)) {
                                    ptr.array_index = std::any_cast<int>(idx_it->second);
                                }
                            }
                        } else if (proj.kind == ProjectionKind::Field) {
                            // フィールドインデックスを設定
                            ptr.field_index = proj.field_id;
                        }
                    }
                }

                // 内部参照用ポインタをセット
                auto it = ctx.locals.find(data.place.local);
                if (it != ctx.locals.end()) {
                    ptr.internal_val_ptr = &it->second;
                } else {
                }

                return Value(ptr);
            }
            case MirRvalue::Aggregate: {
                // 集約型（配列・構造体）の構築
                auto& data = std::get<MirRvalue::AggregateData>(rvalue.data);

                if (data.kind.type == AggregateKind::Type::Array) {
                    // 配列リテラル
                    ArrayValue arr;
                    arr.element_type = data.kind.ty ? data.kind.ty->element_type : nullptr;
                    for (const auto& op : data.operands) {
                        if (op) {
                            arr.elements.push_back(evaluate_operand(ctx, *op));
                        }
                    }
                    return Value(arr);
                } else if (data.kind.type == AggregateKind::Type::Struct) {
                    // 構造体リテラル
                    StructValue sv;
                    sv.type_name = data.kind.name;
                    // 構造体フィールドはFieldIdで管理されている想定
                    // operandsはフィールド順に並んでいる
                    for (size_t i = 0; i < data.operands.size(); ++i) {
                        if (data.operands[i]) {
                            sv.fields[static_cast<FieldId>(i)] =
                                evaluate_operand(ctx, *data.operands[i]);
                        }
                    }
                    return Value(sv);
                }
                return Value{};
            }
            case MirRvalue::Cast: {
                // 型キャスト
                auto& data = std::get<MirRvalue::CastData>(rvalue.data);
                Value operand = data.operand ? evaluate_operand(ctx, *data.operand) : Value{};
                // キャスト処理
                if (data.target_type) {
                    if (data.target_type->kind == hir::TypeKind::Int ||
                        data.target_type->kind == hir::TypeKind::Long) {
                        if (operand.type() == typeid(double)) {
                            return Value(static_cast<int64_t>(std::any_cast<double>(operand)));
                        } else if (operand.type() == typeid(bool)) {
                            return Value(
                                static_cast<int64_t>(std::any_cast<bool>(operand) ? 1 : 0));
                        } else if (operand.type() == typeid(char)) {
                            return Value(static_cast<int64_t>(std::any_cast<char>(operand)));
                        } else if (operand.type() == typeid(void*)) {
                            // ポインタから整数へのキャスト
                            return Value(reinterpret_cast<int64_t>(std::any_cast<void*>(operand)));
                        }
                    } else if (data.target_type->kind == hir::TypeKind::Double ||
                               data.target_type->kind == hir::TypeKind::Float) {
                        if (operand.type() == typeid(int64_t)) {
                            return Value(static_cast<double>(std::any_cast<int64_t>(operand)));
                        }
                    } else if (data.target_type->kind == hir::TypeKind::Bool) {
                        if (operand.type() == typeid(int64_t)) {
                            return Value(std::any_cast<int64_t>(operand) != 0);
                        }
                    } else if (data.target_type->kind == hir::TypeKind::Pointer) {
                        // 整数からポインタへのキャスト（nullを含む）
                        if (operand.type() == typeid(int64_t)) {
                            int64_t val = std::any_cast<int64_t>(operand);
                            PointerValue pv;
                            pv.raw_ptr = reinterpret_cast<void*>(val);
                            pv.target_local = static_cast<LocalId>(-1);
                            if (data.target_type->element_type) {
                                pv.element_type = data.target_type->element_type;
                            }
                            return Value(pv);
                        }
                        // PointerValueのキャスト（型情報を更新）
                        if (operand.type() == typeid(PointerValue)) {
                            PointerValue pv = std::any_cast<PointerValue>(operand);
                            // ターゲット型のポイント先の型情報を設定
                            if (data.target_type->element_type) {
                                pv.element_type = data.target_type->element_type;
                            }
                            return Value(pv);
                        }
                        // ポインタ間のキャスト（型のみ変更）
                        if (operand.type() == typeid(void*)) {
                            return operand;  // 既にvoid*なのでそのまま
                        }
                    }
                }
                return operand;
            }
            case MirRvalue::FormatConvert: {
                // フォーマット変換（文字列補間用）
                auto& data = std::get<MirRvalue::FormatConvertData>(rvalue.data);
                return data.operand ? evaluate_operand(ctx, *data.operand) : Value{};
            }
            default:
                return Value{};
        }
    }
};

}  // namespace cm::mir::interp
