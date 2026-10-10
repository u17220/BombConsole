# BombConsole ドキュメント

ターミナル上で多人数が同時に対戦するボンバーマン風ゲーム。サーバ(C++)1台に、クライアント(Python / curses)が複数接続する。

- 改善方針(現状の精査と段階的な計画): [`improvement-plan.md`](improvement-plan.md)
- 実装の進捗と確認結果: [`progress.md`](progress.md)
- 図(v1 時点のもの。構成の概観として参照): [`img/`](img/)

## 1. 動かし方

### 必要なもの

| | サーバ | クライアント |
|---|---|---|
| Linux | `g++`, `make`(ncurses の開発パッケージがあればサーバ画面付き、なければ画面なしで自動ビルド) | Python 3(標準ライブラリのみ) |
| macOS | Xcode Command Line Tools(`g++` = clang, `make`)。ncurses は標準搭載 | 同左 |
| Windows | WSL を使う | WSL を使う(ネイティブ Python には `curses` がない。`pip install windows-curses` で動けば動く、という扱い) |

端末は UTF-8 で全角文字が表示できるもの。表示が崩れる場合はクライアントを `--ascii` で起動する。

### サーバ

```bash
make            # server/A_server ができる(ncurses の有無は makefile が自動判定)
make run        # ./server/A_server を起動
# または
./server/A_server --no-ui          # 画面なし。ssh 先や tmux に置いておける
./server/A_server --help           # 全オプション
```

- 起動すると待受ポートと、クライアントの起動コマンド例を表示する。
- クライアントが接続すると名前一覧に出る。**2 人以上が全員 `r`(準備完了)を押すと 5 秒後に開始**する。サーバ画面の `g` で強制開始、`q` で終了(`--no-ui` のときは標準入力に `g` / `q` の行を入れても同じ)。
- ラウンドが終わると結果を全員に送り、接続を閉じずにロビーに戻る。クライアントは `r` でもう 1 戦できる。
- パラメータは `--key value` と設定ファイル `server.conf`(`key = value`、1 行 1 項目、`#` 以降はコメント)で変えられる。引数が優先。例:

```
# server.conf
port = 10001
rounds = 3
fuse = 2.5
```

| オプション | 既定 | 内容 |
|---|---|---|
| `--port N` | 10001 | TCP ポート。0 で空きポート |
| `--no-ui` | | ncurses 画面を使わず標準出力にログを出す |
| `--name S` | ホスト名 | クライアントに見せるサーバ名 |
| `--conf FILE` | server.conf | 設定ファイル |
| `--seed N` | 0(ランダム) | 乱数の種(再現テスト用) |
| `--rounds N` | 1 | 1 セッションのラウンド数 |
| `--countdown N` | 5 | 全員準備完了から開始までの秒数 |
| `--min-players N` / `--max-players N` | 2 / 8 | 開始に必要な人数 / 1 ラウンドの上限(超えた分は観戦) |
| `--autostart N` | 0 | N 人そろったら準備完了を待たずに開始 |
| `--tick MS` | 50 | tick の長さ |
| `--fuse S` / `--fire S` / `--dying S` | 2.0 / 1.0 / 1.0 | 導火線 / 炎の持続 / 死亡演出(秒) |
| `--bombs N` / `--power N` / `--speed N` | 4 / 3 / 5 | 初期の同時ボム数 / 炎の長さ / 1 マスに要する tick 数 |
| `--time S` | 0(なし) | ラウンドの制限時間 |

### クライアント

```bash
python3 client/A_client.py <サーバのIPアドレス>      # 例: 192.168.1.10
python3 client/A_client.py                          # 省略時は 127.0.0.1
```

- `host:port` の形式、環境変数 `BOMBCONSOLE_SERVER=host:port` でも指定できる。
- 名前は `--name 名前` > 環境変数 `BOMBCONSOLE_NAME` > ログインユーザー名。同名はサーバ側で `(2)` を付けて区別する。
- `--ascii` で ASCII 文字だけで描く(1 マス 2 桁)。既定は全角文字(1 マス 3 桁)。
- 操作: 移動 = 矢印 / `WASD` / `hjkl`、ボム = `Space` / `z`、準備完了 = `r`、退出 = `q`(ラウンド中は 1 秒以内に 2 回)。

### ボット

```bash
python3 client/bot.py <サーバ> [--name NAME] [--rounds N]
```

curses を使わない簡単なクライアント。接続して準備完了を送り、危険マスから逃げ、敵が炎の届く範囲にいればボムを置く。1 人で試すときの相手や、ヘッドレスの自動対戦に使う。プロトコルの最小実装例でもある。

### 同一マシンで試す

```bash
make
./server/A_server                      # ターミナル 1
python3 client/A_client.py             # ターミナル 2(r を押す)
python3 client/bot.py                  # ターミナル 3(自動で準備完了)
```

### 自動テスト

```bash
make check
```

サーバを `--no-ui --port 0 --countdown 0 --time 60` で起動し、ボットを 2 体つないで 1 ラウンドを完走させ、両者が同じ結果を受け取ることを確認する(`tools/check.py`)。`make check ARGS="--soft 60"` のようにサーバの引数を足せる。

## 2. 構成

```
BombConsole/
├── makefile                 make / make run / make check
├── server/                  C++11 サーバ
│   ├── server.cpp           main: Session(ロビー → ラウンド → 結果 → ロビー)
│   ├── Game.{hpp,cpp}       1 ラウンドのルール(マップ、プレイヤー、ボム、炎、順位)
│   ├── ServerClient.{hpp,cpp}  TCP サーバ。受信スレッドが行とイベントをキューに積む
│   ├── Config.{hpp,cpp}     引数と server.conf
│   ├── Ui.{hpp,cpp}         サーバ画面(ncurses)。NO_CURSES / --no-ui では標準出力
│   ├── Json.hpp             JSON の書き出し(パーサは持たない)
│   ├── ncurses_probe.cpp    makefile の ncurses 検出用
│   └── makefile
├── client/
│   ├── A_client.py          curses クライアント(1 ファイル、標準ライブラリのみ)
│   ├── bot.py               ボット
│   └── legacy/              プロトコル v1 のサンプル(動かない。課題用の資料)
├── tools/check.py           自動テスト
└── docs/
```

### サーバのスレッド

| スレッド | 役割 |
|---|---|
| main | 50 ms ごとに「ネットワークイベントの処理 → `Game::tick` → 状態の全員配信 → 画面描画」を `sleep_until` で刻む。ゲームの状態を触るのはこのスレッドだけ |
| accept | `accept` して `Client` を作り、CONNECT イベントを積む |
| Client × N | `read` して改行で区切り、LINE イベントを積む。切断で DISCONNECT を積んで終了 |

ロビー中も同じ周期で回り、`READY` の集計とカウントダウンをする。サーバ画面の `getch` は非ブロッキングで、周期には関与しない。

## 3. 通信プロトコル v2

TCP、既定ポート 10001。**1 行 1 メッセージ**(改行区切り)。クライアント → サーバは空白区切りのテキスト、サーバ → クライアントは JSON。`nc サーバ 10001` でつないで `HELLO 1 名前` と打つだけで盤面が文字として流れてくる。

### クライアント → サーバ

| メッセージ | 意味 |
|---|---|
| `HELLO 1 <名前>` | 参加(プロトコル版 1)。名前は行末まで(空白可、最大 24 バイト、制御文字は除去) |
| `READY 1` / `READY 0` | 準備完了 / 取り消し(ロビーでのみ有効) |
| `IN <U\|D\|L\|R\|-> <0\|1>` | 入力。方向(なしは `-`)とボム設置。キーを押したときに送る。例: `IN R 0`、`IN - 1` |
| `BYE` | 退出 |

### サーバ → クライアント

| `t` | いつ | 内容 |
|---|---|---|
| `welcome` | HELLO の応答 | `{"t":"welcome","you":0,"proto":1,"name":"hiira","server":"host"}`。`you` が自分の番号(0〜7)、`name` は重複処理後の名前 |
| `lobby` | ロビーで変化があるたび | `{"t":"lobby","players":[{"id":0,"name":"hiira","ready":true,"score":0}],"countdown":5,"min":2,"max":8,"round":1,"rounds":1}`。`countdown` は未開始なら `null` |
| `start` | ラウンド開始 | `{"t":"start","round":1,"rounds":1,"w":7,"h":7,"tick_ms":50,"time":0,"players":[{"id":0,"name":"hiira","x":1,"y":1}]}` |
| `state` | 毎 tick | 下記 |
| `result` | ラウンド終了 | 下記 |
| `end` | セッション終了(全ラウンド終了、またはサーバ終了) | `{"t":"end","rounds":1,"score":[{"id":0,"name":"hiira","score":1,"kills":2}]}`。サーバ終了時は `{"t":"end","msg":"server closed"}` |
| `error` | 不正な HELLO | `{"t":"error","msg":"..."}` のあと切断 |

`state`:

```json
{"t":"state","tick":123,"left":-1,
 "map":["#######","#1   2#","# # # #","#  o  #","# # # #","#   * #","#######"],
 "players":[{"id":0,"x":1,"y":1,"st":"alive","bombs":3,"maxb":4,"fire":3,"spd":1,"kills":0}],
 "bombs":[{"x":3,"y":3,"fuse":1.2,"owner":0}]}
```

- `map` は 1 行 1 文字列、1 マス 1 文字。`#` ハードブロック、`+` ソフトブロック、`o` ボム、`*` 炎、`1`〜`8` 生存プレイヤー(番号 = `id`+1)、`x` 死亡演出中、`F`/`B`/`S` アイテム、空白は何もなし。重なるときは 炎 > プレイヤー > ボム > アイテム > ブロック の優先で 1 文字にする。正確な座標は `players` / `bombs` にある。
- `left` は残り秒(制限時間なしなら -1)。`st` は `alive` / `dying` / `dead`。`bombs` は今置ける数、`fire` は炎の長さ、`spd` は速度段階。

`result`:

```json
{"t":"result","round":1,"rounds":1,"ticks":115,"timeout":false,
 "ranking":[{"id":1,"name":"bob","rank":1,"kills":1,"items":0,"cause":"alive","by":-1,"score":0},
            {"id":0,"name":"alice","rank":2,"kills":0,"items":0,"cause":"killed","by":1,"score":0}],
 "events":[{"type":"kill","tick":110,"killer":1,"victim":0}]}
```

`cause` は `alive`(生存)/ `killed`(`by` に倒した相手)/ `suicide`(自爆)/ `suddendeath` / `disconnect` / `timeout`。同じ tick に死んだ場合は同順位。

### ロビーと観戦

- 接続直後に `HELLO` を送ると `welcome` と `lobby` が返る。`lobby` はロビーの変化(参加、退出、準備、カウントダウンの秒)ごとに全員へ送る。
- `min-players` 人以上が全員 `READY 1` になるとカウントダウンが始まり、誰かが取り消すか退出すると止まる。
- ラウンド中に `HELLO` した接続は観戦者になる(`start` と毎 tick の `state` は届くが `IN` は無視)。次のロビーから参加者になる。
- ラウンド中の切断は死亡(`disconnect`)扱い。ロビー中の切断は一覧から消える。

## 4. ゲームの仕組み(サーバ)

### 4.1 tick

50 ms(20 Hz)。`Game::tick` は次の順で進める。

1. 入力の反映: ボム要求があれば置く(自分のマスにボムがなく、置ける数が残っていれば)。方向入力があり、移動のクールダウン(`speed` tick)が切れていれば 1 マス進める。**1 回の入力で 1 マス**(押しっぱなしは OS のキーリピートで入力が繰り返されるだけ)。ブロックとボムのマスには入れない。自分が置いたボムのマスからは出られる。
2. ボム: 導火線を減らし、0 になったら爆発。炎は爆心から十字に `power` マス。ハードブロックの手前で止まり、ソフトブロック・アイテム・ボムのマスは含めて止まる。ボムに当たると**同じ tick で**誘爆する。炎のマスには「持ち主」が記録される(先に来た炎が優先)。
3. 炎の上にいる生存者は死亡演出(`dying`、1 秒)に入る。持ち主が自分なら自爆、他人ならその人のキル。ハードブロックのマスにいる場合も死ぬ(サドンデス用)。
4. 生存者が 1 人以下になったら 1 秒後にラウンド終了。制限時間(`--time`)があれば、時間切れでも終了(生存者は同順位)。

### 4.2 現在のルール(段階 0 = v1 と同じ)

| 項目 | 値 |
|---|---|
| マップ | `4*ceil(sqrt(人数))-1` の正方形(2〜4 人は 7×7)。外周と偶数座標にハードブロック |
| 初期位置 | (1,1) から x を 4 ずつ、折り返して y を 4 ずつ |
| 同時ボム数 / 炎の長さ | 4 / 3 |
| 導火線 / 炎の持続 / 死亡演出 | 2.0 秒 / 1.0 秒 / 1.0 秒 |
| 移動速度 | 5 tick(0.25 秒)に 1 マス = 4 マス/秒 |
| 制限時間 | なし |

### 4.3 順位

死亡 tick の降順(生存者が 1 位)。同じ tick に死んだ場合は同順位。`--rounds` が 2 以上のときは 1 位に 1 点、全ラウンド終了で `end` に合計を入れる。

## 5. クライアント(`A_client.py`)

1. 引数(または既定 127.0.0.1)に接続し、`HELLO 1 名前` を送る。受信スレッドが JSON 行を読んでキューに積む。
2. `curses.wrapper` の中で 20 ms ごとにキューを空にして画面を更新し、キーを読む。例外や `Ctrl-C` でも端末は復元される。
3. 画面は 3 種類。ロビー(名前一覧、準備状態、カウントダウン)、ゲーム(上 1 行 HUD、マップ、下に番号と名前の凡例)、結果(順位と死因)。結果画面は何かキーを押すとロビーに戻る。
4. 端末がマップに足りない場合は「terminal too small」と必要サイズを出す。リサイズで再描画する。

全角モードの文字: `＃` ハード、`田` ソフト、`爆` ボム、`炎`、`死`、`火`/`弾`/`速` アイテム、`１`〜`８` プレイヤー(自分は反転)。いずれも East Asian Width が Wide の文字で、環境で幅が変わる `■` は使わない。

## 6. macOS で直した点(v1 から引き継ぎ)

- `-lncursesw` が macOS にないため `-lncurses` を使う。v2 では makefile が `ncursesw` → `ncurses` の順に試し、どちらもなければ `NO_CURSES` でビルドする。
- `sockaddr_in` は `memset` で 0 埋めしてから使う(BSD 系の `sin_len`)。
