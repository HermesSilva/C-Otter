"""Proxy SOCKS5 minimo, para conferir a aba Proxy contra um proxy de verdade.

So' biblioteca padrao. Escuta em 127.0.0.1 e repassa para o destino pedido;
com --user/--password exige a autenticacao do RFC 1929.

    python tools/socks5_test_server.py --port 1081 [--user ana --password s3]

Escreve uma linha por conexao em stdout -- e' o que prova que a conexao do
C-Otter PASSOU pelo proxy, e nao foi direta:

    CONNECT localhost:5432 (auth=ana)
"""
import argparse
import select
import socket
import struct
import sys
import threading


def read_exact(conn, count):
    data = b''
    while len(data) < count:
        chunk = conn.recv(count - len(data))
        if not chunk:
            raise ConnectionError('fim da conexao')
        data += chunk
    return data


def relay(a, b):
    try:
        while True:
            ready, _, _ = select.select([a, b], [], [], 60)
            if not ready:
                break
            for source in ready:
                data = source.recv(65536)
                if not data:
                    return
                (b if source is a else a).sendall(data)
    except OSError:
        pass
    finally:
        a.close()
        b.close()


def handle(conn, args):
    try:
        version, count = read_exact(conn, 2)
        methods = read_exact(conn, count)
        if version != 5:
            conn.close()
            return

        user = None
        if args.user:
            if 2 not in methods:
                conn.sendall(b'\x05\xff')
                conn.close()
                print('REFUSED: no password method offered', flush=True)
                return
            conn.sendall(b'\x05\x02')
            read_exact(conn, 1)
            user = read_exact(conn, read_exact(conn, 1)[0]).decode()
            password = read_exact(conn, read_exact(conn, 1)[0]).decode()
            if user != args.user or password != args.password:
                conn.sendall(b'\x01\x01')
                conn.close()
                print('REFUSED: bad credentials for %s' % user, flush=True)
                return
            conn.sendall(b'\x01\x00')
        else:
            conn.sendall(b'\x05\x00')

        version, command, _, kind = read_exact(conn, 4)
        if kind == 1:
            host = socket.inet_ntoa(read_exact(conn, 4))
        elif kind == 3:
            host = read_exact(conn, read_exact(conn, 1)[0]).decode()
        else:
            host = socket.inet_ntop(socket.AF_INET6, read_exact(conn, 16))
        port = struct.unpack('>H', read_exact(conn, 2))[0]

        if command != 1:
            conn.sendall(b'\x05\x07\x00\x01' + bytes(6))
            conn.close()
            return

        try:
            target = socket.create_connection((host, port), timeout=10)
        except OSError:
            conn.sendall(b'\x05\x05\x00\x01' + bytes(6))   # connection refused
            conn.close()
            print('FAILED %s:%d' % (host, port), flush=True)
            return

        conn.sendall(b'\x05\x00\x00\x01' + bytes(6))
        print('CONNECT %s:%d (auth=%s)' % (host, port, user), flush=True)
        relay(conn, target)
    except (ConnectionError, OSError):
        conn.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=1081)
    parser.add_argument('--user')
    parser.add_argument('--password', default='')
    args = parser.parse_args()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('127.0.0.1', args.port))
    server.listen(16)
    print('LISTENING 127.0.0.1:%d' % args.port, flush=True)

    while True:
        conn, _ = server.accept()
        threading.Thread(target=handle, args=(conn, args), daemon=True).start()


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
