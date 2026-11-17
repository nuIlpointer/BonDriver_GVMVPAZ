# BonDriver_GVMVPAZ_T (v1.0.0)

I-O DATA **GV-MVP/AZ**（ISDB-T USB チューナー / `MB86M01`）を **Mirakurun**・**KonomiTV**・TVTest 等の x64 BonDriver 対応アプリケーションから直接利用できるようにした [`BonDriver_BDA`](https://github.com/radi-sh/BonDriver_BDA) ベースのカスタム x64 ビルドです。

> 本リポジトリには I-O DATA 純正バイナリ・ドライバ（`gvmvpaz_x64.sys` や mAgicTV 関連ファイル）は一切含まれていません。あらかじめ純正ドライバを導入済みの Windows 環境でご利用ください。

---

## 初版リリース (`v1.0.0`) の主な特徴・修正内容

1. **ハードウェアスルー初期化 & ストリーム復号**
   - `KSMETHODSETID_GVMVPAZ_Auth`（`5250E9E6`）認証と `KSMETHODSETID_GVMVPAZ_Through`（`74395BA9`）セッション鍵取得・AES-128 CTR ベースの TS ペイロード復号に対応。
   - 選局時に `CD0B5B68` / `048DC5D8`（PID テーブル）を設定し、選局後の `BdaTableSection` 取得から **PAT / PMT を動的再構成**して出力。
2. **120秒ごとのチューナー切断・無信号化を解消（認証キープアライブ）**
   - 純正ドライバ `gvmvpaz_x64.sys` が `5250E9E6` 認証を **120秒（`0x78`）で失効**させ `TunerUnLocked` / `Input stream error` を発生させる仕様に対応し、受信中は `COMProcThread`（同一 COM STA スレッド）から **30秒ごとに認証を自動更新**。
3. **44.1秒時点の 188B TS 同期崩れを修正**
   - GV-MVP/AZ が出力する 192 バイト TTS パケット（4B 27MHz タイムスタンプ + 188B TS）の判定に `pbData[0] != 0x47` が含まれていたため開始約 44.1 秒で誤判定していた問題を解消し、先頭〜最大 4 パケットのオフセット `+4`（`0x47`）一致のみで安定除去するように修正。

---

## ディレクトリ構成

```text
.
├── release/                         # 初版リリース v1.0.0 配布用バイナリ・ZIP
│   ├── BonDriver_GVMVPAZ_T.dll      # Release x64 ビルド済み BonDriver
│   ├── BonDriver_GVMVPAZ_T.ini      # そのまま使える設定ファイル (DebugLog=NO)
│   └── BonDriver_GVMVPAZ_T_v1.0.0_x64.zip
├── configs/                         # サンプル設定ファイル集
│   ├── BonDriver_GVMVPAZ_T.ini
│   └── mirakurun/
│       ├── tuners.sample.yml
│       └── channels.sample.yml
├── src/                             # ソースコード一式 (BonDriver_BDA + 必要ライブラリ)
├── build.bat                        # x64 Release ビルドスクリプト
├── .gitignore
└── README.md
```

---

## 使い方（Mirakurun + KonomiTV 例）

### 1. BonDriver の配置

`release/BonDriver_GVMVPAZ_T.dll` と `release/BonDriver_GVMVPAZ_T.ini` を同じフォルダ（例: `C:\DTV\BonDriver\`）へ配置してください。  
※ x64 アプリ向けビルドのため、呼び出し元（例: `BonRecTest.exe`）も **x64 版** を使用してください。

### 2. Mirakurun 設定例

`%USERPROFILE%\.Mirakurun\tuners.yml`（[configs/mirakurun/tuners.sample.yml](configs/mirakurun/tuners.sample.yml)）:

```yaml
- name: GV-MVP-AZ
  types:
    - GR
  command: C:\DTV\BonRecTest\x64\BonRecTest.exe --driver C:\DTV\BonDriver\BonDriver_GVMVPAZ_T.dll --space 0 --channel <channel> --output -
  decoder: ~
  isDisabled: false
```

`%USERPROFILE%\.Mirakurun\channels.yml`（[configs/mirakurun/channels.sample.yml](configs/mirakurun/channels.sample.yml)）にはお住まいの地域の `ChannelSettingsAuto="UHF"` チャンネル番号（物理 ch - 13 等、BonDriver 上の空間 0 チャンネル番号）に合わせて設定してください。

---

## ソースからのビルド方法

- **必要環境**: Visual Studio 2022（または Build Tools、C++ デスクトップ開発 / `v143` / Windows 10 SDK）
- リポジトリ直下で以下を実行すると `src\x64\Release\BonDriver_BDA.dll` が生成されます。

```bat
build.bat
```

利用の際は出力された `BonDriver_BDA.dll` を `BonDriver_GVMVPAZ_T.dll` にリネームし、対応する `.ini` と同階層に置いてください。

---

## 検証済み環境

- **OS**: Windows 11 (x64)
- **チューナー / ドライバ**: I-O DATA GV-MVP/AZ（純正ドライバ導入済み環境）
- **配信スタック**: `BonDriver_GVMVPAZ_T.dll` (x64) → `BonRecTest` (x64) → `Mirakurun 3.9.0-rc.4` → `KonomiTV 0.14.1` (`QSVEncC`)
- **長時間検証**: BonDriver 直接ソーク（155秒 / `bad_sync=0` / `scram_nz=0`）および KonomiTV 経由連続ライブ配信（160秒連続 / エラーゼロ）を確認済み。

---

## ライセンス・謝辞

- 本プロジェクトは [`radi-sh/BonDriver_BDA`](https://github.com/radi-sh/BonDriver_BDA) を基に改変しています。原本のドキュメントおよび免責事項は [`src/doc/Readme-BonDriver_BDA.md`](src/doc/Readme-BonDriver_BDA.md) を参照してください。
- サードパーティ依存ライブラリとして [`muparser`](https://github.com/beltoforion/muparser) および Microsoft [`Windows-classic-samples`](https://github.com/microsoft/Windows-classic-samples) DirectShow BaseClasses（`3rdParties/` 配下）を利用しています。
- 本ソフトウェアの利用によって生じた損害等について、作者・関係者は一切の責任を負いません。
