{
  description = "Cm Programming Language Compiler";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        
        # LLVM 17を使用
        llvmPackages = pkgs.llvmPackages_17;
      in
      {
        devShells.default = pkgs.mkShell {
          name = "cm-dev";
          
          buildInputs = with pkgs; [
            # ビルドツール
            cmake
            ninja
            gnumake
            
            # LLVM 17
            llvmPackages.llvm
            llvmPackages.clang
            llvmPackages.lld
            
            # テスト
            gtest
            
            # 開発ツール
            clang-tools  # clang-format, clang-tidy
            
            # Node.js (JSバックエンドテスト用)
            nodejs_20
          ];
          
          # 環境変数
          shellHook = ''
            export LLVM_DIR="${llvmPackages.llvm.dev}/lib/cmake/llvm"
            export CC="${llvmPackages.clang}/bin/clang"
            export CXX="${llvmPackages.clang}/bin/clang++"
            echo "🚀 Cm開発環境 (LLVM ${llvmPackages.llvm.version})"
          '';
        };
        
        # パッケージ定義（将来用）
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "cm";
          version = "0.12.0";
          src = ./.;
          
          nativeBuildInputs = with pkgs; [ cmake ninja ];
          buildInputs = with pkgs; [
            llvmPackages.llvm
            gtest
          ];
          
          cmakeFlags = [
            "-DCM_USE_LLVM=ON"
            "-DBUILD_TESTING=OFF"
          ];
        };
      }
    );
}
