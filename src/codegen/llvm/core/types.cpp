/// @file llvm_types.cpp
/// @brief 型変換・定数変換処理

#include "mir_to_llvm.hpp"

#include <iostream>
#include <variant>

namespace cm::codegen::llvm_backend {

// 型変換
llvm::Type* MIRToLLVM::convertType(const hir::TypePtr& type) {
    if (!type)
        return ctx.getI32Type();

    switch (type->kind) {
        case hir::TypeKind::Void:
            return ctx.getVoidType();
        case hir::TypeKind::Bool:
            // メモリ上ではi8として扱う（i1は演算時のみ）
            return ctx.getI8Type();
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
        case hir::TypeKind::Char:
            return ctx.getI8Type();
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return ctx.getI16Type();
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
            return ctx.getI32Type();
        case hir::TypeKind::Long:
        case hir::TypeKind::ULong:
            return ctx.getI64Type();
        case hir::TypeKind::ISize:
        case hir::TypeKind::USize:
            // ポインタサイズ整数（64ビット想定）
            return ctx.getI64Type();
        case hir::TypeKind::Float:
        case hir::TypeKind::UFloat:
            return ctx.getF32Type();
        case hir::TypeKind::Double:
        case hir::TypeKind::UDouble:
            return ctx.getF64Type();
        case hir::TypeKind::String:
        case hir::TypeKind::CString:
            return ctx.getPtrType();
        case hir::TypeKind::Pointer:
        case hir::TypeKind::Reference: {
            // LLVM 14+: opaque pointersを使用するが、元の型情報は保持する
            // ポインタ型はctx.getPtrType()を返すが、
            // 内部的には元の型情報（type->element_type）を保持している
            // store/load時に適切な型でキャストする
            return ctx.getPtrType();
        }
        case hir::TypeKind::Array: {
            // 動的配列（スライス）の場合はポインタ型を返す
            if (!type->array_size.has_value()) {
                return ctx.getPtrType();
            }

            // Clang準拠: 多次元配列はネスト構造を保持
            // int[D1][D2] → [D1 x [D2 x int]]
            // これによりGEPで複数インデックスを使用でき、LLVMのベクトル化が効く
            auto elemType = convertType(type->element_type);
            return llvm::ArrayType::get(elemType, type->array_size.value());
        }
        case hir::TypeKind::Struct: {
            // まずインターフェース型かチェック
            if (isInterfaceType(type->name)) {
                // インターフェース型はfat pointerとして扱う
                auto it = interfaceTypes.find(type->name);
                if (it != interfaceTypes.end()) {
                    return it->second;
                }
                // まだ作成されていない場合は作成
                return getInterfaceFatPtrType(type->name);
            }

            // 構造体型を検索
            std::string lookupName = type->name;

            // <>を含む型名を正規化（"Vector<int>" → "Vector__int"）
            if (lookupName.find('<') != std::string::npos) {
                std::string normalized;
                std::string current;
                bool inBracket = false;
                for (size_t i = 0; i < lookupName.size(); ++i) {
                    char c = lookupName[i];
                    if (c == '<') {
                        normalized += current + "__";
                        current.clear();
                        inBracket = true;
                    } else if (c == '>') {
                        if (!current.empty()) {
                            normalized += current;
                        }
                        current.clear();
                        inBracket = false;
                    } else if (c == ',' && inBracket) {
                        if (!current.empty()) {
                            size_t start = current.find_first_not_of(" \t");
                            size_t end = current.find_last_not_of(" \t");
                            if (start != std::string::npos && end != std::string::npos) {
                                normalized += current.substr(start, end - start + 1) + "__";
                            }
                        }
                        current.clear();
                    } else {
                        current += c;
                    }
                }
                if (!current.empty()) {
                    normalized += current;
                }
                lookupName = normalized;
            }

            // カンマ区切りの型名を正規化（"int, int" → "int__int"）
            // 複数パラメータジェネリクスの型引数がカンマ区切りで格納されている場合のフォールバック
            if (lookupName.find(',') != std::string::npos) {
                std::string normalized;
                std::string current;
                for (size_t i = 0; i < lookupName.size(); ++i) {
                    char c = lookupName[i];
                    if (c == ',') {
                        // 空白をトリム
                        size_t start = current.find_first_not_of(" \t");
                        size_t end = current.find_last_not_of(" \t");
                        if (start != std::string::npos && end != std::string::npos) {
                            if (!normalized.empty())
                                normalized += "__";
                            normalized += current.substr(start, end - start + 1);
                        }
                        current.clear();
                    } else {
                        current += c;
                    }
                }
                // 最後の要素を追加
                if (!current.empty()) {
                    size_t start = current.find_first_not_of(" \t");
                    size_t end = current.find_last_not_of(" \t");
                    if (start != std::string::npos && end != std::string::npos) {
                        if (!normalized.empty())
                            normalized += "__";
                        normalized += current.substr(start, end - start + 1);
                    }
                }
                lookupName = normalized;
            }

            // ジェネリック構造体の場合、型引数を考慮した名前を生成
            // 例: Node<int> -> Node__int
            // 既にマングリング済み(__含む)の場合はスキップ
            // ただし、<>を含む場合はまだ変換が必要
            bool needsMangling =
                !type->type_args.empty() && (lookupName.find("__") == std::string::npos ||
                                             lookupName.find('<') != std::string::npos ||
                                             lookupName.find('>') != std::string::npos);
            if (needsMangling) {
                for (const auto& typeArg : type->type_args) {
                    if (typeArg) {
                        lookupName += "__";
                        // 型名を正規化（Pointer<int>ならptr_int等）
                        if (typeArg->kind == hir::TypeKind::Struct) {
                            // ネストジェネリックの場合、再帰的にマングリング
                            std::string nestedName = typeArg->name;
                            // type_argsがある場合（例: Vector<int>）、再帰的に処理
                            if (!typeArg->type_args.empty()) {
                                for (const auto& nestedArg : typeArg->type_args) {
                                    if (nestedArg) {
                                        nestedName += "__";
                                        switch (nestedArg->kind) {
                                            case hir::TypeKind::Int:
                                                nestedName += "int";
                                                break;
                                            case hir::TypeKind::UInt:
                                                nestedName += "uint";
                                                break;
                                            case hir::TypeKind::Long:
                                                nestedName += "long";
                                                break;
                                            case hir::TypeKind::ULong:
                                                nestedName += "ulong";
                                                break;
                                            case hir::TypeKind::Float:
                                                nestedName += "float";
                                                break;
                                            case hir::TypeKind::Double:
                                                nestedName += "double";
                                                break;
                                            case hir::TypeKind::Bool:
                                                nestedName += "bool";
                                                break;
                                            case hir::TypeKind::Char:
                                                nestedName += "char";
                                                break;
                                            case hir::TypeKind::String:
                                                nestedName += "string";
                                                break;
                                            case hir::TypeKind::Struct:
                                                nestedName += nestedArg->name;
                                                break;
                                            default:
                                                if (!nestedArg->name.empty()) {
                                                    nestedName += nestedArg->name;
                                                }
                                                break;
                                        }
                                    }
                                }
                            }
                            lookupName += nestedName;
                        } else if (typeArg->kind == hir::TypeKind::Pointer) {
                            // ポインタ型の場合：ptr_xxx 形式でマングリング
                            lookupName += "ptr_";
                            if (typeArg->element_type) {
                                switch (typeArg->element_type->kind) {
                                    case hir::TypeKind::Int:
                                        lookupName += "int";
                                        break;
                                    case hir::TypeKind::Long:
                                        lookupName += "long";
                                        break;
                                    case hir::TypeKind::Float:
                                        lookupName += "float";
                                        break;
                                    case hir::TypeKind::Double:
                                        lookupName += "double";
                                        break;
                                    case hir::TypeKind::Bool:
                                        lookupName += "bool";
                                        break;
                                    case hir::TypeKind::Char:
                                        lookupName += "char";
                                        break;
                                    case hir::TypeKind::Struct:
                                        lookupName += typeArg->element_type->name;
                                        break;
                                    default:
                                        lookupName += "void";
                                        break;
                                }
                            } else {
                                lookupName += "void";
                            }
                        } else {
                            // プリミティブ型の場合
                            switch (typeArg->kind) {
                                case hir::TypeKind::Int:
                                    lookupName += "int";
                                    break;
                                case hir::TypeKind::UInt:
                                    lookupName += "uint";
                                    break;
                                case hir::TypeKind::Long:
                                    lookupName += "long";
                                    break;
                                case hir::TypeKind::ULong:
                                    lookupName += "ulong";
                                    break;
                                case hir::TypeKind::Float:
                                    lookupName += "float";
                                    break;
                                case hir::TypeKind::Double:
                                    lookupName += "double";
                                    break;
                                case hir::TypeKind::Bool:
                                    lookupName += "bool";
                                    break;
                                case hir::TypeKind::Char:
                                    lookupName += "char";
                                    break;
                                case hir::TypeKind::String:
                                    lookupName += "string";
                                    break;
                                case hir::TypeKind::Pointer: {
                                    // ポインタ型: ptr_xxx 形式で追加
                                    lookupName += "ptr_";
                                    if (typeArg->element_type) {
                                        switch (typeArg->element_type->kind) {
                                            case hir::TypeKind::Int:
                                                lookupName += "int";
                                                break;
                                            case hir::TypeKind::UInt:
                                                lookupName += "uint";
                                                break;
                                            case hir::TypeKind::Long:
                                                lookupName += "long";
                                                break;
                                            case hir::TypeKind::ULong:
                                                lookupName += "ulong";
                                                break;
                                            case hir::TypeKind::Float:
                                                lookupName += "float";
                                                break;
                                            case hir::TypeKind::Double:
                                                lookupName += "double";
                                                break;
                                            case hir::TypeKind::Bool:
                                                lookupName += "bool";
                                                break;
                                            case hir::TypeKind::Char:
                                                lookupName += "char";
                                                break;
                                            case hir::TypeKind::String:
                                                lookupName += "string";
                                                break;
                                            case hir::TypeKind::Struct:
                                                lookupName += typeArg->element_type->name;
                                                break;
                                            default:
                                                lookupName += "void";
                                                break;
                                        }
                                    } else {
                                        lookupName += "void";
                                    }
                                    break;
                                }
                                default:
                                    // その他の型は型名をそのまま使用
                                    if (!typeArg->name.empty()) {
                                        lookupName += typeArg->name;
                                    }
                                    break;
                            }
                        }
                    }
                }
            }

            auto it = structTypes.find(lookupName);
            if (it != structTypes.end()) {
                return it->second;
            }

            // フォールバック: 元の名前でも検索
            if (lookupName != type->name) {
                auto it2 = structTypes.find(type->name);
                if (it2 != structTypes.end()) {
                    return it2->second;
                }
            }

            // 特殊化構造体が見つからない場合、structDefsも確認
            auto defIt = structDefs.find(lookupName);
            if (defIt != structDefs.end()) {
                // 構造体定義が存在する場合、LLVM型を作成して登録
                auto structType = llvm::StructType::create(ctx.getContext(), lookupName);
                structTypes[lookupName] = structType;

                // フィールド型を設定
                std::vector<llvm::Type*> fieldTypes;
                for (const auto& field : defIt->second->fields) {
                    fieldTypes.push_back(convertType(field.type));
                }
                structType->setBody(fieldTypes);

                // デバッグ情報
                std::cerr << "[LLVM] Registered specialized struct: " << lookupName << " with "
                          << fieldTypes.size() << " fields\n";

                return structType;
            }

            // Tagged Union構造体の動的生成
            // 型名が__TaggedUnion_で始まる場合、{i32, i8[N]}構造体を生成
            // Nはenumの最大ペイロードサイズ
            if (lookupName.find("__TaggedUnion_") == 0) {
                // enum名を抽出（__TaggedUnion_Status -> Status）
                std::string enumName = lookupName.substr(14);

                // enumDefsから最大ペイロードサイズを取得
                // LLVM DataLayoutを使用して構造体型のサイズも正確に計算
                uint32_t maxPayloadSize = 8;  // デフォルト8バイト
                auto enumIt = enumDefs.find(enumName);
                if (enumIt != enumDefs.end() && enumIt->second) {
                    uint32_t computedMax = 0;
                    for (const auto& member : enumIt->second->members) {
                        uint32_t memberSize = 0;
                        for (const auto& [fieldName, fieldType] : member.fields) {
                            if (!fieldType)
                                continue;
                            // 構造体型の場合はconvertTypeでLLVM型を取得してサイズを計算
                            if (fieldType->kind == hir::TypeKind::Struct ||
                                fieldType->kind == hir::TypeKind::Generic) {
                                auto llvmFieldType = convertType(fieldType);
                                if (llvmFieldType) {
                                    auto dl = module->getDataLayout();
                                    memberSize += dl.getTypeAllocSize(llvmFieldType);
                                } else {
                                    memberSize += 8;  // フォールバック
                                }
                            } else {
                                // プリミティブ型のサイズ計算
                                switch (fieldType->kind) {
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
                                    default:
                                        memberSize += 8;
                                        break;
                                }
                            }
                        }
                        if (memberSize > computedMax) {
                            computedMax = memberSize;
                        }
                    }
                    maxPayloadSize = (computedMax > 0) ? computedMax : 8;
                }

                auto structType = llvm::StructType::create(ctx.getContext(), lookupName);
                std::vector<llvm::Type*> fieldTypes = {
                    ctx.getI32Type(),                                      // tag (field[0])
                    llvm::ArrayType::get(ctx.getI8Type(), maxPayloadSize)  // payload (field[1])
                };
                structType->setBody(fieldTypes);
                structTypes[lookupName] = structType;

                return structType;
            }

            // ポインタ型（*xxx形式）の場合、LLVM opaque ptrを返す
            if (!lookupName.empty() && lookupName[0] == '*') {
                // LLVM opaque pointer (ptr)として扱う
                return llvm::PointerType::get(ctx.getContext(), 0);
            }

            // エラーログを追加
            // ジェネリック型パラメータ（T, U, V, K, E等）の場合は警告をスキップ
            // これらはモノモーフィゼーション前のジェネリック構造体定義で残っている
            bool isGenericTypeParam = false;
            if (lookupName.length() == 1 && std::isupper(lookupName[0])) {
                isGenericTypeParam = true;
            } else if (lookupName.length() == 2 && std::isupper(lookupName[0]) &&
                       std::isdigit(lookupName[1])) {
                // T1, T2, V1 等のパターン
                isGenericTypeParam = true;
            }

            if (!isGenericTypeParam) {
                // フォールバックでタグ付きユニオン構造体を生成するため、警告は抑制
                // typedef Union (e.g., IntOrLong = int | long) の場合はここに到達する
            }

            // 見つからない場合、typedef Unionの可能性があるため
            // デフォルトでタグ付きユニオン互換の構造体（{i32, i8[8]}）を生成
            // これにより int | long のようなシンプルなunionが動作する
            auto structType = llvm::StructType::create(ctx.getContext(), lookupName);
            std::vector<llvm::Type*> fieldTypes = {
                ctx.getI32Type(),                         // tag (field[0])
                llvm::ArrayType::get(ctx.getI8Type(), 8)  // payload (field[1]) - 8バイト
            };
            structType->setBody(fieldTypes);
            structTypes[lookupName] = structType;  // キャッシュに登録
            return structType;
        }
        case hir::TypeKind::TypeAlias: {
            // typedefの実際の型がある場合は再帰的に変換
            if (type->element_type) {
                return convertType(type->element_type);
            }

            // まずインターフェース型かチェック
            if (isInterfaceType(type->name)) {
                auto it = interfaceTypes.find(type->name);
                if (it != interfaceTypes.end()) {
                    return it->second;
                }
                return getInterfaceFatPtrType(type->name);
            }

            // TypeAliasはMIRレベルで解決されているべき
            // 万が一解決されていない場合、structTypesを確認
            auto it = structTypes.find(type->name);
            if (it != structTypes.end()) {
                return it->second;
            }
            // フォールバック: intとして扱う
            return ctx.getI32Type();
        }
        case hir::TypeKind::Function: {
            // 関数ポインタ型
            llvm::Type* retType =
                type->return_type ? convertType(type->return_type) : ctx.getVoidType();
            std::vector<llvm::Type*> paramTypes;
            for (const auto& paramType : type->param_types) {
                paramTypes.push_back(convertType(paramType));
            }
            (void)llvm::FunctionType::get(retType, paramTypes, false);  // 型チェックのみ
#if LLVM_VERSION_MAJOR >= 15
            // LLVM 15+: opaque pointerを使用（関数ポインタも単なるptr）
            return ctx.getPtrType();
#else
            // LLVM 14: typed pointer（関数ポインタ型）を返す
            auto funcType = llvm::FunctionType::get(retType, paramTypes, false);
            return llvm::PointerType::get(funcType, 0);
#endif
        }
        case hir::TypeKind::Union: {
            // Union型 (例: int | long) は tagged union として表現
            // 構造体: {tag: i32, data: i8[max_payload_size]}

            // 最大ペイロードサイズを計算（UnionVariantsから）
            uint32_t maxPayloadSize = 8;  // デフォルト8バイト（int/long等）

            // type_argsに含まれる型からサイズを計算
            if (!type->type_args.empty()) {
                maxPayloadSize = 0;
                for (const auto& variantType : type->type_args) {
                    if (variantType) {
                        uint32_t variantSize = 0;
                        switch (variantType->kind) {
                            case hir::TypeKind::Long:
                            case hir::TypeKind::ULong:
                            case hir::TypeKind::Double:
                            case hir::TypeKind::UDouble:
                            case hir::TypeKind::Pointer:
                            case hir::TypeKind::Reference:
                            case hir::TypeKind::String:
                            case hir::TypeKind::CString:
                            case hir::TypeKind::ISize:
                            case hir::TypeKind::USize:
                                variantSize = 8;
                                break;
                            case hir::TypeKind::Int:
                            case hir::TypeKind::UInt:
                            case hir::TypeKind::Float:
                            case hir::TypeKind::UFloat:
                                variantSize = 4;
                                break;
                            case hir::TypeKind::Short:
                            case hir::TypeKind::UShort:
                                variantSize = 2;
                                break;
                            case hir::TypeKind::Bool:
                            case hir::TypeKind::Tiny:
                            case hir::TypeKind::UTiny:
                            case hir::TypeKind::Char:
                                variantSize = 1;
                                break;
                            case hir::TypeKind::Struct:
                            case hir::TypeKind::Union: {
                                // 構造体/union型: convertTypeで実際のLLVM型サイズを取得
                                auto* llvmType = convertType(variantType);
                                auto& dataLayout = module->getDataLayout();
                                variantSize =
                                    static_cast<uint32_t>(dataLayout.getTypeAllocSize(llvmType));
                                break;
                            }
                            default:
                                variantSize = 8;  // その他の型は8バイト仮定
                                break;
                        }
                        if (variantSize > maxPayloadSize) {
                            maxPayloadSize = variantSize;
                        }
                    }
                }
                // 最低8バイトを確保
                if (maxPayloadSize < 8)
                    maxPayloadSize = 8;
            }

            // キャッシュキーを決定: 名前付きならその名前、無名ならtype_argsから生成
            std::string cacheKey;
            if (!type->name.empty()) {
                cacheKey = type->name;
            } else {
                // 匿名union型のキャッシュキーをペイロードサイズから生成
                // 同じペイロードサイズのunionは同じ構造体を共有
                cacheKey = "__anon_union_" + std::to_string(maxPayloadSize);
            }

            // キャッシュから探索
            auto it = structTypes.find(cacheKey);
            if (it != structTypes.end()) {
                return it->second;
            }

            // 新規作成してキャッシュに登録
            auto structType = llvm::StructType::create(ctx.getContext(), cacheKey);
            std::vector<llvm::Type*> fieldTypes = {
                ctx.getI32Type(),                                      // tag (field[0])
                llvm::ArrayType::get(ctx.getI8Type(), maxPayloadSize)  // payload (field[1])
            };
            structType->setBody(fieldTypes);
            structTypes[cacheKey] = structType;

            return structType;
        }
        default:
            return ctx.getI32Type();
    }
}

// ポインタ型から指す先の型を取得
llvm::Type* MIRToLLVM::getPointeeType(const hir::TypePtr& ptrType) {
    if (!ptrType) {
        return ctx.getI32Type();
    }

    // ポインタ型または参照型の場合
    if (ptrType->kind == hir::TypeKind::Pointer || ptrType->kind == hir::TypeKind::Reference) {
        if (ptrType->element_type) {
            return convertType(ptrType->element_type);
        }
        // 要素型が不明な場合はi8として扱う
        return ctx.getI8Type();
    }

    // 関数型の場合（関数ポインタ）
    if (ptrType->kind == hir::TypeKind::Function) {
        // 関数型自体を返す（呼び出し時に使用）
        llvm::Type* retType =
            ptrType->return_type ? convertType(ptrType->return_type) : ctx.getVoidType();
        std::vector<llvm::Type*> paramTypes;
        for (const auto& paramType : ptrType->param_types) {
            paramTypes.push_back(convertType(paramType));
        }
        return llvm::FunctionType::get(retType, paramTypes, false);
    }

    // 配列型（動的配列/スライス）
    if (ptrType->kind == hir::TypeKind::Array && !ptrType->array_size.has_value()) {
        if (ptrType->element_type) {
            return convertType(ptrType->element_type);
        }
        return ctx.getI8Type();
    }

    // その他の型はそのまま変換
    return convertType(ptrType);
}

// 定数変換
llvm::Constant* MIRToLLVM::convertConstant(const mir::MirConstant& constant) {
    // std::variant を処理
    if (std::holds_alternative<bool>(constant.value)) {
        // bool定数はi8として生成（メモリ格納用）
        return llvm::ConstantInt::get(ctx.getI8Type(), std::get<bool>(constant.value));
    } else if (std::holds_alternative<char>(constant.value)) {
        // 文字リテラルはi8として生成
        return llvm::ConstantInt::get(ctx.getI8Type(), std::get<char>(constant.value));
    } else if (std::holds_alternative<int64_t>(constant.value)) {
        int64_t val = std::get<int64_t>(constant.value);
        // std::cerr << "[MIR2LLVM]             convertConstant: val=" << val << "\n";

        // 型情報がある場合、適切な型で生成
        if (constant.type) {
            switch (constant.type->kind) {
                case hir::TypeKind::Pointer:
                case hir::TypeKind::Reference:
                case hir::TypeKind::String:
                case hir::TypeKind::CString:
                    // ポインタ型の場合は null pointer
                    return llvm::ConstantPointerNull::get(
                        llvm::PointerType::get(ctx.getContext(), 0));
                case hir::TypeKind::Array:
                    // 動的配列（スライス）の場合もポインタ
                    if (!constant.type->array_size.has_value()) {
                        return llvm::ConstantPointerNull::get(
                            llvm::PointerType::get(ctx.getContext(), 0));
                    }
                    break;
                case hir::TypeKind::Long:
                case hir::TypeKind::ULong:
                case hir::TypeKind::ISize:
                case hir::TypeKind::USize:
                    return llvm::ConstantInt::get(ctx.getI64Type(), val);
                case hir::TypeKind::Short:
                case hir::TypeKind::UShort:
                    return llvm::ConstantInt::get(ctx.getI16Type(), val);
                case hir::TypeKind::Tiny:
                case hir::TypeKind::UTiny:
                case hir::TypeKind::Char:
                    return llvm::ConstantInt::get(ctx.getI8Type(), val);
                default:
                    break;
            }
        }
        return llvm::ConstantInt::get(ctx.getI32Type(), val);
    } else if (std::holds_alternative<double>(constant.value)) {
        // 型情報がある場合、適切な浮動小数点型で生成
        if (constant.type && (constant.type->kind == hir::TypeKind::Float ||
                              constant.type->kind == hir::TypeKind::UFloat)) {
            return llvm::ConstantFP::get(ctx.getF32Type(), std::get<double>(constant.value));
        }
        return llvm::ConstantFP::get(ctx.getF64Type(), std::get<double>(constant.value));
    } else if (std::holds_alternative<std::string>(constant.value)) {
        // 文字列リテラル
        auto& str = std::get<std::string>(constant.value);
        return builder->CreateGlobalStringPtr(str, "str");
    } else {
        // nullまたは未知の型
        if (constant.type) {
            if (constant.type->kind == hir::TypeKind::Pointer ||
                constant.type->kind == hir::TypeKind::Reference ||
                constant.type->kind == hir::TypeKind::String ||
                constant.type->kind == hir::TypeKind::CString ||
                (constant.type->kind == hir::TypeKind::Array &&
                 !constant.type->array_size.has_value())) {
                // LLVM 14+: opaque pointerを使用
                return llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(ctx.getPtrType()));
            }
        }
        return llvm::Constant::getNullValue(ctx.getI32Type());
    }
}

}  // namespace cm::codegen::llvm_backend
