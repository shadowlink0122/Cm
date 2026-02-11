#pragma once

#include "../common/span.hpp"
#include "../hir/types.hpp"

#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace cm::mir {

// ============================================================
// 前方宣言
// ============================================================
struct MirOperand;
struct MirPlace;
struct MirRvalue;
struct MirStatement;
struct MirTerminator;
struct BasicBlock;
struct MirFunction;

using MirOperandPtr = std::unique_ptr<MirOperand>;
using MirPlacePtr = std::unique_ptr<MirPlace>;
using MirRvaluePtr = std::unique_ptr<MirRvalue>;
using MirStatementPtr = std::unique_ptr<MirStatement>;
using MirTerminatorPtr = std::unique_ptr<MirTerminator>;
using BasicBlockPtr = std::unique_ptr<BasicBlock>;
using MirFunctionPtr = std::unique_ptr<MirFunction>;

// ============================================================
// 基本型定義
// ============================================================
using BlockId = uint32_t;
using LocalId = uint32_t;
using FieldId = uint32_t;

// 特別なブロックID
constexpr BlockId ENTRY_BLOCK = 0;
constexpr BlockId INVALID_BLOCK = std::numeric_limits<uint32_t>::max();

// ============================================================
// Place（場所）- メモリ位置を表現
// ============================================================

// Place投影（フィールドアクセス、配列インデックス等）
enum class ProjectionKind {
    Field,  // 構造体フィールド
    Index,  // 配列/スライスのインデックス
    Deref,  // ポインタ/参照の間接参照
};

struct PlaceProjection {
    ProjectionKind kind;
    union {
        FieldId field_id;  // Field の場合
        LocalId index_local;  // Index の場合（インデックスを保持するローカル変数）
    };
    hir::TypePtr result_type;   // 投影後の型
    hir::TypePtr pointee_type;  // Derefの場合のpointee type

    // 既存の互換性のためのファクトリメソッド
    static PlaceProjection field(FieldId id) {
        PlaceProjection p;
        p.kind = ProjectionKind::Field;
        p.field_id = id;
        return p;
    }

    static PlaceProjection index(LocalId local) {
        PlaceProjection p;
        p.kind = ProjectionKind::Index;
        p.index_local = local;
        return p;
    }

    static PlaceProjection deref() {
        PlaceProjection p;
        p.kind = ProjectionKind::Deref;
        return p;
    }

    // 型情報を持つ新しいファクトリメソッド
    static PlaceProjection field(FieldId id, hir::TypePtr result_type) {
        PlaceProjection p;
        p.kind = ProjectionKind::Field;
        p.field_id = id;
        p.result_type = result_type;
        return p;
    }

    static PlaceProjection index(LocalId local, hir::TypePtr result_type) {
        PlaceProjection p;
        p.kind = ProjectionKind::Index;
        p.index_local = local;
        p.result_type = result_type;
        return p;
    }

    static PlaceProjection deref(hir::TypePtr result_type, hir::TypePtr pointee_type) {
        PlaceProjection p;
        p.kind = ProjectionKind::Deref;
        p.result_type = result_type;
        p.pointee_type = pointee_type;
        return p;
    }
};

struct MirPlace {
    LocalId local;
    std::vector<PlaceProjection> projections;
    hir::TypePtr type;          // このPlaceが示す値の型
    hir::TypePtr pointee_type;  // ポインタの場合のpointee type

    // 既存の互換性のためのコンストラクタ
    MirPlace(LocalId l) : local(l) {}
    MirPlace(LocalId l, std::vector<PlaceProjection> p) : local(l), projections(std::move(p)) {}

    // 型情報を持つコンストラクタ
    MirPlace(LocalId l, hir::TypePtr t) : local(l), type(t) {
        // ポインタ型の場合、pointee_typeを設定
        if (t && t->kind == hir::TypeKind::Pointer) {
            pointee_type = t->element_type;
        }
    }

    MirPlace(LocalId l, std::vector<PlaceProjection> p, hir::TypePtr t)
        : local(l), projections(std::move(p)), type(t) {
        // ポインタ型の場合、pointee_typeを設定
        if (t && t->kind == hir::TypeKind::Pointer) {
            pointee_type = t->element_type;
        }
    }
};

// ============================================================
// Operand（オペランド）- 値を表現
// ============================================================
struct MirConstant {
    std::variant<std::monostate,  // unit/void
                 bool, int64_t, double, char, std::string>
        value;
    hir::TypePtr type;
};

struct MirOperand {
    enum Kind {
        Move,        // 所有権を移動
        Copy,        // 値をコピー
        Constant,    // 定数
        FunctionRef  // 関数参照
    };

    Kind kind;
    std::variant<MirPlace, MirConstant, std::string> data;  // stringは関数名用
    hir::TypePtr type;                                      // オペランドの型情報

    // デフォルトコンストラクタ
    MirOperand() : kind(Constant), data(MirConstant{}) {}

    // 既存の互換性のためのファクトリメソッド（型なし）
    static MirOperandPtr move(MirPlace place) {
        auto op = std::make_unique<MirOperand>();
        op->kind = Move;
        op->data = std::move(place);
        return op;
    }

    static MirOperandPtr copy(MirPlace place) {
        auto op = std::make_unique<MirOperand>();
        op->kind = Copy;
        op->data = std::move(place);
        return op;
    }

    // 型情報を持つ新しいファクトリメソッド
    static MirOperandPtr move(MirPlace place, hir::TypePtr type) {
        auto op = std::make_unique<MirOperand>();
        op->kind = Move;
        op->data = std::move(place);
        op->type = type;
        return op;
    }

    static MirOperandPtr copy(MirPlace place, hir::TypePtr type) {
        auto op = std::make_unique<MirOperand>();
        op->kind = Copy;
        op->data = std::move(place);
        op->type = type;
        return op;
    }

    static MirOperandPtr constant(MirConstant c) {
        auto op = std::make_unique<MirOperand>();
        op->kind = Constant;
        op->data = std::move(c);
        // Constantの場合、MirConstant自体に型情報があるので、それを使用
        op->type = c.type;
        return op;
    }

    static MirOperandPtr function_ref(std::string func_name, hir::TypePtr type = nullptr) {
        auto op = std::make_unique<MirOperand>();
        op->kind = FunctionRef;
        op->data = std::move(func_name);
        op->type = type;
        return op;
    }
};

// ============================================================
// Rvalue（右辺値）
// ============================================================
enum class MirBinaryOp {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    BitAnd,
    BitOr,
    BitXor,
    Shl,
    Shr,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    And,
    Or,
};

enum class MirUnaryOp {
    Neg,     // 算術否定
    Not,     // 論理否定
    BitNot,  // ビット否定
};

enum class BorrowKind {
    Shared,   // 共有借用 (&)
    Mutable,  // 可変借用 (&mut)
};

// 集約型の種類
struct AggregateKind {
    enum Type {
        Array,   // 配列
        Tuple,   // タプル
        Struct,  // 構造体
    };

    Type type;
    std::string name;  // Structの場合の型名
    hir::TypePtr ty;
};

struct MirRvalue {
    enum Kind {
        Use,            // オペランドの使用
        BinaryOp,       // 二項演算
        UnaryOp,        // 単項演算
        Ref,            // 借用
        Aggregate,      // 集約型の構築
        Cast,           // 型変換
        FormatConvert,  // フォーマット変換
    };

    Kind kind;

    // デフォルトコンストラクタ
    MirRvalue() : kind(Use), data(UseData{}) {}

    // 各種データ
    struct UseData {
        MirOperandPtr operand;
    };

    struct BinaryOpData {
        MirBinaryOp op;
        MirOperandPtr lhs;
        MirOperandPtr rhs;
        hir::TypePtr result_type;  // 結果の型（ポインタ演算で必要）
    };

    struct UnaryOpData {
        MirUnaryOp op;
        MirOperandPtr operand;
    };

    struct RefData {
        BorrowKind borrow;
        MirPlace place;
    };

    struct AggregateData {
        AggregateKind kind;
        std::vector<MirOperandPtr> operands;
    };

    struct CastData {
        MirOperandPtr operand;
        hir::TypePtr target_type;
    };

    struct FormatConvertData {
        MirOperandPtr operand;
        std::string format_spec;  // "x", "X", "b", "o", ".2" など
    };

    std::variant<UseData, BinaryOpData, UnaryOpData, RefData, AggregateData, CastData,
                 FormatConvertData>
        data;

    static MirRvaluePtr use(MirOperandPtr op) {
        auto rv = std::make_unique<MirRvalue>();
        rv->kind = Use;
        rv->data = UseData{std::move(op)};
        return rv;
    }

    static MirRvaluePtr binary(MirBinaryOp op, MirOperandPtr lhs, MirOperandPtr rhs,
                               hir::TypePtr result_type = nullptr) {
        auto rv = std::make_unique<MirRvalue>();
        rv->kind = BinaryOp;
        rv->data = BinaryOpData{op, std::move(lhs), std::move(rhs), std::move(result_type)};
        return rv;
    }

    static MirRvaluePtr unary(MirUnaryOp op, MirOperandPtr operand) {
        auto rv = std::make_unique<MirRvalue>();
        rv->kind = UnaryOp;
        rv->data = UnaryOpData{op, std::move(operand)};
        return rv;
    }

    static MirRvaluePtr format_convert(MirOperandPtr op, const std::string& format_spec) {
        auto rv = std::make_unique<MirRvalue>();
        rv->kind = FormatConvert;
        rv->data = FormatConvertData{std::move(op), format_spec};
        return rv;
    }

    static MirRvaluePtr ref(MirPlace place, bool is_mutable) {
        auto rv = std::make_unique<MirRvalue>();
        rv->kind = Ref;
        rv->data = RefData{is_mutable ? BorrowKind::Mutable : BorrowKind::Shared, std::move(place)};
        return rv;
    }

    static MirRvaluePtr cast(MirOperandPtr operand, hir::TypePtr target_type) {
        auto rv = std::make_unique<MirRvalue>();
        rv->kind = Cast;
        rv->data = CastData{std::move(operand), target_type};
        return rv;
    }
};

// ============================================================
// Statement（文）
// ============================================================
struct MirStatement {
    enum Kind {
        Assign,       // 代入: place = rvalue
        StorageLive,  // 変数の有効範囲開始
        StorageDead,  // 変数の有効範囲終了
        Nop,          // 何もしない（最適化で削除される）
        Asm,          // インラインアセンブリ
    };

    Kind kind;
    Span span;
    bool no_opt = false;  // 最適化禁止フラグ（must{}ブロック内の文）

    // デフォルトコンストラクタ
    MirStatement() : kind(Nop), no_opt(false), data(std::monostate{}) {}

    struct AssignData {
        MirPlace place;
        MirRvaluePtr rvalue;
    };

    struct StorageData {
        LocalId local;
    };

    // asmオペランド（制約+ローカル変数IDまたは定数値）
    struct MirAsmOperand {
        std::string constraint;  // "+r", "=r", "r", "i", "n", etc.
        LocalId local_id;        // 変数のローカルID（is_constant=falseの場合）
        bool is_constant;        // 定数値かどうか（i,n制約用）
        int64_t const_value;     // 定数値（is_constant=trueの場合）

        // デフォルトコンストラクタ
        MirAsmOperand() : local_id(0), is_constant(false), const_value(0) {}

        // 変数用コンストラクタ
        MirAsmOperand(std::string c, LocalId id)
            : constraint(std::move(c)), local_id(id), is_constant(false), const_value(0) {}

        // 定数用コンストラクタ
        MirAsmOperand(std::string c, int64_t val)
            : constraint(std::move(c)), local_id(0), is_constant(true), const_value(val) {}
    };

    struct AsmData {
        std::string code;
        bool is_must;  // must修飾（最適化抑制）
        std::vector<std::string> clobbers;
        std::vector<MirAsmOperand> operands;  // オペランド情報
    };

    std::variant<std::monostate,  // Nop
                 AssignData, StorageData, AsmData>
        data;

    static MirStatementPtr assign(MirPlace place, MirRvaluePtr rvalue, Span s = {}) {
        auto stmt = std::make_unique<MirStatement>();
        stmt->kind = Assign;
        stmt->span = s;
        stmt->data = AssignData{std::move(place), std::move(rvalue)};
        return stmt;
    }

    static MirStatementPtr storage_live(LocalId local, Span s = {}) {
        auto stmt = std::make_unique<MirStatement>();
        stmt->kind = StorageLive;
        stmt->span = s;
        stmt->data = StorageData{local};
        return stmt;
    }

    static MirStatementPtr storage_dead(LocalId local, Span s = {}) {
        auto stmt = std::make_unique<MirStatement>();
        stmt->kind = StorageDead;
        stmt->span = s;
        stmt->data = StorageData{local};
        return stmt;
    }

    // インラインアセンブリ用
    static MirStatementPtr asm_stmt(std::string code, bool is_must = true,
                                    std::vector<MirAsmOperand> operands = {},
                                    std::vector<std::string> clobbers = {}, Span s = {}) {
        auto stmt = std::make_unique<MirStatement>();
        stmt->kind = Asm;
        stmt->span = s;
        stmt->data = AsmData{std::move(code), is_must, std::move(clobbers), std::move(operands)};
        return stmt;
    }
};

// ============================================================
// Terminator（終端命令）
// ============================================================
struct MirTerminator {
    enum Kind {
        Goto,         // 無条件ジャンプ
        SwitchInt,    // 整数値による分岐
        Return,       // 関数からのリターン
        Unreachable,  // 到達不可能
        Call,         // 関数呼び出し
    };

    Kind kind;
    Span span;

    // デフォルトコンストラクタ
    MirTerminator() : kind(Unreachable), data(std::monostate{}) {}

    struct GotoData {
        BlockId target;
    };

    struct SwitchIntData {
        MirOperandPtr discriminant;
        std::vector<std::pair<int64_t, BlockId>> targets;
        BlockId otherwise;
    };

    struct CallData {
        MirOperandPtr func;
        std::vector<MirOperandPtr> args;
        std::optional<MirPlace> destination;  // 戻り値の格納先
        BlockId success;                      // 成功時の遷移先
        std::optional<BlockId> unwind;        // パニック時の遷移先

        // インターフェースメソッド呼び出し用（オプション）
        std::string interface_name;  // インターフェース名（空なら通常の関数呼び出し）
        std::string method_name;  // メソッド名
        bool is_virtual = false;  // vtable経由の呼び出しか

        // 末尾呼び出し最適化（LLVM tail call attribute）
        bool is_tail_call = false;  // 末尾位置の自己呼び出し

        // async関数をawaitで呼び出しているか（同期実行する）
        bool is_awaited = false;
    };

    std::variant<std::monostate,  // Return, Unreachable
                 GotoData, SwitchIntData, CallData>
        data;

    static MirTerminatorPtr goto_block(BlockId target, Span s = {}) {
        auto term = std::make_unique<MirTerminator>();
        term->kind = Goto;
        term->span = s;
        term->data = GotoData{target};
        return term;
    }

    static MirTerminatorPtr return_value(Span s = {}) {
        auto term = std::make_unique<MirTerminator>();
        term->kind = Return;
        term->span = s;
        return term;
    }

    static MirTerminatorPtr unreachable(Span s = {}) {
        auto term = std::make_unique<MirTerminator>();
        term->kind = Unreachable;
        term->span = s;
        return term;
    }

    static MirTerminatorPtr switch_int(MirOperandPtr discriminant,
                                       std::vector<std::pair<int64_t, BlockId>> targets,
                                       BlockId otherwise, Span s = {}) {
        auto term = std::make_unique<MirTerminator>();
        term->kind = SwitchInt;
        term->span = s;
        term->data = SwitchIntData{std::move(discriminant), std::move(targets), otherwise};
        return term;
    }
};

// ============================================================
// 基本ブロック
// ============================================================
struct BasicBlock {
    BlockId id;
    std::vector<MirStatementPtr> statements;
    MirTerminatorPtr terminator;

    // CFG情報（解析用）
    std::vector<BlockId> predecessors;
    std::vector<BlockId> successors;

    BasicBlock(BlockId i) : id(i) {}

    void add_statement(MirStatementPtr stmt) { statements.push_back(std::move(stmt)); }

    void set_terminator(MirTerminatorPtr term) {
        terminator = std::move(term);
        update_successors();
    }

    void update_successors() {
        successors.clear();
        if (!terminator)
            return;

        switch (terminator->kind) {
            case MirTerminator::Goto: {
                auto& data = std::get<MirTerminator::GotoData>(terminator->data);
                successors.push_back(data.target);
                break;
            }
            case MirTerminator::SwitchInt: {
                auto& data = std::get<MirTerminator::SwitchIntData>(terminator->data);
                for (const auto& [_, target] : data.targets) {
                    successors.push_back(target);
                }
                successors.push_back(data.otherwise);
                break;
            }
            case MirTerminator::Call: {
                auto& data = std::get<MirTerminator::CallData>(terminator->data);
                successors.push_back(data.success);
                if (data.unwind) {
                    successors.push_back(*data.unwind);
                }
                break;
            }
            default:
                break;
        }
    }
};

// ============================================================
// ローカル変数情報
// ============================================================
struct LocalDecl {
    LocalId id;
    std::string name;  // デバッグ用の名前
    hir::TypePtr type;
    bool is_mutable;
    bool is_user_variable;  // ユーザー定義の変数か、コンパイラ生成の一時変数か
    bool is_static = false;  // static変数（関数呼び出し間で値が保持される）

    // クロージャ関数ポインタの場合のキャプチャ情報
    bool is_closure = false;
    std::string closure_func_name;         // 実際のクロージャ関数名
    std::vector<LocalId> captured_locals;  // キャプチャされた変数のローカルID

    LocalDecl(LocalId i, std::string n, hir::TypePtr t, bool mut = true, bool user = true,
              bool is_static_ = false)
        : id(i),
          name(std::move(n)),
          type(std::move(t)),
          is_mutable(mut),
          is_user_variable(user),
          is_static(is_static_) {}
};

// ============================================================
// MIR関数
// ============================================================
struct MirFunction {
    std::string name;
    std::string module_path;  // モジュールパス（例："std::io", ""は現在のモジュール）
    std::string package_name;         // パッケージ名 (FFI用)
    bool is_export = false;           // エクスポートされているか
    bool is_extern = false;           // extern "C" 関数か
    bool is_variadic = false;         // 可変長引数（FFI用）
    bool is_async = false;            // async関数（JSバックエンド用）
    std::vector<LocalDecl> locals;    // ローカル変数（引数も含む）
    std::vector<LocalId> arg_locals;  // 引数に対応するローカルID
    LocalId return_local;             // 戻り値用のローカル（_0）
    std::vector<BasicBlockPtr> basic_blocks;
    BlockId entry_block = ENTRY_BLOCK;

    // ローカル変数の追加
    LocalId add_local(std::string name, hir::TypePtr type, bool is_mutable = true,
                      bool is_user = true, bool is_static = false) {
        LocalId id = locals.size();
        locals.emplace_back(id, std::move(name), std::move(type), is_mutable, is_user, is_static);
        return id;
    }

    // 基本ブロックの追加
    BlockId add_block() {
        BlockId id = basic_blocks.size();
        basic_blocks.push_back(std::make_unique<BasicBlock>(id));
        return id;
    }

    BasicBlock* get_block(BlockId id) {
        if (id < basic_blocks.size()) {
            return basic_blocks[id].get();
        }
        return nullptr;
    }

    const BasicBlock* get_block(BlockId id) const {
        if (id < basic_blocks.size()) {
            return basic_blocks[id].get();
        }
        return nullptr;
    }

    // CFGの構築（predecessorの計算）
    void build_cfg() {
        // まずすべてのpredecessorをクリア
        for (auto& block : basic_blocks) {
            if (!block)
                continue;
            block->predecessors.clear();
            // terminatorの変更を反映させるためにsuccessorsも更新
            block->update_successors();
        }

        // successorからpredecessorを計算
        for (size_t i = 0; i < basic_blocks.size(); ++i) {
            if (!basic_blocks[i])
                continue;
            for (BlockId succ : basic_blocks[i]->successors) {
                if (auto* succ_block = get_block(succ)) {
                    succ_block->predecessors.push_back(i);
                }
            }
        }
    }
};

// ============================================================
// MIRプログラム
// ============================================================
// 構造体定義
struct MirStructField {
    std::string name;
    hir::TypePtr type;
    uint32_t offset;  // バイトオフセット（将来の最適化用）
};

struct MirStruct {
    std::string name;
    std::string module_path;  // モジュールパス
    bool is_export = false;   // エクスポートされているか
    std::vector<MirStructField> fields;
    uint32_t size;   // 構造体全体のサイズ
    uint32_t align;  // アライメント要求
    bool is_css = false;

    // インターフェース実装情報
    std::vector<std::string> implemented_interfaces;
};

using MirStructPtr = std::unique_ptr<MirStruct>;

// ============================================================
// Enum定義（Tagged Union対応）
// ============================================================
struct MirEnumMember {
    std::string name;
    int64_t tag_value;  // タグ値（バリアントを識別）
    // Associated data フィールド（Tagged Union用）
    std::vector<std::pair<std::string, hir::TypePtr>> fields;

    // Associated dataを持つかどうか
    bool has_data() const { return !fields.empty(); }
};

struct MirEnum {
    std::string name;
    std::string module_path;
    bool is_export = false;
    std::vector<MirEnumMember> members;

    // Tagged Unionかどうか（dataを持つメンバーがあるか）
    bool is_tagged_union() const {
        for (const auto& m : members) {
            if (m.has_data())
                return true;
        }
        return false;
    }

    // 最大ペイロードサイズを計算（Tagged Union用）
    uint32_t max_payload_size() const {
        uint32_t maxSize = 0;
        for (const auto& member : members) {
            uint32_t memberSize = 0;
            for (const auto& [name, type] : member.fields) {
                if (!type)
                    continue;
                switch (type->kind) {
                    case hir::TypeKind::Bool:
                    case hir::TypeKind::Char:
                    case hir::TypeKind::Tiny:
                    case hir::TypeKind::UTiny:
                        memberSize += 1;
                        break;
                    case hir::TypeKind::Short:
                    case hir::TypeKind::UShort:
                        memberSize += 2;
                        break;
                    case hir::TypeKind::Int:
                    case hir::TypeKind::UInt:
                    case hir::TypeKind::Float:
                        memberSize += 4;
                        break;
                    case hir::TypeKind::Long:
                    case hir::TypeKind::ULong:
                    case hir::TypeKind::Double:
                    case hir::TypeKind::Pointer:
                    case hir::TypeKind::String:
                        memberSize += 8;
                        break;
                    default:
                        memberSize += 8;  // デフォルトはポインタサイズ
                        break;
                }
            }
            if (memberSize > maxSize) {
                maxSize = memberSize;
            }
        }
        return maxSize;
    }
};

using MirEnumPtr = std::unique_ptr<MirEnum>;

// インターフェースメソッド定義
struct MirInterfaceMethod {
    std::string name;
    hir::TypePtr return_type;
    std::vector<hir::TypePtr> param_types;
};

// 演算子の種類
enum class MirOperatorKind {
    Eq,   // ==
    Ne,   // != (自動導出)
    Lt,   // <
    Gt,   // > (自動導出)
    Le,   // <= (自動導出)
    Ge,   // >= (自動導出)
    Add,  // +
    Sub,  // -
    Mul,  // *
    Div,  // /
    Mod,  // %
};

// 演算子シグネチャ
struct MirOperatorSig {
    MirOperatorKind op;
    hir::TypePtr return_type;
    std::vector<hir::TypePtr> param_types;
};

// インターフェース定義
struct MirInterface {
    std::string name;
    std::vector<MirInterfaceMethod> methods;
    std::vector<MirOperatorSig> operators;    // 演算子シグネチャ
    std::vector<std::string> generic_params;  // ジェネリックパラメータ
};

using MirInterfacePtr = std::unique_ptr<MirInterface>;

// vtableエントリ（動的ディスパッチ用）
struct VTableEntry {
    std::string method_name;
    std::string impl_function_name;  // 実際に呼び出す関数名
};

// vtable（型ごとのインターフェース実装）
struct VTable {
    std::string type_name;       // 実装する型
    std::string interface_name;  // 実装するインターフェース
    std::vector<VTableEntry> entries;
};

using VTablePtr = std::unique_ptr<VTable>;

// ============================================================
// Module（モジュール）
// ============================================================
struct MirImport {
    std::vector<std::string> path;   // e.g., ["std", "io"]
    std::string package_name;        // パッケージ名 (e.g. "axios")
    std::string alias;               // エイリアス（空の場合はなし）
    std::vector<std::string> items;  // 選択的インポート項目
    bool is_wildcard = false;        // ワイルドカードインポートか
};

using MirImportPtr = std::unique_ptr<MirImport>;

struct MirModule {
    std::string name;                   // モジュール名
    std::vector<std::string> path;      // モジュールパス (e.g., ["std", "io"])
    std::vector<MirImportPtr> imports;  // インポート
    std::vector<std::string> exports;   // エクスポートされる名前のリスト
};

using MirModulePtr = std::unique_ptr<MirModule>;

struct MirProgram {
    std::vector<MirFunctionPtr> functions;
    std::vector<MirStructPtr> structs;        // 構造体定義
    std::vector<MirEnumPtr> enums;            // enum定義（Tagged Union含む）
    std::vector<MirInterfacePtr> interfaces;  // インターフェース定義
    std::vector<VTablePtr> vtables;           // vtable（動的ディスパッチ用）
    std::vector<MirModulePtr> modules;        // モジュール
    std::vector<MirImportPtr> imports;        // インポート
    std::string filename;

    // 関数を名前で検索
    const MirFunction* find_function(const std::string& name) const {
        for (const auto& func : functions) {
            if (func && func->name == name) {
                return func.get();
            }
        }
        return nullptr;
    }

    // モジュール修飾名で関数を検索（例: "math::add"）
    const MirFunction* find_function_qualified(const std::string& qualified_name) const {
        // モジュール修飾名を分割
        size_t pos = qualified_name.find("::");
        if (pos != std::string::npos) {
            std::string module = qualified_name.substr(0, pos);
            std::string func_name = qualified_name.substr(pos + 2);

            for (const auto& func : functions) {
                if (func && func->name == func_name && func->module_path == module) {
                    return func.get();
                }
            }
        } else {
            // 修飾なしの場合は通常の検索
            return find_function(qualified_name);
        }
        return nullptr;
    }

    // 構造体を名前で検索
    const MirStruct* find_struct(const std::string& name) const {
        for (const auto& st : structs) {
            if (st && st->name == name) {
                return st.get();
            }
        }
        return nullptr;
    }

    // vtableを検索
    const VTable* find_vtable(const std::string& type_name,
                              const std::string& interface_name) const {
        for (const auto& vt : vtables) {
            if (vt && vt->type_name == type_name && vt->interface_name == interface_name) {
                return vt.get();
            }
        }
        return nullptr;
    }
};

}  // namespace cm::mir
