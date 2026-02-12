import eslint from '@eslint/js';
import tseslint from 'typescript-eslint';
import eslintConfigPrettier from 'eslint-config-prettier';

export default tseslint.config(
    eslint.configs.recommended,
    ...tseslint.configs.recommended,
    eslintConfigPrettier,
    {
        files: ['scripts/**/*.ts'],
        languageOptions: {
            parserOptions: {
                projectService: true,
                tsconfigRootDir: import.meta.dirname,
            },
        },
        rules: {
            // 未使用変数は警告（_プレフィックスは除外）
            '@typescript-eslint/no-unused-vars': [
                'warn',
                { argsIgnorePattern: '^_', varsIgnorePattern: '^_' },
            ],
            // console.logはスクリプトなので許可
            'no-console': 'off',
            // process.exitはCLIスクリプトなので許可
            'no-process-exit': 'off',
        },
    },
    {
        // lint対象外
        ignores: ['out/**', 'node_modules/**', '*.json', '*.yaml', '*.yml'],
    },
);
