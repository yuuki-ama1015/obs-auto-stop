# 引き継ぎメモ（obs-auto-stop）

最終更新: 2026-09-23（Asia/Tokyo）  
リポジトリ: https://github.com/yuuki-ama1015/obs-auto-stop （**公開** / `main`）  
直近 tip（このメモ作成時点）: `a28ff44` — `fix(dock): tighten spacing under combine-mode row`

このファイルは、別エージェント／別担当が続きをやるときの入口です。まずここを読んでから README とログを見てください。

---

## 1. 何のプロジェクトか

OBS Studio 外部プラグイン。録画を次の条件で自動停止する。

| 条件 | 概要 |
| --- | --- |
| タイマー | 最大録画時間（分）。0 で無効 |
| 画面静止 | 動きスコア（changed-pixel %）で静止判定。領域限定あり |
| メディア終了 | 番組シーン内の非ループ `ffmpeg_source` / `vlc_source` 等の `media_ended` |
| 無音 | プログラム音声ピーク dBFS |

**条件の組み合わせ（ドック上部）**

- **または (OR):** ON の静止・無音・メディア終了のどれか1つで停止
- **かつ (AND):** ON の静止・無音・メディア終了がすべて満たされたら停止
- **タイマーは常に単独ハードストップ**（AND/OR の対象外）

UI 文言の例:

- タイマーによって自動で録画終了
- 画面が一定時間静止したら自動で録画終了
- メディアソースの再生が終わったら自動で録画終了
- 一定時間無音なら自動で録画終了

その他ドック機能:

- 「タスクバーに最小化」— **フロート表示のときだけ**（ドック収納状態では不可）
- フォルダアイコン — 録画保存先フォルダを開く
- 静止感度は **セッション限定**（OBS 終了で既定 0.5% に戻る）
- 監視領域は静止検出のオプション（静止 ON のときだけ有効）

ライセンス: **GPL-2.0**

---

## 2. 実行環境（ユーザー PC）

- OS: Windows（DESKTOP-559MM5B、ユーザー `yuuki`）
- OBS: **32.2.2**（`C:\Program Files\obs-studio\`）
- **インストール先（必須）:**

```
C:\ProgramData\obs-studio\plugins\obs-auto-stop\
  bin\64bit\obs-auto-stop.dll
  data\locale\en-US.ini
  data\locale\ja-JP.ini
```

**注意:** `%APPDATA%\obs-studio\plugins` は OBS 32 では読まれない。ここに置くとドックが出ない。

- ローカルに CMake / VS / OBS ソースは揃っていない想定。Windows DLL は **GitHub Actions**（`.github/workflows/build-windows.yml`）で作る。
- ワークフロー入力 `plugin_ref` のデフォルトは **`main`**（昔は `v0.1.0-pre-region` タグだったので、古いデフォルトのままビルドしないこと）。

```bash
gh workflow run build-windows.yml --repo yuuki-ama1015/obs-auto-stop --ref main \
  -f plugin_ref=main -f attach_release=
```

成果物 zip を展開して ProgramData に上書き → OBS を完全終了してから再起動。

---

## 3. 実機検証の到達点

| 項目 | 結果 |
| --- | --- |
| A タイマー | PASS |
| B 静止 | PASS（誤検知対策: motion score を changed-pixel % に変更、既定感度 0.5%） |
| C 領域限定 | PASS |
| メディア終了 / 無音 | 実装済み。DLL にも入っている。実機確認は途中で他エージェントに引き継いだ経緯あり — 最新 DLL で再確認推奨 |
| AND/OR UI・保存先アイコン・余白詰め | main にマージ済み、PC の ProgramData にも入れた（2026-09-20） |

領域の意味（重要）:

- ウィンドウキャプチャ時、静止判定の % 領域は **出力キャンバス全体（ウィンドウ枠）基準**
- 領域 OFF = そのウィンドウ出力全体が対象
- 領域 ON = その中の部分だけ（例: ゲーム画面だけ）

---

## 4. 主要コード地図

| パス | 役割 |
| --- | --- |
| `src/plugin-main.cpp` | load/unload、tick、フロントエンドイベント。AND/OR 判定はここ |
| `src/auto-stop-dock.cpp` / `.hpp` | ドック UI（日本語ハードコード多め） |
| `src/motion-detector.*` | 静止検出 |
| `src/media-end-watcher.*` | メディア終了（MSVC では `struct calldata` 必須 — PR #6） |
| `src/silence-detector.*` | 無音検出 |
| `src/recording-monitor.*` | 録画経過・最大時間 |
| `src/stop-controller.*` | 停止要求 + `StopCombineMode` |
| `src/region-select-dialog.*` | 領域ドラッグ選択 |
| `data/locale/ja-JP.ini`, `en-US.ini` | ロケール（ドック文言の一部は C++ 直書き） |
| `.github/workflows/build-windows.yml` | Windows CI（OBS 32.0.2 + deps でビルド） |

---

## 5. 配布・リリース

- 公開リリース: [v0.1.0-pre-region](https://github.com/yuuki-ama1015/obs-auto-stop/releases/tag/v0.1.0-pre-region)  
  - 元は領域機能前の `ed48a22` 起点。その後 Windows/Linux zip を差し替えたことがあるが、**現行 main の最新機能一式とは一致していない可能性が高い**
- **未対応（推奨）:** main tip 向けの新しいタグ／Release（例: `v0.2.0`）を切って、最新 Windows/Linux zip を付け直す

---

## 6. 既知の落とし穴

1. **AppData に入れるとロードされない** → 必ず ProgramData
2. **Windows DLL のエクスポート** — `obs_module_load` 等が必要（過去に 0 exports でロード失敗した）
3. **MediaEndWatcher の `calldata_t`** — MSVC では前方宣言だけだとビルド失敗。`struct calldata` を使う（PR #6）
4. **CI の `plugin_ref`** — 古いタグを指定するとラベル修正などが入らない
5. **Cloud Agents** — このユーザー環境ではプラン未対応のことがあった。コード変更は `gh` API / PR 経由で実施してきた
6. **OBS ログ** — `%APPDATA%\obs-studio\logs\` で `OBS Auto Stop` / `obs-auto-stop` を確認。成功時は `plugin loaded` と `dock registered`

---

## 7. マージ済み PR（参考）

1. LICENSE / ja-JP / env override  
2. raw video callback unregister + diagnostics  
3. CI default `plugin_ref=main`  
4. motion score（誤静止対策）  
5. media end + silence  
6. MediaEndWatcher calldata MSVC fix  
7. UI: media-end 最上段、領域を静止の下へ  
8. 静止感度は終了時リセット  
9. OR 明示 + フロートドックのタスクバー最小化  
10. AND モード + 録画保存先フォルダアイコン  

その後 main 直コミット: ツールチップ文字列修正、combine 行下の余白詰め（`a28ff44`）

---

## 8. 次にやるとよいこと（候補）

優先度はユーザー判断。候補のみ。

1. **最新 main の Release を切る**（配布 zip を現行機能に合わせる）
2. **メディア終了 / 無音 / AND の実機再確認**（チェックリストをログ付きで）
3. README の「将来検討」: 黒画面、終了前待機、インストーラなど
4. ドック文言の locale 化（いま C++ ハードコードが多い）
5. OBS 32.2.2 向け SDK でビルドする CI 更新（現状 32.0.2 ベース）

---

## 9. 作業エージェントメモ

- ユーザー: ほんはくすい（Asia/Tokyo）
- 関連エージェント: `obs auto`（本チャット）、`その他`（一時引き継ぎあり）、旧 `obs` は途中で無応答になった経緯あり
- Windows machineId（Grok Bot ローカル実行）: `fb636c38-9b95-4450-b72d-6dca531fb840`

---

## 10. すぐ動かす最短手順

```bash
# 1) Windows DLL を main からビルド
gh workflow run build-windows.yml --repo yuuki-ama1015/obs-auto-stop --ref main \
  -f plugin_ref=main -f attach_release=

# 2) artifact zip を取得し、ProgramData の上記パスへ上書き

# 3) OBS を完全終了 → 起動

# 4) ドック「OBS Auto Stop」を確認。ログに plugin loaded / dock registered
```

詳細なユーザー向け説明は `README.md` を参照。
