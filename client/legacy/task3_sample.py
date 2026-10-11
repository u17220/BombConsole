import socket   # ソケット通信のため
import curses


# サーバーのIPアドレス
SERVER_IP = "192.168.0.3" 
# ポート番号
PORT = 8000 

MAP_SIZE_X = 5
MAP_SIZE_Y = 5

# サンプルマップデータ
map_data2 = [[0x20, 0x20, 0x20, 0x20, 0x20],
             [0x20, 0x08, 0x80, 0x40, 0x20], 
             [0x20, 0x10, 0x20, 0x00, 0x20], 
             [0x20, 0x00, 0x00, 0x00, 0x20],
             [0x20, 0x20, 0x20, 0x20, 0x20]]
"""
■  ■  ■  ■  ■
■  炎  敵  死  ■
■  爆* ■      ■
■      自      ■
■  ■  ■  ■  ■
"""

# 初期位置
x = 3
y = 3

# ソケットを作成する
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# サーバーに接続する
s.connect((SERVER_IP, PORT))


# cursesの初期化処理（おまじない）
stdscr = curses.initscr()
curses.noecho()
curses.cbreak()
curses.curs_set(0)
stdscr.keypad(True)
curses.halfdelay(1)

# キー入力と送信ループ
while True:
    # ボムを置いたか 0 or 1
    bomb = 0

    # 入力されたキーの取得
    key = stdscr.getch()

    # qで終了
    if(key == ord('q')):
        break

    elif(key == curses.KEY_UP):
        y -= 1
    
    elif(key == curses.KEY_DOWN):
        y += 1
    
    elif(key == curses.KEY_LEFT):
        x -= 1

    elif(key == curses.KEY_RIGHT):
        x += 1

    elif(key == ord(' ')):
        bomb = 1

    # 何も入力されていないときはデータを送信しない
    if(key != curses.ERR):
        # 送信データの作成
        send_data = x.to_bytes(1, byteorder="little", signed=False) + y.to_bytes(1, byteorder="little", signed=False) + bomb.to_bytes(1, byteorder="little", signed=False)
        # 送信
        s.send(send_data)

    # 画面クリア
    stdscr.clear()

    # マップデータを全走査して表示
    for j in range(MAP_SIZE_Y):
        for i in range(MAP_SIZE_X):
            square = map_data2[j][i]
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
        stdscr.addstr('\n')

    # 自キャラを表示する
    stdscr.addstr(y, x*3, "自 ")
    
    # リフレッシュして画面にマップを適用
    stdscr.refresh()


# cursesの終了処理（おまじない）
curses.nocbreak()
stdscr.keypad(False)
curses.echo()
curses.endwin()
