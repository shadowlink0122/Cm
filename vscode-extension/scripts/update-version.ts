#!/usr/bin/env node

/**
 * VSCode拡張機能のバージョンをルートのVERSIONファイルから自動更新するスクリプト
 * 使用方法: node out/update-version.js
 */

import * as fs from 'fs';
import * as path from 'path';

// 型定義
interface PackageJson {
  version: string;
  [key: string]: unknown;
}

// パス設定
const rootDir = path.join(__dirname, '../..');
const versionFile = path.join(rootDir, 'VERSION');
const packageJsonPath = path.join(__dirname, '../package.json');

// VERSIONファイルを読み込み
if (!fs.existsSync(versionFile)) {
  console.error('❌ Error: VERSION file not found at', versionFile);
  process.exit(1);
}

const version = fs.readFileSync(versionFile, 'utf8').trim();

if (!version) {
  console.error('❌ Error: VERSION file is empty');
  process.exit(1);
}

// バージョンフォーマットチェック (x.y.z)
if (!/^\d+\.\d+\.\d+$/.test(version)) {
  console.error('❌ Error: Invalid version format in VERSION:', version);
  console.error('   Expected format: x.y.z (e.g., 0.14.0)');
  process.exit(1);
}

// package.jsonを読み込み
if (!fs.existsSync(packageJsonPath)) {
  console.error('❌ Error: package.json not found at', packageJsonPath);
  process.exit(1);
}

const packageJson: PackageJson = JSON.parse(fs.readFileSync(packageJsonPath, 'utf8'));
const oldVersion = packageJson.version;

// バージョンが既に同じ場合はスキップ
if (oldVersion === version) {
  console.log('✅ Version is already up to date:', version);
  process.exit(0);
}

// バージョンを更新
packageJson.version = version;

// package.jsonに書き戻し（フォーマット保持）
fs.writeFileSync(packageJsonPath, JSON.stringify(packageJson, null, 2) + '\n', 'utf8');

console.log('✅ Version updated successfully:');
console.log('   Old version:', oldVersion);
console.log('   New version:', version);
console.log('   Source:', versionFile);
console.log('   Target:', packageJsonPath);
