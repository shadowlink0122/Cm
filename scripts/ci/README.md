# CI/CDスクリプト

GitHub Actionsワークフロー（`.github/workflows/`）の各ステップの実処理。
ワークフローYAMLは手順の宣言に徹し、ロジックは全てここの実行可能シェルスクリプトに分割されている。
`GITHUB_PATH`/`GITHUB_ENV`/`GITHUB_OUTPUT` はローカル実行時は `/dev/null` にフォールバックするため、CIと同じ処理をローカルで再現できる。

| スクリプト | 用途 | 使用ワークフロー |
|---|---|---|
| install_llvm.sh [ver] [--minimal] | LLVMインストール（Linux=apt / macOS=brew。--minimalはllvm-devのみ） | ci / release |
| install_clang_format.sh [ver] | clang-formatインストール | ci (lint) |
| check_cpp_format.sh [ver] | C++フォーマット検査（make format-checkと同一対象） | ci (lint) |
| build_compiler.sh <type> <arch> [--testing] | CMake configure+ビルド | ci / release |
| verify_build.sh [--strict] | 成果物検証（--strictは欠落を即エラー） | ci / release |
| install_wasmtime.sh | wasmtimeインストール+検証（macOS=brew。失敗時はnodeフォールバックを警告付きで許可） | ci (wasm) |
| install_sv_tools.sh | iverilogインストール | ci (sv) |
| run_backend_tests.sh <target> | バックエンドスイート実行+FAIL行のAnnotation出力 | ci (integration) |
| run_benchmarks.sh | JITベンチマーク実行 | ci (benchmark) |
| check_version_branch.sh | VERSIONとブランチ名の整合 | version-check |
| check_release_note.sh | リリースノート存在確認 | version-check |
| check_tutorial_version.sh | チュートリアルのバージョン表記確認 | version-check |
| check_vscode_version.sh | VSCode拡張package.jsonのバージョン整合 | version-check |
| check_release_version.sh <ver> | リリース入力とVERSION/拡張の整合 | release |
| package_vscode_extension.sh | VSCode拡張の.vsixパッケージング | release |
| build_dist.sh <ver> <arch> | 配布物tar.gz構築 | release |
