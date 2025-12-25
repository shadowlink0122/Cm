#pragma once

#include "../../common/target.hpp"
#include "decl.hpp"
#include "module.hpp"
#include "nodes.hpp"
#include "typedef.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace cm::ast {

class TargetFilteringVisitor {
    Target target_;

   public:
    explicit TargetFilteringVisitor(Target t) : target_(t) {}

    void visit(Program& prog) { filter_decls(prog.declarations); }

   private:
    void filter_decls(std::vector<DeclPtr>& decls) {
        decls.erase(std::remove_if(decls.begin(), decls.end(),
                                   [this](const DeclPtr& d) { return !should_keep(*d); }),
                    decls.end());

        for (auto& d : decls) {
            process_recursion(*d);
        }
    }

    bool should_keep(const Decl& d) {
        const std::vector<AttributeNode>* attrs = nullptr;

        std::visit(
            [&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::unique_ptr<FunctionDecl>>) {
                    attrs = &arg->attributes;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<StructDecl>>) {
                    attrs = &arg->attributes;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<InterfaceDecl>>) {
                    attrs = &arg->attributes;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<ImplDecl>>) {
                    attrs = &arg->attributes;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<EnumDecl>>) {
                    attrs = &arg->attributes;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<TypedefDecl>>) {
                    attrs = &arg->attributes;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<GlobalVarDecl>>) {
                    attrs = &arg->attributes;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<UseDecl>>) {
                    attrs = &arg->attributes;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<ImportDecl>>) {
                    attrs = &arg->attributes;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<ExternBlockDecl>>) {
                    attrs = &arg->attributes;
                }
                // その他の宣言は属性やターゲットフィルタリングをまだサポートしていない可能性があります
            },
            d.kind);

        if (!attrs) {
            // printf("No attributes (attrs is null)\n");
            return true;
        }

        // printf("Checking attributes size: %zu\n", attrs->size());

        /*
        for (const auto& attr : *attrs) {
            printf("Attr: %s\n", attr.name.c_str());
        }
        */

        return check_target_attributes(*attrs);
    }

    bool check_target_attributes(const std::vector<AttributeNode>& attrs) {
        for (const auto& attr : attrs) {
            if (attr.name == "target") {
                // printf("Found target attr\n");
                if (!check_target_condition(attr.args)) {
                    // printf("Target condition failed\n");
                    return false;  // falseと評価されるtarget属性が見つかりました
                }
            } else {
                // printf("Ignoring attr: %s\n", attr.name.c_str());
            }
        }
        return true;  // target属性が見つからないか、すべてtrueと評価されました
                      // （複数のtarget属性がある場合はANDとして扱われます）
        // 設計上、「引数はOR」です。"target(a, b)" は "a OR b" です。
        // もし2つのtarget属性がある場合は？ "#[target(a)] #[target(b)]"？
        // 通常はANDとして扱われます。両方が満たされる必要があります。
        // ここのロジックは1つでも失敗すれば即座にfalseを返します。したがってANDです。正しいです。
    }

    bool check_target_condition(const std::vector<std::string>& args) {
        // 引数はOR条件です。少なくとも1つがtrueである必要があります。
        // 引数は "js" または "!js" です。

        for (const auto& arg : args) {
            if (evaluate_condition(arg)) {
                return true;
            }
        }
        return false;  // 一致なし
    }

    bool evaluate_condition(const std::string& arg) {
        bool negated = false;
        std::string t_str = arg;
        if (t_str.size() > 1 && t_str[0] == '!') {
            negated = true;
            t_str = t_str.substr(1);
        }

        if (t_str == "active") {
            return !negated;
        }

        bool match = false;
        // ターゲット文字列を列挙型にパースするか、単純に文字列比較します
        // "js", "wasm", "native", "web"

        // マッチ確認用ヘルパー

        // 特殊ケース: "native" は Target::Native と異なる可能性があります
        Target cond_target = string_to_target_safe(t_str);

        if (cond_target == Target::Native && t_str != "native") {
            // 未認識のターゲット文字列（string_to_targetはデフォルトでNativeを返す？）
        }

        // common/target.hpp の string_to_target の実装
        // if (s == "wasm") return Target::Wasm; ... default Native.

        // "native" が Target::Native にマッチするのは正しいか？
        // "js" 用にコンパイルする場合、target_ は JS です。"native" (Native) != JS。正しいです。

        match = (string_to_target(t_str) == target_);

        // t_str が標準ターゲットでない場合の追加処理
        // "web" は純粋なWebを意味します。"js" を使用するとNodeまたはWebを意味する可能性があります
        // 設計上: "target(js)" はJSバックエンド（NodeまたはWeb）にマッチします。
        // "target(web)" はWebにのみマッチします。

        if (t_str == "js") {
            match = (target_ == Target::JS || target_ == Target::Web);
        }

        return negated ? !match : match;
    }

    Target string_to_target_safe(const std::string& s) {
        // commonターゲット関数への依存を回避（デフォルトの場合）
        if (s == "wasm")
            return Target::Wasm;
        if (s == "js")
            return Target::JS;
        if (s == "web")
            return Target::Web;
        if (s == "intr")
            return Target::Interpreter;
        return Target::Native;
    }

    void process_recursion(Decl& d) {
        std::visit(
            [&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::unique_ptr<ImplDecl>>) {
                    // ImplDecl内のメソッドをフィルタリング
                    filter_methods(arg->methods);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<ExternBlockDecl>>) {
                    // ExternBlockは...を持ちます
                    filter_c_decls(arg->declarations);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<ModuleDecl>>) {
                    filter_decls(arg->declarations);
                }
            },
            d.kind);
    }

    void filter_methods(std::vector<std::unique_ptr<FunctionDecl>>& methods) {
        methods.erase(std::remove_if(methods.begin(), methods.end(),
                                     [this](const std::unique_ptr<FunctionDecl>& f) {
                                         // Create a temporary Decl wrapper to use should_keep?
                                         // Or redundant. accessing f->attributes directly is
                                         // easier.
                                         return !check_target_attributes(f->attributes);
                                     }),
                      methods.end());
    }

    void filter_c_decls(std::vector<std::unique_ptr<FunctionDecl>>& decls) {
        decls.erase(std::remove_if(decls.begin(), decls.end(),
                                   [this](const std::unique_ptr<FunctionDecl>& f) {
                                       return !check_target_attributes(f->attributes);
                                   }),
                    decls.end());
    }
};

}  // namespace cm::ast
