import socket   # ソケット通信のため
import curses

# サーバーのIPアドレス
SERVER_IP = "192.168.0.3" 
# ポート番号
PORT = 8000 

MAP_SIZE_X = 7
MAP_SIZE_Y = 7

# マップデータの2次元配列(MAP_SIZE_X * MAP_SIZE_Y)の作成
map_data2 = [[0 for i in range(MAP_SIZE_X)] for j in range(MAP_SIZE_Y)]

# ソケットを作成する
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# サーバーに接続する
s.connect((SERVER_IP, PORT))

# cursesの初期化処理（おまじない）
stdscr = curses.initscr()
curses.noecho()
curses.cbreak()
curses.halfdelay(1)

# 受信とマップの表示ループ
while True:
    # 2048byteまで受信
    receive_data = s.recv(2048)
    
    # 受信データからキャラの状態（生死）のデータを取り出す
    state = receive_data[0]

    # 改行文字が入っていたら終了とみなす
    if state == ord('\n'):
        break
    
    # 受信データからマップデータを取り出す
    map_data = receive_data[1:]

    # 画面クリア
    stdscr.clear()

    # マップデータを全走査して表示
    for j in range(MAP_SIZE_Y):
        for i in range(MAP_SIZE_X):
            # j行目のi文字目を取り出す
            square = map_data[j*MAP_SIZE_Y + i]
            map_data2[j][i] = square

            # プレイヤー（生存）
            if square & 0x80:
                stdscr.addstr("敵 ")

            # プレイヤー（死亡）
            elif square & 0x40:
                stdscr.addstr("死 ")

            # ブロック　（なぜか半角スペース2つ必要）
            elif square & 0x20:
                stdscr.addstr("■  ")

            # ボム
            elif square & 0x10:
                stdscr.addstr("爆*")

            # 炎
            elif square & 0x08:
                stdscr.addstr("炎 ")

            # なにもない
            else:
                stdscr.addstr("   ")
        # 改行
        stdscr.addch('\n')
    
    # リフレッシュして画面にマップを適用
    stdscr.refresh()


# cursesの終了処理（おまじない）
curses.nocbreak()
stdscr.keypad(False)
curses.echo()
curses.endwin()
