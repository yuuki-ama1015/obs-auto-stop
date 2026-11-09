# AGENTS.md — OBS Auto Stop

OBS Studio の録画をタイマー／画面静止／メディア終了／無音などで自動停止する外部プラグイン。OBS本体は改造しない。

## 概要
- C++ / CMake（`CMakeLists.txt`、`src/`）
- 配布: GitHub Releases（例: v0.2.0）。進捗: `PROGRESS.md`。検証証拠: `docs/V0.2.0-VALIDATION.md`
- インストール先は **ProgramData**（OBS 32 は `%APPDATA%\obs-studio\plugins` を読まない）:
  `C:\ProgramData\obs-studio\plugins\obs-auto-stop\bin\64bit\obs-auto-stop.dll` と locale
- Windows DLL は主に GitHub Actions（`.github/workflows/build-windows.yml`）。`plugin_ref` は通常 `main`（古いタグ既定に注意）
- 成功時ログ目安: `plugin loaded` / `dock registered`（`%APPDATA%\obs-studio\logs\`）

## 落とし穴（要約）
- AppData 配置だとドックが出ない
- Windows で `obs_module_load` 等のエクスポート欠落に注意（過去に0 exportsで失敗）
- `calldata_t` は MSVC では `struct calldata` が必要だった経緯あり
- 静止判定の領域%は出力キャンバス（ウィンドウ枠）基準

## GitHubで開発を行う場合のルール
GitHubのリポジトリで開発を行う場合は、以下に従うこと。

作業を始める前に、リポジトリのルートにある PROGRESS.md を読むこと。なければルートに作成すること。
作業の最後に、PROGRESS.md を以下の構成で更新し、コミット・プッシュすること。

# 進捗と方針
## 今後の実装方針
（決まった設計・技術選定・やり方。変わったら書き換える）
## 完了したこと
（日付と内容を1行ずつ追記する）
## 次にやること
（優先順に並べる。終わったら「完了したこと」へ移す）
## 未解決の問題・メモ
（バグ、迷っている点、人間に確認したいこと）
