import socket
import curses

PORT = 8000
BUFSIZE = 256

SERVER_IP = "192.168.0.12"

socket1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
socket1.connect((SERVER_IP, PORT))

stdscr = curses.initscr()

# curses initialize
curses.noecho()
curses.cbreak()
stdscr.keypad(True)
curses.halfdelay(1)

while True:
    c = stdscr.getch()
    if c == curses.ERR:
        socket1.send(c.to_bytes(4, byteorder="little", signed=True))
        if chr(c) == 'q':
            break

# curses end
curses.nocbreak()
stdscr.keypad(False)
curses.echo()
curses.endwin()
