#!/usr/bin/env node

/**
 * VSCode拡張機能のバージョンがルートのVERSIONファイルと一致しているか確認するスクリプト
 * CI/CDやビルド前のチェックに使用
 * 使用方法: node out/verify-version.js
 */

import * as fs from 'fs';
import * as path from 'path';

// 型定義
interface PackageJson {
  version: string;
  [key: string]: unknown;
}

// パス設定
const rootDir = path.join(__dirname, '../../..');
const versionFile = path.join(rootDir, 'VERSION');
const packageJsonPath = path.join(__dirname, '../../package.json');

// VERSIONファイルを読み込み
if (!fs.existsSync(versionFile)) {
  console.error('❌ Error: VERSION file not found at', versionFile);
  process.exit(1);
}

const expectedVersion = fs.readFileSync(versionFile, 'utf8').trim();

if (!expectedVersion) {
  console.error('❌ Error: VERSION file is empty');
  process.exit(1);
}

// package.jsonを読み込み
if (!fs.existsSync(packageJsonPath)) {
  console.error('❌ Error: package.json not found at', packageJsonPath);
  process.exit(1);
}

const packageJson: PackageJson = JSON.parse(fs.readFileSync(packageJsonPath, 'utf8'));
const actualVersion = packageJson.version;

// バージョンチェック
if (actualVersion !== expectedVersion) {
  console.error('❌ Version mismatch detected!');
  console.error('   VERSION:', expectedVersion);
  console.error('   package.json:', actualVersion);
  console.error('');
  console.error('💡 Fix by running: cd vscode-extension && npm run update-version');
  process.exit(1);
}

console.log('✅ Version check passed:', actualVersion);
console.log('   VSCode extension version matches VERSION file');
