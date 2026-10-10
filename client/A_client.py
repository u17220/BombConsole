import socket   # ソケット通信のため
import curses   # 画面描画、キー入力のため
import getpass  # ユーザー名取得のため
import sys      # 引数を使うため

# サーバーのIPアドレス
if len(sys.argv) == 1:
    SERVER_IP = socket.gethostbyname(socket.gethostname())
else:
    SERVER_IP = sys.argv[1]
#SERVER_IP = "192.168.0.9" 

# ポート番号
PORT = 10001 

# ちょうど size バイト受信する（TCPは1回のrecvで全部届くとは限らない）
# サーバとの接続が切れたら None を返す
def recv_exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            return None
        data += chunk
    return data


#   スタート時の処理 -----------------------------------------
user_name = getpass.getuser()
user_name_bytes = user_name.encode("utf-8")

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((SERVER_IP, PORT))
s.send(user_name_bytes)

receive_data = recv_exact(s, 4)
if receive_data is None:
    sys.exit("サーバとの接続が切れました")

x = receive_data[0]
y = receive_data[1]

MAP_SIZE_X = receive_data[2]
MAP_SIZE_Y = receive_data[3]
# --------------------------------------------------------


# マップデータの2次元配列(MAP_SIZE_X * MAP_SIZE_Y)の作成
map_data2 = [[0 for i in range(MAP_SIZE_X)] for j in range(MAP_SIZE_Y)]

# cursesの初期化処理（おまじない)
stdscr = curses.initscr()
curses.noecho()
curses.cbreak()
stdscr.timeout(10)
curses.start_color()
curses.curs_set(0)
stdscr.keypad(True)
curses.init_color(curses.COLOR_YELLOW,1000,1000,0)
curses.init_color(curses.COLOR_MAGENTA,1000,0,686)
curses.init_pair(1,curses.COLOR_RED,curses.COLOR_BLACK)
curses.init_pair(2,curses.COLOR_BLUE,curses.COLOR_BLACK)
curses.init_pair(3,curses.COLOR_GREEN,curses.COLOR_BLACK)
curses.init_pair(4,curses.COLOR_YELLOW,curses.COLOR_BLACK)
curses.init_pair(5,curses.COLOR_MAGENTA,curses.COLOR_YELLOW)


# 受信とマップの表示ループ
while True:
    #  キー入力による自キャラの操作とデータ送信処理#
    nx=x
    ny=y
	#ボムの有無
    bomb = 0

    #キー入力
    c=stdscr.getch()
    curses.flushinp()   # 押しっぱなし対策

    if  c == curses.KEY_UP: 
        ny = y-1

    elif c == curses.KEY_DOWN:
        ny = y+1

    elif c == curses.KEY_LEFT:
        nx = x-1

    elif c == curses.KEY_RIGHT:
        nx = x+1

    elif (c == ord(' ')):
        bomb = 1

    elif (c == ord('q')):
        curses.endwin()
        s.close()
        exit()

    #if	(map_data2[ny][nx] != 0x20):
    if	(map_data2[ny][nx] & 0x30 == 0):
        x=nx
        y=ny

    
    data=x.to_bytes(1,"little", signed=False)+y.to_bytes(1,"little", signed=False)+bomb.to_bytes(1, "little", signed=False) #送信用データ生成
    s.send(data) #データ送信

    
    #  -------------------------------------------------------------------
    

    #  マップデータの受信と表示処理    #
    receive_data = recv_exact(s, 1)
    if receive_data is None:
        break
    
    # 受信データからキャラの状態（生死）のデータを取り出す
    state = receive_data[0]

    # 改行文字がstateに入っていたら終了とみなす
    if state == ord('\n'):
        break
    
    # 受信データからマップデータを取り出す
    map_data = recv_exact(s, MAP_SIZE_X*MAP_SIZE_Y)
    if map_data is None:
        break
    
    # バッファクリア
    stdscr.erase()

    # マップデータを全走査して表示
    for j in range(MAP_SIZE_Y):
        for i in range(MAP_SIZE_X):
            # j行目のi文字目を取り出す
            square = map_data[j*MAP_SIZE_X + i]
            map_data2[j][i] = square

            # プレイヤー（生存）
            if square & 0x80:
                if square & 0x04:
                     stdscr.addstr("敵 ",curses.color_pair(5))
                else:
                    stdscr.addstr("敵 ",curses.color_pair(3))

            # プレイヤー（死亡）
            elif square & 0x40:
                stdscr.addstr("死 ",curses.color_pair(5))

            # ブロック　（なぜか半角スペース2つ必要）
            elif square & 0x20:
                stdscr.addstr("■  ")

            # ボム
            elif square & 0x10:
                if square & 0x04:
                     stdscr.addstr("爆*",curses.color_pair(5))
                else:
                     stdscr.addstr("爆",curses.color_pair(2))
                     stdscr.addstr("*",curses.color_pair(4))
            # 炎
            elif square & 0x08:
                if square & 0x04:
                     stdscr.addstr("炎 ",curses.color_pair(5))
                else:
                     stdscr.addstr("炎 ",curses.color_pair(1))

            # なにもない
            else:
                if square & 0x04:
                     stdscr.addstr("   ",curses.color_pair(5))
                else:
                     stdscr.addstr("   ")

        # 改行
        stdscr.addch('\n')
    

    # 自キャラを表示する(state==0のとき→生存)
    if(state == 0):
        stdscr.addstr(y, x*3, "自 ")
    else:
        stdscr.addstr(MAP_SIZE_Y, 0, "YOU DIED", curses.color_pair(1))
    # リフレッシュして画面にマップを適用
    stdscr.refresh()

    #  -------------------------------------------------------------------


# cursesの終了処理（おまじない）
curses.nocbreak()
stdscr.keypad(False)
curses.echo()
curses.endwin()


#  エンド時の処理 -----------------------------------------
# サーバが接続を閉じるまで受信する
receive_data = b""
while True:
    chunk = s.recv(2048)
    if not chunk:
        break
    receive_data += chunk

ranking = receive_data.decode("utf-8", errors="replace")
print("RANKING")
print(ranking)

# --------------------------------------------------------

# socketのクローズ
s.close()
