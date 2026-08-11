#pragma once

#include "internal/base/span.hpp"
#include "internal/hir/nodes.hpp"
#include "internal/hir/types.hpp"
#include "internal/syntax/lexer/token.hpp"

#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
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

    // 既存の互換性のためのファクトリメソッド（実装は nodes.cpp）
    static PlaceProjection field(FieldId id);
    static PlaceProjection index(LocalId local);
    static PlaceProjection deref();

    // 型情報を持つ新しいファクトリメソッド（実装は nodes.cpp）
    static PlaceProjection field(FieldId id, hir::TypePtr result_type);
    static PlaceProjection index(LocalId local, hir::TypePtr result_type);
    static PlaceProjection deref(hir::TypePtr result_type, hir::TypePtr pointee_type);
};

struct MirPlace {
    LocalId local;
    std::vector<PlaceProjection> projections;
    hir::TypePtr type;          // このPlaceが示す値の型
    hir::TypePtr pointee_type;  // ポインタの場合のpointee type

    // 既存の互換性のためのコンストラクタ
    MirPlace(LocalId l) : local(l) {}
    MirPlace(LocalId l, std::vector<PlaceProjection> p) : local(l), projections(std::move(p)) {}

    // 型情報を持つコンストラクタ（ポインタ型なら pointee_type も設定する。実装は nodes.cpp）
    MirPlace(LocalId l, hir::TypePtr t);
    MirPlace(LocalId l, std::vector<PlaceProjection> p, hir::TypePtr t);
};

// ============================================================
// Operand（オペランド）- 値を表現
// ============================================================
struct MirConstant {
    std::variant<std::monostate,  // unit/void
                 bool, int64_t, double, char, std::string>
        value;
    hir::TypePtr type;
    std::optional<BitLiteralInfo> bit_info;  // SV幅付きリテラル情報（nullopt = 通常定数）
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

    // 既存の互換性のためのファクトリメソッド（型なし。実装は nodes.cpp）
    static MirOperandPtr move(MirPlace place);
    static MirOperandPtr copy(MirPlace place);

    // 型情報を持つ新しいファクトリメソッド（実装は nodes.cpp）
    static MirOperandPtr move(MirPlace place, hir::TypePtr type);
    static MirOperandPtr copy(MirPlace place, hir::TypePtr type);
    static MirOperandPtr constant(MirConstant c);
    static MirOperandPtr function_ref(std::string func_name, hir::TypePtr type = nullptr);
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
        // ユニオン型の実行時型判別 (expr is Type)。trueならタグ比較のboolを返す
        bool check_only = false;
        // インターフェースupcast（具象→interfaceのfat pointer構築）。
        // 非空なら具象構造体名で、target_typeのinterfaceへvtableを引いてfat pointerを組む。
        // バックエンドのassign/引数認識ヒューリスティックを廃し、MIRの構築物として一意に表現する（coercion第2段）
        std::string iface_concrete;
        // operandが具象構造体へのポインタ（Shape* p = &sq 経路）。falseは値upcast
        bool iface_from_pointer = false;
        // 値upcastのペイロードをヒープへ実体化（boxing）してから包む（Q3: 戻り値経由のダングリング防止）。
        // malloc不能なベアメタルnoStdターゲットではバックエンドがスキップしてよい
        bool iface_boxed = false;
    };

    struct FormatConvertData {
        MirOperandPtr operand;
        std::string format_spec;  // "x", "X", "b", "o", ".2" など
    };

    std::variant<UseData, BinaryOpData, UnaryOpData, RefData, AggregateData, CastData,
                 FormatConvertData>
        data;

    // ファクトリメソッド（実装は nodes.cpp）
    static MirRvaluePtr use(MirOperandPtr op);
    static MirRvaluePtr binary(MirBinaryOp op, MirOperandPtr lhs, MirOperandPtr rhs,
                               hir::TypePtr result_type = nullptr);
    static MirRvaluePtr unary(MirUnaryOp op, MirOperandPtr operand);
    static MirRvaluePtr format_convert(MirOperandPtr op, const std::string& format_spec);
    static MirRvaluePtr ref(MirPlace place, bool is_mutable);
    static MirRvaluePtr cast(MirOperandPtr operand, hir::TypePtr target_type,
                             bool check_only = false);
    // インターフェースupcast構築（fat pointer）。値upcastはboxed指定可・ポインタupcastはfrom_pointer=true
    static MirRvaluePtr iface_upcast(MirOperandPtr operand, hir::TypePtr iface_type,
                                     const std::string& concrete_name, bool from_pointer,
                                     bool boxed);
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

    // ファクトリメソッド（実装は nodes.cpp）
    static MirStatementPtr assign(MirPlace place, MirRvaluePtr rvalue, Span s = {});
    static MirStatementPtr storage_live(LocalId local, Span s = {});
    static MirStatementPtr storage_dead(LocalId local, Span s = {});

    // インラインアセンブリ用（実装は nodes.cpp）
    static MirStatementPtr asm_stmt(std::string code, bool is_must = true,
                                    std::vector<MirAsmOperand> operands = {},
                                    std::vector<std::string> clobbers = {}, Span s = {});
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
        // SVターゲット専用（SV-N3）: don't-careビットマスク（targetsと同順。空なら全件が完全一致・-1は完全一致）。
        // 判定は先頭から順に (discriminant & mask) == value（matchの先勝ち意味論）。定数評価するパスはこの規則で判定すること
        std::vector<int64_t> target_masks;
        // SVターゲット専用（SV-N3）: case修飾（0=既定(unique)・1=priority・2=unique0。#[sv::priority]/#[sv::unique0]由来）
        uint8_t sv_case_modifier = 0;
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

    // ファクトリメソッド（実装は nodes.cpp）
    static MirTerminatorPtr goto_block(BlockId target, Span s = {});
    static MirTerminatorPtr return_value(Span s = {});
    static MirTerminatorPtr unreachable(Span s = {});
    static MirTerminatorPtr switch_int(MirOperandPtr discriminant,
                                       std::vector<std::pair<int64_t, BlockId>> targets,
                                       BlockId otherwise, Span s = {});
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

    BasicBlock() : id(0) {}
    BasicBlock(BlockId i) : id(i) {}

    void add_statement(MirStatementPtr stmt) { statements.push_back(std::move(stmt)); }

    // ターミネータを設定しsuccessorsを更新する（実装は nodes.cpp）
    void set_terminator(MirTerminatorPtr term);

    // ターミネータからsuccessorsを再計算する（実装は nodes.cpp）
    void update_successors();
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
    bool is_global = false;  // グローバル変数（MirGlobalVarへの参照）

    // クロージャ関数ポインタの場合のキャプチャ情報
    bool is_closure = false;
    std::string closure_func_name;         // 実際のクロージャ関数名
    std::vector<LocalId> captured_locals;  // キャプチャされた変数のローカルID

    LocalDecl(LocalId i, std::string n, hir::TypePtr t, bool mut = true, bool user = true,
              bool is_static_ = false, bool is_global_ = false)
        : id(i),
          name(std::move(n)),
          type(std::move(t)),
          is_mutable(mut),
          is_user_variable(user),
          is_static(is_static_),
          is_global(is_global_) {}
};

// ============================================================
// MIR関数
// ============================================================
struct MirFunction {
    std::string name;
    std::string module_path;  // モジュールパス（例："std::io", ""は現在のモジュール）
    std::string source_file;   // 元ソースファイルパス（モジュール分割用）
    std::string package_name;  // パッケージ名 (FFI用)
    bool is_export = false;    // エクスポートされているか
    bool is_extern = false;    // extern "C" 関数か
    bool is_variadic = false;  // 可変長引数（FFI用）
    bool is_inline = false;    // inline修飾子（LLVMのinlinehint属性へ伝搬する）
    bool is_async = false;     // async関数（JSバックエンド用）
    bool is_always = false;  // always修飾子（SVバックエンド用: always_ff/always_comb）
    // SVバックエンド: always ブロックの種別
    enum class AlwaysKind { None, Auto, FF, Comb, Latch } always_kind = AlwaysKind::None;
    std::vector<std::string> attributes;  // SV属性（clock_domain, pipeline等）
    // #[test] 関数用: HIR文への参照（SVテストベンチ生成で使用。
    // MirInitialBlock::hir_stmts と同じ寿命モデル＝HIRプログラム存命中のみ有効）
    std::vector<const hir::HirStmt*> hir_stmts;
    std::vector<LocalDecl> locals;    // ローカル変数（引数も含む）
    std::vector<LocalId> arg_locals;  // 引数に対応するローカルID
    LocalId return_local;             // 戻り値用のローカル（_0）
    std::vector<BasicBlockPtr> basic_blocks;
    BlockId entry_block = ENTRY_BLOCK;

    // ローカル変数の追加（実装は nodes.cpp）
    LocalId add_local(std::string name, hir::TypePtr type, bool is_mutable = true,
                      bool is_user = true, bool is_static = false, bool is_global = false);

    // 基本ブロックの追加（実装は nodes.cpp）
    BlockId add_block();

    // 基本ブロックの取得（範囲外は nullptr。実装は nodes.cpp）
    BasicBlock* get_block(BlockId id);
    const BasicBlock* get_block(BlockId id) const;

    // CFGの構築（predecessorの計算、実装は nodes.cpp）
    void build_cfg();
};

// ============================================================
// MIRプログラム
// ============================================================
// 構造体定義
struct MirStructField {
    std::string name;
    hir::TypePtr type;
    std::vector<std::string> attributes;  // フィールド属性（sv::param, output 等）
    std::string default_value_str;        // デフォルト値の文字列表現（SV用）
    // 注: バイトオフセットは保持しない。レイアウト（サイズ・アライメント・オフセット）は
    // LoweringContext::layout_size/layout_align とLLVMのDataLayoutが唯一の情報源であり、
    // ここへ複製すると二重管理で食い違う（M13）
};

struct MirStruct {
    std::string name;
    std::string module_path;  // モジュールパス
    std::string source_file;  // 元ソースファイルパス（モジュール分割用）
    bool is_export = false;   // エクスポートされているか
    std::vector<MirStructField> fields;
    // 注: 構造体全体のサイズ・アライメントは保持しない（MirStructFieldのoffsetと同じ理由。M13）
    bool is_css = false;
    bool is_extern = false;               // extern struct（外部HWモジュール）
    std::vector<std::string> attributes;  // 構造体属性（sv::packed/sv::unpacked 等）

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
    std::string source_file;  // 元ソースファイルパス（モジュール分割用）
    bool is_export = false;
    std::vector<MirEnumMember> members;

    // Tagged Unionかどうか（dataを持つメンバーがあるか。実装は nodes.cpp）
    bool is_tagged_union() const;

    // 最大ペイロードサイズを計算（Tagged Union用、実装は nodes.cpp）
    uint32_t max_payload_size() const;
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

// ============================================================
// グローバル変数
// ============================================================
struct MirGlobalVar {
    std::string name;
    hir::TypePtr type;
    std::unique_ptr<MirConstant> init_value;  // 初期値（nullptrならゼロ初期化）
    const hir::HirExpr* init_expr =
        nullptr;  // 非定数初期化式（assign文用、SVバックエンド等で使用）
    bool is_const = false;
    bool is_assign = false;  // SV assign文（連続代入）
    bool is_export = false;
    std::vector<std::string> attributes;  // "input", "output" 等（SV用）
    // extern struct インスタンスのフィールド初期化値
    // key: フィールド名, value: 初期化値の定数
    std::vector<std::pair<std::string, MirConstant>> struct_field_inits;
};

using MirGlobalVarPtr = std::unique_ptr<MirGlobalVar>;

// ============================================================
// SV initial ブロック
// ============================================================
struct MirInitialBlock {
    std::vector<BasicBlockPtr> blocks;
    std::vector<std::string> attributes;
    // HIR文のリスト（SVコードジェネレータで使用）
    std::vector<const hir::HirStmt*> hir_stmts;
};

using MirInitialBlockPtr = std::unique_ptr<MirInitialBlock>;

struct MirProgram {
    std::vector<MirFunctionPtr> functions;
    std::vector<MirStructPtr> structs;               // 構造体定義
    std::vector<MirEnumPtr> enums;                   // enum定義（Tagged Union含む）
    std::vector<MirInterfacePtr> interfaces;         // インターフェース定義
    std::vector<VTablePtr> vtables;                  // vtable（動的ディスパッチ用）
    std::vector<MirModulePtr> modules;               // モジュール
    std::vector<MirImportPtr> imports;               // インポート
    std::vector<MirGlobalVarPtr> global_vars;        // グローバル変数
    std::vector<MirInitialBlockPtr> initial_blocks;  // SV initial ブロック
    std::string filename;

    // typedef定義マップ（名前→解決済み型）
    // LLVM backendでTypeAlias/Struct名の透過的解決に使用
    std::unordered_map<std::string, hir::TypePtr> typedef_defs;

    // 名前検索系ヘルパー（実装は nodes.cpp）
    // 関数を名前で検索
    const MirFunction* find_function(const std::string& name) const;
    // モジュール修飾名で関数を検索（例: "math::add"）
    const MirFunction* find_function_qualified(const std::string& qualified_name) const;
    // 構造体を名前で検索
    const MirStruct* find_struct(const std::string& name) const;
    // vtableを検索
    const VTable* find_vtable(const std::string& type_name,
                              const std::string& interface_name) const;
};

}  // namespace cm::mir
