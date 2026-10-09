# FeliCa dump for Raspberry Pi Pico

RC-S660/S を UART で接続し、カードの System Code、Area、Service Code と、認証なしで読める16バイトのブロックを USB シリアルへ `printf` で出力します。出力形式は [pasmo.txt](pasmo.txt) を参考にしています。カードのデータを書き換えるコマンドは送信しません。

## 配線

| Raspberry Pi Pico | RC-S660/S |
| --- | --- |
| GPIO16 (UART0 TX, pin 21) | RXD (pin 3) |
| GPIO17 (UART0 RX, pin 22) | TXD (pin 2) |
| 3V3(OUT) | VDD (pin 1) |
| GND | GND (pin 4) |

RC-S660/S 側 UART は 115200 bps です。表示は Pico の USB CDC シリアル経由です。USB シリアル表示は UTF-8 に設定してください。`-DRCS660S_TX_PIN=... -DRCS660S_RX_PIN=...` で GPIO を変更できますが、UART0 に対応するピンの組み合わせを指定してください。

## ビルド

Pico SDK、CMake、Ninja、Arm GNU Toolchain が必要です。このフォルダで実行します。

```sh
cmake -S . -B build -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_BOARD=pico
cmake --build build
```

Pico 2 では `-DPICO_BOARD=pico2` にします。生成された `build/felica_dump.uf2` を BOOTSEL モードの Pico に書き込みます。USB シリアルを開いてカードをかざすと、一度ダンプします。再度読む場合はカードを離してからかざしてください。

## コードの探索と出力

最初に任意 System Code (`FFFF`) で Polling し、`Request System Code` (`0C`) によってカード上の System Code を列挙します。各 System に対して `Search Service Code` (`0A`) を繰り返し、Area と Service Code を得ます。サービスの属性は Service Code の下位ビットから表示します。認証なしで読める Service には `Read Without Encryption` (`06`) を送り、失敗するか [config.h](config.h) の上限に達するまでブロックを出力します。

一部カードはコード列挙コマンドに対応しません。任意の候補コードを調べる場合は [config.h](config.h) の `EXTRA_SYSTEM_CODES` と `EXTRA_SERVICE_CODES` に追加して再ビルドしてください。コードは `0x0003` のように通常の表示順で記入します。追加 Service Code の存在は `Request Service` (`02`) で確認します。設定された候補以外の未列挙コードまで総当たりはしません。

出力例:

```text
# Number of System = 2

# System code: 0003
# IDm: 01 01 03 12 55 24 C9 2F
# Number of area = 10
# Area: 0000 - FFFE
# Number of service code = 58
# Service code = 090F : Cyclic Access Read only
090F:0000 00 00 00 80 00 00 00 00 00 00 00 00 00 00 00 00
```

カード種別は既知の Service Code と System Code から `候補` として表示します。同じコードを複数の製品が使用する可能性があるため、確定判定ではありません。認証が必要なブロックは出力できません。列挙や読み出しの対応状況はカードによって異なります。

RC-S660/S の通信ドライバは [toyowata/felica-reader-rcs660s-pico](https://github.com/toyowata/felica-reader-rcs660s-pico/) を基にしています。元コードのライセンスは [LICENSE](LICENSE) を参照してください。
