# OBS Auto Stop

OBS Studio の録画を、次の条件で自動停止するための外部プラグインです。

- **タイマーによって自動で録画終了**（最大録画時間）
- **画面が一定時間静止したら自動で録画終了**（映像の静止検出）
- **メディアソース再生終了で自動録画終了**
- **一定時間無音なら自動で録画終了**

Status: Early development / experimental

このプロジェクトは OBS Studio 本体を改造しません。OBS の公開 Plugin API を使う外部プラグインです。

まだ実運用向けではありません。挙動は環境によって変わり得るため、重要な録画では十分に確認してから使ってください。

English: External OBS Studio plugin that can auto-stop recording on a max timer and/or when the video stays still for a while. Early / experimental.

## 配布（ダウンロード）

最新の配布 zip は GitHub Releases にあります。

- [v0.1.0-pre-region](https://github.com/yuuki-ama1015/obs-auto-stop/releases/tag/v0.1.0-pre-region)
  - Windows: `obs-auto-stop-v0.1.0-pre-region-windows.zip`
  - Linux: `obs-auto-stop-v0.1.0-pre-region-linux.zip`

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
- メディアソース再生終了による自動停止（ループOFFのみ）
- 無音判定による自動停止（プログラム音声ピーク dBFS）

## まだ未実装 / 将来検討

- 黒画面検出
- 終了前の待機時間
- 静止 + 無音などの **かつ (AND)** 組み合わせ判定（現状はすべて OR）
- インストーラー / 自動アップデート

## Dock UI

**条件の組み合わせ:** ON にした停止条件はすべて **「または」(OR)** です。複数チェックしていても、どれか 1 つでも満たせば録画を終了します（「かつ」ではありません）。

フロート表示中のドックは **「タスクバーに最小化」**（またはタイトルバーの最小化）でタスクバーへ収納できます。ドック表示のままでは最小化できません。

Dock 名: **OBS Auto Stop**

| 表示 | 意味 |
| --- | --- |
| メディアソースの再生が終わったら自動で録画終了 | ループOFFのメディア終了で停止（ドック最上段） |
| タイマーによって自動で録画終了 | 録画タイマーによる自動停止の ON/OFF |
| 録画タイマー | 最大録画時間（分）。0 で無効 |
| 画面が一定時間静止したら自動で録画終了 | 静止検出の ON/OFF |
| 静止と判断する時間 | 静止が続いたら停止するまでの秒数 |
| 静止判定の感度 | 動きとみなすしきい値（%）。**セッション限定**（OBS終了で既定 0.5% に戻る） |
| 最低録画時間 | この時間までは静止停止しない（分） |
| └ 監視領域を限定する（静止検出のオプション） | 静止検出 ON のときだけ有効。領域を選択でプレビュー上をドラッグ |
| 一定時間無音なら自動で録画終了 | 無音判定の ON/OFF |
| 無音と判断する時間 | 無音が続いたら停止するまでの秒数 |
| 無音しきい値 | これ以下のピークを無音とみなす（dBFS） |
| プラグインステータス | 待機中 / 録画中 など |
| 経過時間 | 録画経過 / 録画タイマー上限 |
| 静止時間 | 現在の静止継続 / 判定までの時間 |
| メディア終了 | 監視中 / 検知 |
| 無音時間 | 現在の無音継続 / 判定までの時間 |
| タスクバーに最小化 | フロート中のドックをタスクバーへ収納 |

## インストール（Windows / OBS 32+）

OBS 32 以降の推奨パスは **ProgramData** です（`%APPDATA%\obs-studio\plugins` は読まれません）。

1. OBS を完全終了する
2. 配布 zip を展開し、次の場所に配置する（フォルダ名と DLL 名を一致させる）:

```
C:\ProgramData\obs-studio\plugins\obs-auto-stop\
  bin\64bit\obs-auto-stop.dll
  data\locale\en-US.ini
  data\locale\ja-JP.ini
```

3. OBS を起動し、メニュー「ドック」に **OBS Auto Stop** が出るか確認
4. 出ない場合はヘルプ → ログファイルを確認し、`obs-auto-stop` / `OBS Auto Stop` の行を見る

旧パス（動作しない）: `%APPDATA%\obs-studio\plugins\obs-auto-stop`

## インストール（Linux）

1. OBS を終了する
2. 配布 zip を展開し、次の場所に配置する:

```
~/.config/obs-studio/plugins/obs-auto-stop/
  bin/64bit/obs-auto-stop.so
  data/locale/en-US.ini
  data/locale/ja-JP.ini
```

3. OBS を起動し、ドックに **OBS Auto Stop** が出るか確認

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
- テスト用環境変数（設定されている場合、プロファイル保存値より優先されます）:
  - `OBS_AUTOSTOP_MAX_SECONDS`
  - `OBS_AUTOSTOP_MOTION`
  - `OBS_AUTOSTOP_INACTIVITY_SECONDS`
  - `OBS_AUTOSTOP_MIN_RECORDING_SECONDS`
- ロケール: `data/locale/en-US.ini`, `data/locale/ja-JP.ini`（OBS の言語設定に応じて読み込まれます）。Dock UI の一部ラベルは C++ 側で日本語ハードコードです。

Linux 環境では、録画タイマー自動停止と画面静止による自動停止の両方を実録画で確認済みです。出力ファイルの生成可否は表示環境に依存します。

## LICENSE

本プロジェクトは **GPL-2.0**（GNU General Public License v2.0）です。全文はリポジトリ直下の [`LICENSE`](LICENSE) を参照してください。

English: This project is licensed under **GPL-2.0**. See [`LICENSE`](LICENSE) for the full text.

OBS Studio の Plugin API / Qt を利用するプラグインとして、OBS 本体と同じく GPL-2.0 を採用しています。
