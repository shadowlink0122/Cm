#!/bin/bash
# Add Jekyll front matter to tutorial files

add_frontmatter() {
    local file=$1
    local title=$2
    local parent=$3
    local nav_order=$4
    
    # Check if file already has front matter
    if head -n 1 "$file" | grep -q "^---$"; then
        echo "Skipping $file (already has front matter)"
        return
    fi
    
    # Create temporary file with front matter
    cat > "${file}.tmp" << EOF
---
layout: default
title: ${title}
parent: ${parent}
nav_order: ${nav_order}
---

EOF
    
    # Append original content
    cat "$file" >> "${file}.tmp"
    
    # Replace original
    mv "${file}.tmp" "$file"
    echo "✅ Added front matter to $file"
}

cd /Users/shadowlink/Documents/git/Cm/docs

# Basics tutorials
echo "Processing basics tutorials..."
add_frontmatter "tutorials/basics/introduction.md" "はじめに" "Tutorials" 1
add_frontmatter "tutorials/basics/setup.md" "環境構築" "Tutorials" 2
add_frontmatter "tutorials/basics/hello-world.md" "Hello World" "Tutorials" 3
add_frontmatter "tutorials/basics/variables.md" "変数と型" "Tutorials" 4
add_frontmatter "tutorials/basics/operators.md" "演算子" "Tutorials" 5
add_frontmatter "tutorials/basics/control-flow.md" "制御構文" "Tutorials" 6
add_frontmatter "tutorials/basics/functions.md" "関数" "Tutorials" 7
add_frontmatter "tutorials/basics/arrays.md" "配列" "Tutorials" 8
add_frontmatter "tutorials/basics/pointers.md" "ポインタ" "Tutorials" 9

# Types tutorials
echo "Processing types tutorials..."
add_frontmatter "tutorials/types/structs.md" "構造体" "Tutorials" 10
add_frontmatter "tutorials/types/enums.md" "列挙型" "Tutorials" 11
add_frontmatter "tutorials/types/interfaces.md" "インターフェース" "Tutorials" 12
add_frontmatter "tutorials/types/typedefs.md" "型エイリアス" "Tutorials" 13
add_frontmatter "tutorials/types/generics.md" "ジェネリクス" "Tutorials" 14
add_frontmatter "tutorials/types/constraints.md" "型制約" "Tutorials" 15

# Advanced tutorials
echo "Processing advanced tutorials..."
add_frontmatter "tutorials/advanced/match.md" "パターンマッチング" "Tutorials" 20
add_frontmatter "tutorials/advanced/macros.md" "マクロ" "Tutorials" 21
add_frontmatter "tutorials/advanced/operators.md" "演算子オーバーロード" "Tutorials" 22
add_frontmatter "tutorials/advanced/with-keyword.md" "with自動実装" "Tutorials" 23
add_frontmatter "tutorials/advanced/function-pointers.md" "関数ポインタ" "Tutorials" 24
add_frontmatter "tutorials/advanced/strings.md" "文字列処理" "Tutorials" 25

# Compiler tutorials
echo "Processing compiler tutorials..."
add_frontmatter "tutorials/compiler/usage.md" "コンパイラ使い方" "Tutorials" 30
add_frontmatter "tutorials/compiler/llvm.md" "LLVMバックエンド" "Tutorials" 31
add_frontmatter "tutorials/compiler/wasm.md" "WASMバックエンド" "Tutorials" 32

echo "✅ All done!"
