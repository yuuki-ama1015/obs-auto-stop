# OBS Auto Stop

OBS Studio の録画を、次の条件で自動停止するための外部プラグインです。

- 録画タイマー（最大録画時間）
- 画面静止で録画終了（映像の静止検出）

Status: Early development / experimental

このプロジェクトは OBS Studio 本体を改造しません。OBS の公開 Plugin API を使う外部プラグインです。

まだ実運用向けではありません。挙動は環境によって変わり得るため、重要な録画では十分に確認してから使ってください。

## 実装済み

- プラグインの load / unload
- 録画タイマーによる自動停止
- 画面静止検出による自動停止
- OBS Dock UI（設定と状態表示）
- 最低録画時間（静止検出の誤検知防止）
- 静止判定の感度
- 手動停止時のタイマー / 静止カウンターリセット
- プロファイルへの設定保存
- 監視領域の限定（ドラッグ選択）

## まだ未実装 / 将来検討

- 音声レベル（無音）判定
- 黒画面検出
- 終了前の待機時間
- 動画終了イベントの利用
- 静止 + 無音の組み合わせ判定
- インストーラー / 自動アップデート

## Dock UI

Dock 名: **OBS Auto Stop**

| ラベル | 意味 |
| --- | --- |
| 録画の自動停止 | 全体の ON/OFF |
| 録画タイマー | 最大録画時間（分）。0 で無効 |
| 画面静止で録画終了 | 静止検出の ON/OFF |
| 静止と判断する時間 | 静止が続いたら停止するまでの秒数 |
| 静止判定の感度 | 動きとみなすしきい値（%） |
| 最低録画時間 | この時間までは静止停止しない（分） |
| 監視領域を限定する | ON で監視範囲を限定。ボタン「領域を選択」でプレビュー上をドラッグして指定 |
| プラグインステータス | 待機中 / 録画中 など |
| 経過時間 | 録画経過 / 録画タイマー上限 |
| 静止時間 | 現在の静止継続 / 判定までの時間 |

## ビルド

OBS SDK は同梱しません。CMake の設定値または環境変数でパスを渡します。

```powershell
cmake -S . -B build `
  -DOBS_SOURCE_DIR=C:/path/to/obs-studio `
  -DOBS_BUILD_DIR=C:/path/to/obs-studio/build
cmake --build build --config RelWithDebInfo
```

OBS SDK が見つからない場合、CMake は警告を出してプラグイン target をスキップします。

Qt Widgets が必要です（Dock UI 用）。

## 開発・検証メモ

- プラグイン名: `obs-auto-stop`
- ログ接頭辞: `OBS Auto Stop`
- テスト用環境変数:
  - `OBS_AUTOSTOP_MAX_SECONDS`
  - `OBS_AUTOSTOP_MOTION`
  - `OBS_AUTOSTOP_INACTIVITY_SECONDS`
  - `OBS_AUTOSTOP_MIN_RECORDING_SECONDS`

Linux 環境では、録画タイマー自動停止と画面静止による自動停止の両方を実録画で確認済みです。出力ファイルの生成可否は表示環境に依存します。

## LICENSE

`LICENSE` ファイルはまだありません。OBS Studio や今後追加する依存ライブラリとの互換性を確認してから選定します。
