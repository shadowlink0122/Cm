# Cm Language Server Protocol (LSP) Implementation
**Date**: 2026-01-14
**Version**: v0.12.0

## 概要

`cm-lsp`はCm言語の公式Language Serverです。VSCode、Neovim、Sublime Textなど、LSP対応エディタで高度なコード補完、型チェック、リファクタリング機能を提供します。

## 設計目標

1. **完全なLSP 3.17準拠**: 最新のLSP仕様に完全準拠
2. **インクリメンタル解析**: 高速な応答性を実現
3. **正確な型推論**: コンパイラと同じ型システムを使用
4. **リアルタイム診断**: エラーと警告を即座に表示
5. **高度なリファクタリング**: 安全な名前変更、抽出機能

## アーキテクチャ

### システム構成

```
┌──────────────────────────────────────────────┐
│                IDE/Editor                     │
├──────────────────────────────────────────────┤
│            LSP Client (Extension)             │
└────────────────┬─────────────────────────────┘
                 │ JSON-RPC 2.0
┌────────────────▼─────────────────────────────┐
│              cm-lsp Server                    │
├──────────────────────────────────────────────┤
│            Transport Layer                    │
├──────────┬──────────┬──────────┬────────────┤
│  Request │ Document │  Workspace│  Diagnostic│
│  Handler │ Manager  │  Manager  │  Engine    │
├──────────┴──────────┴──────────┴────────────┤
│          Incremental Analyzer                 │
├──────────────────────────────────────────────┤
│     Cm Compiler Frontend (HIR/MIR)           │
└──────────────────────────────────────────────┘
```

### コンポーネント詳細

```rust
// 主要コンポーネント
pub struct CmLanguageServer {
    // ドキュメント管理
    documents: DocumentManager,

    // ワークスペース情報
    workspace: WorkspaceManager,

    // 型チェックエンジン
    type_checker: TypeChecker,

    // 診断エンジン
    diagnostics: DiagnosticEngine,

    // シンボルインデックス
    symbol_index: SymbolIndex,

    // インクリメンタル解析器
    analyzer: IncrementalAnalyzer,

    // 設定
    config: ServerConfig,
}
```

## LSP機能実装

### 1. 初期化

```rust
impl LanguageServer for CmLanguageServer {
    async fn initialize(&self, params: InitializeParams) -> Result<InitializeResult> {
        // サーバー機能を宣言
        Ok(InitializeResult {
            capabilities: ServerCapabilities {
                // テキスト同期
                text_document_sync: Some(TextDocumentSyncCapability::Options(
                    TextDocumentSyncOptions {
                        open_close: Some(true),
                        change: Some(TextDocumentSyncKind::INCREMENTAL),
                        save: Some(SaveOptions { include_text: Some(false) }.into()),
                        ..Default::default()
                    }
                )),

                // コード補完
                completion_provider: Some(CompletionOptions {
                    resolve_provider: Some(true),
                    trigger_characters: Some(vec![
                        ".".to_string(),  // メンバーアクセス
                        ":".to_string(),  // 型注釈
                        "<".to_string(),  // ジェネリック
                        "(".to_string(),  // 関数呼び出し
                    ]),
                    all_commit_characters: None,
                    work_done_progress_options: Default::default(),
                    completion_item: Some(CompletionOptionsCompletionItem {
                        label_details_support: Some(true),
                    }),
                }),

                // ホバー情報
                hover_provider: Some(HoverProviderCapability::Simple(true)),

                // 定義ジャンプ
                definition_provider: Some(OneOf::Left(true)),
                type_definition_provider: Some(TypeDefinitionProviderCapability::Simple(true)),
                implementation_provider: Some(ImplementationProviderCapability::Simple(true)),

                // 参照検索
                references_provider: Some(OneOf::Left(true)),

                // ドキュメントシンボル
                document_symbol_provider: Some(OneOf::Left(true)),

                // ワークスペースシンボル
                workspace_symbol_provider: Some(OneOf::Left(true)),

                // コードアクション
                code_action_provider: Some(CodeActionProviderCapability::Simple(true)),

                // コードレンズ
                code_lens_provider: Some(CodeLensOptions {
                    resolve_provider: Some(true),
                }),

                // フォーマット
                document_formatting_provider: Some(OneOf::Left(true)),
                document_range_formatting_provider: Some(OneOf::Left(true)),
                document_on_type_formatting_provider: Some(DocumentOnTypeFormattingOptions {
                    first_trigger_character: "}".to_string(),
                    more_trigger_character: Some(vec![";".to_string()]),
                }),

                // リネーム
                rename_provider: Some(OneOf::Right(RenameOptions {
                    prepare_provider: Some(true),
                    work_done_progress_options: Default::default(),
                })),

                // シグネチャヘルプ
                signature_help_provider: Some(SignatureHelpOptions {
                    trigger_characters: Some(vec!["(".to_string(), ",".to_string()]),
                    retrigger_characters: Some(vec![")".to_string()]),
                    work_done_progress_options: Default::default(),
                }),

                // セマンティックトークン
                semantic_tokens_provider: Some(
                    SemanticTokensServerCapabilities::SemanticTokensOptions(
                        SemanticTokensOptions {
                            legend: SemanticTokensLegend {
                                token_types: vec![
                                    SemanticTokenType::NAMESPACE,
                                    SemanticTokenType::TYPE,
                                    SemanticTokenType::CLASS,
                                    SemanticTokenType::ENUM,
                                    SemanticTokenType::INTERFACE,
                                    SemanticTokenType::STRUCT,
                                    SemanticTokenType::TYPE_PARAMETER,
                                    SemanticTokenType::PARAMETER,
                                    SemanticTokenType::VARIABLE,
                                    SemanticTokenType::PROPERTY,
                                    SemanticTokenType::FUNCTION,
                                    SemanticTokenType::METHOD,
                                    SemanticTokenType::KEYWORD,
                                    SemanticTokenType::MODIFIER,
                                    SemanticTokenType::COMMENT,
                                    SemanticTokenType::STRING,
                                    SemanticTokenType::NUMBER,
                                    SemanticTokenType::OPERATOR,
                                ],
                                token_modifiers: vec![
                                    SemanticTokenModifier::DECLARATION,
                                    SemanticTokenModifier::DEFINITION,
                                    SemanticTokenModifier::READONLY,
                                    SemanticTokenModifier::STATIC,
                                    SemanticTokenModifier::ASYNC,
                                    SemanticTokenModifier::MODIFICATION,
                                    SemanticTokenModifier::DOCUMENTATION,
                                ],
                            },
                            full: Some(SemanticTokensFullOptions::Bool(true)),
                            range: Some(true),
                            ..Default::default()
                        }
                    )
                ),

                // インレイヒント
                inlay_hint_provider: Some(OneOf::Right(InlayHintServerCapabilities::Options(
                    InlayHintOptions {
                        resolve_provider: Some(true),
                        work_done_progress_options: Default::default(),
                    }
                ))),

                ..Default::default()
            },
            server_info: Some(ServerInfo {
                name: "cm-lsp".to_string(),
                version: Some(env!("CARGO_PKG_VERSION").to_string()),
            }),
        })
    }
}
```

### 2. コード補完

```rust
impl CmLanguageServer {
    async fn completion(
        &self,
        params: CompletionParams,
    ) -> Result<CompletionResponse> {
        let document = self.documents.get(&params.text_document_position.text_document.uri)?;
        let position = params.text_document_position.position;

        // コンテキスト解析
        let context = self.analyzer.get_completion_context(&document, position)?;

        let mut items = Vec::new();

        match context {
            CompletionContext::MemberAccess { receiver_type, .. } => {
                // 構造体/列挙型のメンバー補完
                items.extend(self.complete_members(receiver_type)?);
            }
            CompletionContext::TypePosition => {
                // 型名の補完
                items.extend(self.complete_types()?);
            }
            CompletionContext::FunctionCall { function, arg_index } => {
                // 関数引数の補完
                items.extend(self.complete_arguments(function, arg_index)?);
            }
            CompletionContext::Import => {
                // インポートパスの補完
                items.extend(self.complete_imports()?);
            }
            CompletionContext::General => {
                // 一般的な補完（変数、関数、キーワード）
                items.extend(self.complete_scope(&document, position)?);
                items.extend(self.complete_keywords()?);
                items.extend(self.complete_snippets()?);
            }
        }

        Ok(CompletionResponse::Array(items))
    }

    fn complete_members(&self, ty: &Type) -> Result<Vec<CompletionItem>> {
        let mut items = Vec::new();

        // 構造体のフィールド
        if let Type::Struct(s) = ty {
            for field in &s.fields {
                items.push(CompletionItem {
                    label: field.name.clone(),
                    kind: Some(CompletionItemKind::FIELD),
                    detail: Some(format!(": {}", field.ty)),
                    documentation: field.doc.as_ref().map(|d| {
                        Documentation::MarkupContent(MarkupContent {
                            kind: MarkupKind::Markdown,
                            value: d.clone(),
                        })
                    }),
                    ..Default::default()
                });
            }
        }

        // メソッド
        for method in self.type_checker.get_methods(ty)? {
            items.push(CompletionItem {
                label: method.name.clone(),
                kind: Some(CompletionItemKind::METHOD),
                detail: Some(method.signature()),
                insert_text: Some(format!("{}($0)", method.name)),
                insert_text_format: Some(InsertTextFormat::SNIPPET),
                ..Default::default()
            });
        }

        Ok(items)
    }

    fn complete_snippets(&self) -> Vec<CompletionItem> {
        vec![
            // 関数定義
            CompletionItem {
                label: "fn".to_string(),
                kind: Some(CompletionItemKind::SNIPPET),
                insert_text: Some("${1:return_type} ${2:function_name}(${3:params}) {\n    $0\n}".to_string()),
                insert_text_format: Some(InsertTextFormat::SNIPPET),
                detail: Some("Function definition".to_string()),
                ..Default::default()
            },
            // 構造体定義
            CompletionItem {
                label: "struct".to_string(),
                kind: Some(CompletionItemKind::SNIPPET),
                insert_text: Some("struct ${1:Name} {\n    ${2:field}: ${3:type}$0\n}".to_string()),
                insert_text_format: Some(InsertTextFormat::SNIPPET),
                detail: Some("Struct definition".to_string()),
                ..Default::default()
            },
            // forループ
            CompletionItem {
                label: "for".to_string(),
                kind: Some(CompletionItemKind::SNIPPET),
                insert_text: Some("for (${1:init}; ${2:condition}; ${3:update}) {\n    $0\n}".to_string()),
                insert_text_format: Some(InsertTextFormat::SNIPPET),
                detail: Some("For loop".to_string()),
                ..Default::default()
            },
        ]
    }
}
```

### 3. 型情報とホバー

```rust
impl CmLanguageServer {
    async fn hover(&self, params: HoverParams) -> Result<Option<Hover>> {
        let document = self.documents.get(&params.text_document_position_params.text_document.uri)?;
        let position = params.text_document_position_params.position;

        // 位置のシンボルを取得
        let symbol = self.analyzer.get_symbol_at(&document, position)?;

        if let Some(symbol) = symbol {
            let mut contents = Vec::new();

            // 型情報
            if let Some(ty) = self.type_checker.get_type(&symbol) {
                contents.push(MarkedString::LanguageString(LanguageString {
                    language: "cm".to_string(),
                    value: format!("{}: {}", symbol.name, ty),
                }));
            }

            // ドキュメント
            if let Some(doc) = self.get_documentation(&symbol) {
                contents.push(MarkedString::String(doc));
            }

            // 使用例
            if let Some(example) = self.get_example(&symbol) {
                contents.push(MarkedString::LanguageString(LanguageString {
                    language: "cm".to_string(),
                    value: example,
                }));
            }

            return Ok(Some(Hover {
                contents: HoverContents::Array(contents),
                range: Some(symbol.range),
            }));
        }

        Ok(None)
    }
}
```

### 4. 定義ジャンプ

```rust
impl CmLanguageServer {
    async fn goto_definition(
        &self,
        params: GotoDefinitionParams,
    ) -> Result<Option<GotoDefinitionResponse>> {
        let document = self.documents.get(&params.text_document_position_params.text_document.uri)?;
        let position = params.text_document_position_params.position;

        // シンボルの定義を検索
        if let Some(symbol) = self.analyzer.get_symbol_at(&document, position)? {
            if let Some(definition) = self.symbol_index.get_definition(&symbol.id)? {
                return Ok(Some(GotoDefinitionResponse::Scalar(Location {
                    uri: definition.file_uri,
                    range: definition.range,
                })));
            }
        }

        Ok(None)
    }

    async fn goto_type_definition(
        &self,
        params: TypeDefinitionParams,
    ) -> Result<Option<TypeDefinitionResponse>> {
        let document = self.documents.get(&params.text_document_position_params.text_document.uri)?;
        let position = params.text_document_position_params.position;

        // 変数の型定義にジャンプ
        if let Some(symbol) = self.analyzer.get_symbol_at(&document, position)? {
            if let Some(ty) = self.type_checker.get_type(&symbol)? {
                if let Some(type_def) = self.symbol_index.get_type_definition(&ty)? {
                    return Ok(Some(TypeDefinitionResponse::Scalar(Location {
                        uri: type_def.file_uri,
                        range: type_def.range,
                    })));
                }
            }
        }

        Ok(None)
    }
}
```

### 5. リアルタイム診断

```rust
impl CmLanguageServer {
    async fn publish_diagnostics(&self, uri: &Url) -> Result<()> {
        let document = self.documents.get(uri)?;

        // インクリメンタル解析
        let ast = self.analyzer.parse_incremental(&document)?;

        // 型チェック
        let type_errors = self.type_checker.check(&ast)?;

        // リントチェック
        let lint_warnings = self.linter.check(&ast)?;

        // 診断メッセージを生成
        let mut diagnostics = Vec::new();

        // エラー
        for error in type_errors {
            diagnostics.push(Diagnostic {
                range: error.range,
                severity: Some(DiagnosticSeverity::ERROR),
                code: Some(NumberOrString::String(error.code.clone())),
                source: Some("cm".to_string()),
                message: error.message,
                related_information: error.related_info.map(|info| {
                    info.into_iter().map(|i| DiagnosticRelatedInformation {
                        location: Location {
                            uri: i.file_uri,
                            range: i.range,
                        },
                        message: i.message,
                    }).collect()
                }),
                ..Default::default()
            });
        }

        // 警告
        for warning in lint_warnings {
            diagnostics.push(Diagnostic {
                range: warning.range,
                severity: Some(DiagnosticSeverity::WARNING),
                code: Some(NumberOrString::String(warning.code.clone())),
                source: Some("cm-lint".to_string()),
                message: warning.message,
                code_description: Some(CodeDescription {
                    href: format!("https://cm-lang.org/lint/{}", warning.code),
                }),
                ..Default::default()
            });
        }

        // 診断を送信
        self.client.publish_diagnostics(PublishDiagnosticsParams {
            uri: uri.clone(),
            version: document.version,
            diagnostics,
        }).await;

        Ok(())
    }
}
```

### 6. コードアクション

```rust
impl CmLanguageServer {
    async fn code_action(
        &self,
        params: CodeActionParams,
    ) -> Result<Option<CodeActionResponse>> {
        let mut actions = Vec::new();

        // Quick Fix
        for diagnostic in &params.context.diagnostics {
            if let Some(fixes) = self.get_quick_fixes(diagnostic)? {
                for fix in fixes {
                    actions.push(CodeActionOrCommand::CodeAction(CodeAction {
                        title: fix.title,
                        kind: Some(CodeActionKind::QUICKFIX),
                        diagnostics: Some(vec![diagnostic.clone()]),
                        edit: Some(fix.workspace_edit),
                        ..Default::default()
                    }));
                }
            }
        }

        // Refactoring
        if params.context.only.as_ref()
            .map_or(true, |only| only.contains(&CodeActionKind::REFACTOR))
        {
            // Extract Function
            if let Some(selection) = self.get_valid_extraction(&params.range)? {
                actions.push(CodeActionOrCommand::CodeAction(CodeAction {
                    title: "Extract function".to_string(),
                    kind: Some(CodeActionKind::REFACTOR_EXTRACT),
                    edit: Some(self.extract_function(selection)?),
                    ..Default::default()
                }));
            }

            // Extract Variable
            if let Some(expr) = self.get_expression_at(&params.range)? {
                actions.push(CodeActionOrCommand::CodeAction(CodeAction {
                    title: "Extract variable".to_string(),
                    kind: Some(CodeActionKind::REFACTOR_EXTRACT),
                    edit: Some(self.extract_variable(expr)?),
                    ..Default::default()
                }));
            }
        }

        Ok(Some(actions))
    }
}
```

### 7. インレイヒント

```rust
impl CmLanguageServer {
    async fn inlay_hint(
        &self,
        params: InlayHintParams,
    ) -> Result<Option<Vec<InlayHint>>> {
        let document = self.documents.get(&params.text_document.uri)?;
        let mut hints = Vec::new();

        // 型ヒント
        if self.config.inlay_hints.type_hints {
            for var in self.analyzer.get_variables_in_range(&document, params.range)? {
                if var.ty.is_inferred() {
                    hints.push(InlayHint {
                        position: var.name_end_position(),
                        label: InlayHintLabel::String(format!(": {}", var.ty)),
                        kind: Some(InlayHintKind::TYPE),
                        text_edits: None,
                        tooltip: None,
                        padding_left: Some(false),
                        padding_right: Some(true),
                        data: None,
                    });
                }
            }
        }

        // パラメータ名ヒント
        if self.config.inlay_hints.parameter_hints {
            for call in self.analyzer.get_function_calls_in_range(&document, params.range)? {
                for (i, arg) in call.arguments.iter().enumerate() {
                    if let Some(param_name) = self.get_parameter_name(&call.function, i)? {
                        hints.push(InlayHint {
                            position: arg.start_position(),
                            label: InlayHintLabel::String(format!("{}: ", param_name)),
                            kind: Some(InlayHintKind::PARAMETER),
                            ..Default::default()
                        });
                    }
                }
            }
        }

        Ok(Some(hints))
    }
}
```

## インクリメンタル解析

### 差分解析エンジン

```rust
pub struct IncrementalAnalyzer {
    // ASTキャッシュ
    ast_cache: HashMap<Url, CachedAst>,

    // 型情報キャッシュ
    type_cache: TypeCache,

    // シンボルテーブル
    symbol_table: SymbolTable,
}

impl IncrementalAnalyzer {
    fn parse_incremental(&mut self, document: &Document) -> Result<Ast> {
        if let Some(cached) = self.ast_cache.get(&document.uri) {
            // 変更された部分のみ再解析
            let changes = document.get_changes_since(cached.version);

            if changes.is_small() {
                // 部分的な再解析
                let mut ast = cached.ast.clone();
                for change in changes {
                    self.reparse_node(&mut ast, change)?;
                }
                return Ok(ast);
            }
        }

        // 完全な再解析
        let ast = self.parse_full(document)?;
        self.ast_cache.insert(document.uri.clone(), CachedAst {
            ast: ast.clone(),
            version: document.version,
        });

        Ok(ast)
    }
}
```

## パフォーマンス最適化

### 1. 並列処理

```rust
impl CmLanguageServer {
    async fn handle_file_changes(&self, changes: Vec<FileChangeEvent>) {
        // ファイル変更を並列処理
        let futures = changes.into_iter().map(|change| {
            let self_clone = self.clone();
            async move {
                match change.typ {
                    FileChangeType::CREATED | FileChangeType::CHANGED => {
                        self_clone.analyze_file(&change.uri).await
                    }
                    FileChangeType::DELETED => {
                        self_clone.remove_file(&change.uri).await
                    }
                }
            }
        });

        futures::future::join_all(futures).await;
    }
}
```

### 2. キャッシュ戦略

```rust
pub struct CacheManager {
    // LRUキャッシュ
    ast_cache: LruCache<Url, Arc<Ast>>,
    type_cache: LruCache<NodeId, Type>,
    symbol_cache: LruCache<String, Vec<Symbol>>,

    // メモリ制限
    max_memory: usize,
}

impl CacheManager {
    fn evict_if_needed(&mut self) {
        while self.current_memory() > self.max_memory {
            // 最も使われていないエントリを削除
            self.ast_cache.pop_lru();
        }
    }
}
```

## テスト戦略

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use lsp_test::*;

    #[tokio::test]
    async fn test_completion() {
        let server = setup_test_server().await;

        // テストファイルを開く
        server.did_open("test.cm", r#"
            struct Point {
                int x;
                int y;
            }

            int main() {
                Point p;
                p.
            }
        "#).await;

        // 補完リクエスト
        let completions = server.completion(Position::new(8, 18)).await;

        assert!(completions.items.iter().any(|i| i.label == "x"));
        assert!(completions.items.iter().any(|i| i.label == "y"));
    }

    #[tokio::test]
    async fn test_diagnostics() {
        let server = setup_test_server().await;

        // エラーを含むコード
        server.did_open("error.cm", r#"
            int main() {
                int x = "string";  // 型エラー
            }
        "#).await;

        // 診断メッセージを確認
        let diagnostics = server.wait_for_diagnostics().await;

        assert_eq!(diagnostics.len(), 1);
        assert_eq!(diagnostics[0].severity, Some(DiagnosticSeverity::ERROR));
    }
}
```

## 実装優先順位

1. **Phase 1** (v0.12.0): 基本LSP機能（補完、診断、定義ジャンプ）
2. **Phase 2** (v0.13.0): 高度な機能（リファクタリング、インレイヒント）
3. **Phase 3** (v0.14.0): デバッグアダプタプロトコル（DAP）統合