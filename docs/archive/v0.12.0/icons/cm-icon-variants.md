# Cm Language Icon Variants

## Icon Files

### 1. cm-icon.svg (Primary)
メインアイコン - グラデーション付きの美しいデザイン

### 2. cm-icon-mono.svg (Monochrome)
```xml
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg width="256" height="256" viewBox="0 0 256 256" version="1.1" xmlns="http://www.w3.org/2000/svg">
  <!-- Monochrome version for single color displays -->
  <g fill="#000000">
    <!-- Outer ring -->
    <path d="M 128 8 A 120 120 0 1 1 127.9 8 Z M 128 28 A 100 100 0 1 0 128.1 28 Z" />

    <!-- C shape -->
    <path d="M 180 128 A 60 60 0 1 0 128 68 L 128 88 A 40 40 0 1 1 160 128 Z" />

    <!-- m shape -->
    <path d="M 108 148 L 108 118 Q 118 108 128 118 L 128 148 M 128 118 Q 138 108 148 118 L 148 148"
          stroke-width="12" stroke="#000000" fill="none" />
  </g>
</svg>
```

### 3. cm-icon-flat.svg (Flat Design)
```xml
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg width="256" height="256" viewBox="0 0 256 256" version="1.1" xmlns="http://www.w3.org/2000/svg">
  <!-- Flat design version -->
  <rect width="256" height="256" rx="32" fill="#4A90E2"/>

  <!-- White C and m -->
  <g fill="#FFFFFF">
    <path d="M 180 128 A 60 60 0 1 0 128 68 L 128 88 A 40 40 0 1 1 160 128"
          fill="none" stroke="#FFFFFF" stroke-width="16" stroke-linecap="round"/>

    <path d="M 108 148 L 108 118 Q 118 108 128 118 L 128 148 M 128 118 Q 138 108 148 118 L 148 148"
          stroke-width="12" stroke="#FFFFFF" fill="none" stroke-linecap="round"/>
  </g>
</svg>
```

### 4. cm-icon-small.svg (16x16 Favicon)
```xml
<?xml version="1.0" encoding="UTF-8"?>
<svg width="16" height="16" viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
  <!-- Simplified for small sizes -->
  <circle cx="8" cy="8" r="7" fill="#4A90E2"/>
  <path d="M 11 8 A 3.5 3.5 0 1 0 8 4.5"
        fill="none" stroke="#FFFFFF" stroke-width="1.5" stroke-linecap="round"/>
  <path d="M 6 10 L 6 7 L 8 9 L 10 7 L 10 10"
        fill="none" stroke="#FFFFFF" stroke-width="1" stroke-linecap="round"/>
</svg>
```

## 使用ガイドライン

### カラーパレット
- **Primary Blue**: #4A90E2
- **Purple Accent**: #7B68EE
- **White**: #FFFFFF
- **Dark**: #2C3E50

### サイズバリエーション
- **Large**: 512x512px - マーケティング資料
- **Medium**: 256x256px - アプリケーションアイコン
- **Small**: 64x64px - ツールバーアイコン
- **Tiny**: 16x16px - ファビコン

### 使用場所
1. **VSCode拡張**: 128x128px PNG
2. **Antigravity拡張**: 128x128px PNG
3. **Webサイト**: SVGまたはPNG各種サイズ
4. **ドキュメント**: SVGベクター形式
5. **パッケージマネージャー**: 32x32px PNG

## アイコンの意味

### デザインコンセプト
- **'C'文字**: C言語の系譜を表現
- **'m'文字**: "memory safe"と"modern"を象徴
- **円形**: 完全性と統合を表現
- **グラデーション**: 現代的で進歩的な言語を表現

## ファイルエクスポート

### PNG版の生成
```bash
# SVGからPNGへの変換コマンド
for size in 16 32 64 128 256 512; do
  inkscape -w $size -h $size cm-icon.svg -o cm-icon-${size}.png
done
```

### ICO版の生成（Windows用）
```bash
convert cm-icon-16.png cm-icon-32.png cm-icon-64.png cm-icon-128.png cm-icon-256.png cm-icon.ico
```

### ICNS版の生成（macOS用）
```bash
iconutil -c icns cm-icon.iconset -o cm-icon.icns
```