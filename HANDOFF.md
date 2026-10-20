# 引き継ぎメモ（obs-auto-stop）

## 2026-10-19〜21 の追加作業

- 最小化修正・バージョン0.2.0・Windows/Linux zip作成を `fd4005e`、ドックのスクロール対応を `65bddcc` で反映。最終CI `39258171293` 成功、PCにも最終ビルドを導入済み。
- 旧版 `ad3bf56` と同条件の NVENC 実録画で比較し、今回の素材では CPU/GPU/描画時間が低下。メディア監視1件＋フロートドック表示中の録画終了もハングせず、リーク報告0。
- 2026-10-21 に VLC 3.0.24 を導入し、最終候補でループ ON の録画継続、OFF のメディア終了による自動停止、録画中の OBS 終了を確認。3録画の全編デコード成功、終了時リーク報告0。最終スクロール・重なり解消・収納中案内も確認。タスクバーのクリック復帰はユーザーの実操作による回答で確認した。
- [Release v0.2.0](https://github.com/yuuki-ama1015/obs-auto-stop/releases/tag/v0.2.0) を公開済み。Windows/Linux zip と SHA256 を添付。Linux はビルド確認のみ。検証で変更した設定6ファイルは当日の開始直前へ戻し、SHA256 一致を確認。
- 最新の証拠と未実施項目は [docs/V0.2.0-VALIDATION.md](docs/V0.2.0-VALIDATION.md)。以下の旧メモより、この追加作業と検証文書を優先する。

最終更新: 2026-10-21（Asia/Tokyo）
リポジトリ: https://github.com/yuuki-ama1015/obs-auto-stop （**公開** / `main`）  
直近: 2026-10-19 にコードレビュー（REVIEW）指摘を PR #11〜#15 で一括対応（下の「7. マージ済み PR」参照）

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

- CI は main への push と PR でも自動で走る（Linux `-Werror` ジョブ + Windows）。OBS SDK（obs-deps と obs.lib / obs-frontend-api.lib / obsconfig.h）は `actions/cache` でキャッシュされ、2 回目以降は OBS のビルドを省略する。
- CMakeLists をワークフロー内で書き換える処理は廃止。OBS のパスは `-DOBS_*` キャッシュ変数で渡す。エクスポートは obs-module.h の `MODULE_EXPORT` で付き、CI で dumpbin により確認する。
- zip 名は `obs-auto-stop-v<CMake の version>-windows-x64.zip`。

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
| `src/plugin-main.cpp` | load/unload、UI スレッドの 250ms QTimer で条件評価、フロントエンドイベント（一時停止・プロファイル・シーン切替・終了処理）。AND/OR 判定はここ |
| `src/auto-stop-dock.cpp` / `.hpp` | ドック UI。文言は `ObsText("Dock.*")`（`src/plugin-text.hpp`）経由でロケールから |
| `src/motion-detector.*` | 静止検出（main rendered callback で GPU 縮小 256x144 → 1 フレーム遅れのステージング読み戻し。フル解像度の raw video callback は使わない） |
| `src/media-end-watcher.*` | メディア終了（入れ子シーン・グループ対応、番組シーン切替で張り直し、`loop`/`looping`。MSVC では `struct calldata` 必須 — PR #6） |
| `src/silence-detector.*` | 無音検出（トラック 1、ロックフリー） |
| `src/recording-monitor.*` | 録画経過・最大時間（一時停止中は数えない） |
| `src/stop-controller.*` | 停止要求 + `StopCombineMode` |
| `src/region-select-dialog.*` | 領域ドラッグ選択 |
| `data/locale/ja-JP.ini`, `en-US.ini` | ロケール（ドック・領域ダイアログの全文言。DLL と一緒に配置必須） |
| `.github/workflows/build-windows.yml` | CI（push/PR/手動。Linux -Werror + Windows x64、OBS 32.0.2 + deps、SDK キャッシュ） |

---

## 5. 配布・リリース

- 公開リリース: [v0.2.0](https://github.com/yuuki-ama1015/obs-auto-stop/releases/tag/v0.2.0)。ビルド元は `65bddcc`、その後の main は検証記録の更新。
- Windows/Linux x64 zip と SHA256SUMS を同梱。実機確認範囲は検証文書を参照。

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

レビュー対応（2026-10-19）:

11. #11 一時停止の扱い（経過時間が一時停止で 0 に戻るバグ修正、一時停止中は停止しない）、録画中に ON にした条件を即監視、評価を UI スレッドの QTimer へ、最小化はフロート時のみ
12. #12 静止検出を GPU 縮小 + 小さい読み戻しに（フル解像度読み戻しを廃止）
13. #13 メディア終了（グループ・シーン追従・VLC loop）、プロファイル切替で再読込、終了処理、AND 警告、保存デバウンス、ログ整理
14. #14 CMake/CI（パッケージ優先、Release 既定、サイズ削減フラグ、/utf-8、push/PR 実行、Linux -Werror、キャッシュ、ProgramData 表記）
15. #15 ドック文言のロケール化（ja-JP は従来と同一表示）

---

## 8. 次にやるとよいこと（候補）

優先度はユーザー判断。候補のみ。

1. Linux 実機での録画確認（Windows の今回の依頼範囲は完了）。
2. AND 条件を使った追加の長時間運用確認。
3. README の「将来検討」: 黒画面、終了前待機、インストーラなど
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
