import getpass  # ユーザー名取得のため
import socket   # ソケット通信のため

# サーバーのIPアドレス
SERVER_IP = "192.168.0.3" 
# ポート番号
PORT = 8000 

# スタート時の処理 -----------------------------------------

# ログインしているユーザー名を取得する
user_name = getpass.getuser()

# ソケット通信で送るために、bytes型に変換する
user_name_bytes = user_name.encode("utf-8")

# ソケットを作成する
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# サーバーに接続する
s.connect((SERVER_IP, PORT))

# 自分の名前（ログインしているユーザー名）を送信する
s.send(user_name_bytes)

# サーバーからデータ(4byte)を受信
receive_data = s.recv(4)

# 受信データの配列から初期位置をとりだす
# 1番目がx座標、2番目がy座標
x = receive_data[0]
y = receive_data[1]
# 3番目がマップサイズ（x）、4番目がマップサイズ（y）
map_size_x = receive_data[2]
map_size_y = receive_data[3]

# 座標の表示
print("x = %d, y = %d\n" %(x, y))
# マップサイズの表示
print("mapsize %d * %d\n" %(map_size_x, map_size_y))

# --------------------------------------------------------


# エンド時の処理 -----------------------------------------

# サーバーからデータを受信　データサイズは適当に2048byte
receive_data = s.recv(2048)

# 受信データをstr型に変換する
ranking = receive_data.decode("utf-8")

# ランキングを表示する
print(ranking)

# --------------------------------------------------------