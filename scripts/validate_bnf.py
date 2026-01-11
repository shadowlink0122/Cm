#!/usr/bin/env python3
"""
BNF Grammar Validator and Formatter for Cm Language
Usage: python validate_bnf.py [options] <bnf_file>
"""

import argparse
import re
import sys
from collections import defaultdict, deque
from pathlib import Path
from typing import Dict, List, Set, Tuple, Optional

class BNFGrammar:
    """BNF文法の表現と解析"""

    def __init__(self):
        self.productions: Dict[str, List[List[str]]] = defaultdict(list)
        self.terminals: Set[str] = set()
        self.nonterminals: Set[str] = set()
        self.start_symbol: Optional[str] = None
        self.metadata: Dict[str, str] = {}

    def parse_file(self, filepath: str):
        """BNFファイルをパース"""
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()

        # コメントを削除
        content = re.sub(r'#.*$', '', content, flags=re.MULTILINE)

        # プロダクションルールをパース
        current_lhs = None
        current_alternatives = []

        for line in content.split('\n'):
            line = line.strip()
            if not line:
                continue

            # 新しいプロダクション
            if '::=' in line:
                if current_lhs:
                    self.productions[current_lhs] = current_alternatives
                    current_alternatives = []

                parts = line.split('::=')
                current_lhs = parts[0].strip()
                rhs = parts[1].strip()

                if not self.start_symbol:
                    self.start_symbol = current_lhs

                self.nonterminals.add(current_lhs)
                current_alternatives.append(self._parse_alternative(rhs))

            # 継続行（|で始まる）
            elif line.startswith('|'):
                if current_lhs:
                    alt = line[1:].strip()
                    current_alternatives.append(self._parse_alternative(alt))

        # 最後のプロダクションを追加
        if current_lhs:
            self.productions[current_lhs] = current_alternatives

    def _parse_alternative(self, alt: str) -> List[str]:
        """選択肢をトークンに分割"""
        tokens = []
        # シングルクォートで囲まれた終端記号を保持
        parts = re.findall(r"'[^']*'|\S+", alt)

        for part in parts:
            if part.startswith("'") and part.endswith("'"):
                # 終端記号
                self.terminals.add(part)
                tokens.append(part)
            else:
                # 非終端記号または演算子
                if part in ['*', '+', '?', '(', ')', '|']:
                    tokens.append(part)
                else:
                    tokens.append(part)

        return tokens

class BNFValidator:
    """BNF文法の検証"""

    def __init__(self, grammar: BNFGrammar):
        self.grammar = grammar
        self.errors: List[str] = []
        self.warnings: List[str] = []
        self.first_sets: Dict[str, Set[str]] = {}
        self.follow_sets: Dict[str, Set[str]] = {}

    def validate(self) -> bool:
        """文法を検証"""
        self._check_undefined_symbols()
        self._check_unreachable_symbols()
        self._check_left_recursion()
        self._compute_first_follow()
        self._check_ll1_conflicts()

        return len(self.errors) == 0

    def _check_undefined_symbols(self):
        """未定義の非終端記号をチェック"""
        for lhs, alternatives in self.grammar.productions.items():
            for alt in alternatives:
                for symbol in alt:
                    if (not symbol.startswith("'") and
                        symbol not in ['*', '+', '?', '(', ')', '|'] and
                        symbol not in self.grammar.productions):
                        # プリミティブ型とキーワードは除外
                        if not self._is_builtin(symbol):
                            self.errors.append(f"Undefined nonterminal: {symbol}")

    def _is_builtin(self, symbol: str) -> bool:
        """組み込みシンボルかチェック"""
        builtins = {
            'identifier', 'integer_literal', 'float_literal',
            'char_literal', 'string_literal', 'bool_literal',
            'int', 'double', 'float', 'bool', 'char', 'void', 'string',
            'tiny', 'short', 'long', 'uint', 'utiny', 'ushort', 'ulong',
            'size_t', 'null_literal',
            'if', 'else', 'while', 'for', 'return', 'break', 'continue',
            'switch', 'case', 'default', 'defer', 'struct', 'enum', 'trait',
            'impl', 'typedef', 'macro', 'import', 'export', 'module',
            'operator', 'private', 'const', 'static', 'sizeof', 'typeof',
            'true', 'false', 'null', 'nullptr', 'auto', 'with', 'where', 'as'
        }
        return symbol in builtins or symbol.startswith('[') and symbol.endswith(']')

    def _check_unreachable_symbols(self):
        """到達不可能な記号をチェック"""
        reachable = set()
        queue = deque([self.grammar.start_symbol])

        while queue:
            symbol = queue.popleft()
            if symbol in reachable:
                continue

            reachable.add(symbol)

            if symbol in self.grammar.productions:
                for alt in self.grammar.productions[symbol]:
                    for s in alt:
                        if s in self.grammar.productions and s not in reachable:
                            queue.append(s)

        for symbol in self.grammar.productions:
            if symbol not in reachable:
                self.warnings.append(f"Unreachable symbol: {symbol}")

    def _check_left_recursion(self):
        """左再帰をチェック"""
        for lhs, alternatives in self.grammar.productions.items():
            for alt in alternatives:
                if alt and alt[0] == lhs:
                    self.warnings.append(f"Direct left recursion: {lhs} ::= {lhs} ...")

    def _compute_first_follow(self):
        """FIRST集合とFOLLOW集合を計算"""
        # 簡略版の実装
        for symbol in self.grammar.productions:
            self.first_sets[symbol] = set()
            self.follow_sets[symbol] = set()

    def _check_ll1_conflicts(self):
        """LL(1)競合をチェック"""
        for lhs, alternatives in self.grammar.productions.items():
            if len(alternatives) > 1:
                # 各選択肢のFIRST集合が重複していないかチェック
                first_sets = []
                for alt in alternatives:
                    if alt and alt[0].startswith("'"):
                        first_sets.append({alt[0]})
                    elif alt and alt[0] in self.grammar.productions:
                        first_sets.append(self.first_sets.get(alt[0], set()))

                # 重複チェック
                for i in range(len(first_sets)):
                    for j in range(i + 1, len(first_sets)):
                        intersection = first_sets[i] & first_sets[j]
                        if intersection:
                            self.warnings.append(
                                f"LL(1) conflict in {lhs}: "
                                f"overlapping FIRST sets {intersection}"
                            )

class BNFFormatter:
    """BNF文法のフォーマットと出力"""

    def __init__(self, grammar: BNFGrammar):
        self.grammar = grammar

    def to_text(self) -> str:
        """テキスト形式で出力"""
        lines = []
        lines.append("=" * 60)
        lines.append("Cm Language Grammar (BNF)")
        lines.append("=" * 60)
        lines.append("")

        # 統計情報
        lines.append(f"Start symbol: {self.grammar.start_symbol}")
        lines.append(f"Productions: {len(self.grammar.productions)}")
        lines.append(f"Terminals: {len(self.grammar.terminals)}")
        lines.append(f"Nonterminals: {len(self.grammar.nonterminals)}")
        lines.append("")
        lines.append("-" * 60)
        lines.append("")

        # プロダクションルール
        for lhs, alternatives in self.grammar.productions.items():
            if not alternatives:
                continue

            # 最初の選択肢
            lines.append(f"{lhs}")
            lines.append(f"    ::= {' '.join(alternatives[0])}")

            # 残りの選択肢
            for alt in alternatives[1:]:
                lines.append(f"      | {' '.join(alt)}")

            lines.append("")

        return '\n'.join(lines)

    def to_html(self) -> str:
        """HTML形式で出力"""
        html = []
        html.append("""<!DOCTYPE html>
<html>
<head>
    <title>Cm Language Grammar</title>
    <style>
        body { font-family: monospace; margin: 20px; }
        .production { margin: 20px 0; padding: 10px; border-left: 3px solid #007acc; }
        .lhs { font-weight: bold; color: #007acc; }
        .terminal { color: #008000; }
        .nonterminal { color: #800080; }
        .operator { color: #ff6600; }
        .alternative { margin-left: 40px; }
        .stats { background: #f0f0f0; padding: 10px; margin: 20px 0; }
        h1 { color: #007acc; }
    </style>
</head>
<body>
    <h1>Cm Language Grammar (BNF)</h1>
""")

        # 統計情報
        html.append('<div class="stats">')
        html.append(f'<strong>Statistics:</strong><br>')
        html.append(f'Start Symbol: <span class="nonterminal">{self.grammar.start_symbol}</span><br>')
        html.append(f'Productions: {len(self.grammar.productions)}<br>')
        html.append(f'Terminals: {len(self.grammar.terminals)}<br>')
        html.append(f'Nonterminals: {len(self.grammar.nonterminals)}<br>')
        html.append('</div>')

        # プロダクションルール
        for lhs, alternatives in self.grammar.productions.items():
            if not alternatives:
                continue

            html.append('<div class="production">')
            html.append(f'<span class="lhs">{lhs}</span> ::=')

            first = True
            for alt in alternatives:
                if not first:
                    html.append('<div class="alternative">|')
                else:
                    html.append('<div class="alternative">')

                for token in alt:
                    if token.startswith("'"):
                        html.append(f'<span class="terminal">{token}</span>')
                    elif token in ['*', '+', '?', '|', '(', ')']:
                        html.append(f'<span class="operator">{token}</span>')
                    else:
                        html.append(f'<span class="nonterminal">{token}</span>')
                    html.append(' ')

                html.append('</div>')
                first = False

            html.append('</div>')

        html.append("""
</body>
</html>
""")

        return '\n'.join(html)

    def to_dot(self) -> str:
        """Graphviz DOT形式で出力"""
        dot = []
        dot.append('digraph CmGrammar {')
        dot.append('    rankdir=LR;')
        dot.append('    node [shape=box, style=rounded];')
        dot.append('')

        # スタートシンボルを強調
        dot.append(f'    "{self.grammar.start_symbol}" [style="rounded,bold", color=blue];')
        dot.append('')

        # エッジを追加
        for lhs, alternatives in self.grammar.productions.items():
            for alt in alternatives:
                # 非終端記号への依存関係のみ表示
                seen = set()
                for symbol in alt:
                    if symbol in self.grammar.productions and symbol not in seen:
                        dot.append(f'    "{lhs}" -> "{symbol}";')
                        seen.add(symbol)

        dot.append('}')

        return '\n'.join(dot)

def main():
    parser = argparse.ArgumentParser(description='BNF Grammar Validator and Formatter')
    parser.add_argument('bnf_file', help='Path to BNF grammar file')
    parser.add_argument('-o', '--output', help='Output file (default: stdout)')
    parser.add_argument('-f', '--format', choices=['text', 'html', 'dot'],
                       default='text', help='Output format')
    parser.add_argument('-v', '--validate', action='store_true',
                       help='Validate grammar')
    parser.add_argument('--stats', action='store_true',
                       help='Show statistics only')

    args = parser.parse_args()

    # BNFファイルを読み込み
    grammar = BNFGrammar()
    try:
        grammar.parse_file(args.bnf_file)
    except Exception as e:
        print(f"Error parsing BNF file: {e}", file=sys.stderr)
        sys.exit(1)

    # 検証
    if args.validate:
        validator = BNFValidator(grammar)
        is_valid = validator.validate()

        print("=" * 60)
        print("Validation Results")
        print("=" * 60)

        if validator.errors:
            print("\nERRORS:")
            for error in validator.errors:
                print(f"  ✗ {error}")

        if validator.warnings:
            print("\nWARNINGS:")
            for warning in validator.warnings:
                print(f"  ⚠ {warning}")

        if is_valid:
            print("\n✓ Grammar is valid")
        else:
            print("\n✗ Grammar has errors")
            sys.exit(1)

        if not args.output and not args.stats:
            sys.exit(0)

    # 統計情報のみ
    if args.stats:
        print(f"Start symbol: {grammar.start_symbol}")
        print(f"Productions: {len(grammar.productions)}")
        print(f"Terminals: {len(grammar.terminals)}")
        print(f"Nonterminals: {len(grammar.nonterminals)}")
        sys.exit(0)

    # フォーマット出力
    formatter = BNFFormatter(grammar)

    if args.format == 'text':
        output = formatter.to_text()
    elif args.format == 'html':
        output = formatter.to_html()
    elif args.format == 'dot':
        output = formatter.to_dot()

    # 出力
    if args.output:
        with open(args.output, 'w', encoding='utf-8') as f:
            f.write(output)
        print(f"Output written to {args.output}")
    else:
        print(output)

if __name__ == '__main__':
    main()