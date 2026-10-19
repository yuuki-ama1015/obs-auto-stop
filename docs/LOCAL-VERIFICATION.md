# Windows 実機検証の引き継ぎ

確認日: 2026-10-19 (Asia/Tokyo)

## 導入状態

- 対象: https://github.com/yuuki-ama1015/obs-auto-stop
- 取得した main: `d7cff18f9825453b76efb180fea605ddca322b8a`
- OBS: `C:\Program Files\obs-studio\bin\64bit\obs64.exe`、ファイルバージョン 32.2.2
- 導入先: `C:\ProgramData\obs-studio\plugins\obs-auto-stop`
- [Actions run 39199737037](https://github.com/yuuki-ama1015/obs-auto-stop/actions/runs/39199737037) は成功。artifact `obs-auto-stop-windows` の zip と、導入済み DLL・両ロケール・README-DIST.txt の SHA256 がすべて一致した。
- DLL SHA256: `D59410545137760C233E9CEC38FA7BFF25EF859F50C2EE57185187E644E174D2`
- 最新 main の push に対する run 39199733836 も成功。
- 確認時 OBS は未起動。最新ログは 2026-10-15 で、今回の DLL 更新前のため今回の動作確認の証拠には使えない。
- 公開 Release は `v0.1.0-pre-region` のみ。今回 Release 作成・GitHub push はしていない。
- 現在の CMake バージョンと artifact 名はまだ `0.1.0`。新しい Release の際はバージョン・タグ・zip 名をそろえる。

## 最新 DLL で行う検証

下記はすべて **未実施**。コードやビルド成功だけで実機 PASS としない。

| 項目 | 確認する結果 |
| --- | --- |
| ドック最小化 | 収納時は案内。フロート時は最小化でき、タスクバーから復帰できる |
| 録画途中の ON | 静止・無音をそれぞれ ON にした時点からカウントする |
| 一時停止・再開 | 一時停止中は自動停止しない。再開時は静止・無音を 0 から数え直す |
| 狭い監視領域 | 領域内の動きと静止を検出する。領域外の動きに影響されない |
| NVENC 負荷 | 同じ映像・録画設定で静止検出 ON/OFF と旧版を比較し、CPU/GPU 使用率・描画/エンコード遅延を記録する。比較前は負荷低下を実測済みとしない |
| グループ・シーン | グループ内メディア終了で停止。シーン切替後は新しいシーンのメディアを監視する |
| VLC ループ | ON では終了停止せず、OFF では再生終了時に停止する |
| プロファイル | 別のプロファイルへ切り替えると設定がそのプロファイルの値になる |
| 録画中の終了 | 録画中に OBS を終了しても固まらず、終了ログを確認できる |

既存の設定・シーンを保全し、試験録画を識別できる形で実施する。結果には条件、ログ、録画ファイルを対応付ける。
実機確認で問題がなければ `v0.2.0` の Windows/Linux zip 付き Release を次の候補とする。
