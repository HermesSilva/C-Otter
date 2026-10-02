"""SQL Server Browser de mentira, para conferir a resolucao de instancia nomeada.

O servico de verdade (UDP 1434) costuma estar desligado em maquina de
desenvolvimento. Este responde ao CLNT_UCAST_INST do protocolo [MC-SQLR] como
o Browser responderia, dizendo que a instancia pedida escuta na porta dada:

    python tools/sqlbrowser_test_server.py --port 11434 --instance OTTER --tcp 1433

Uma instancia diferente da configurada nao recebe resposta -- e' o que o
Browser faz, e o cliente precisa tratar como "nao existe".
"""
import argparse
import socket

parser = argparse.ArgumentParser()
parser.add_argument('--port', type=int, default=11434)
parser.add_argument('--instance', default='OTTER')
parser.add_argument('--tcp', type=int, default=1433)
parser.add_argument('--count', type=int, default=0, help='encerra depois de N pedidos (0 = nunca)')
args = parser.parse_args()

server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
server.bind(('127.0.0.1', args.port))
print('escutando em 127.0.0.1:%d (instancia %s -> tcp %d)' % (args.port, args.instance, args.tcp),
      flush=True)

served = 0
while args.count == 0 or served < args.count:
    data, client = server.recvfrom(4096)
    served += 1
    if len(data) < 2 or data[0] != 0x04:
        print('pedido desconhecido:', data[:8].hex(), flush=True)
        continue
    wanted = data[1:].split(b'\x00', 1)[0].decode('ascii', 'replace')
    print('pedido da instancia', wanted, flush=True)
    if wanted.upper() != args.instance.upper():
        continue
    text = ('ServerName;LOCALHOST;InstanceName;%s;IsClustered;No;Version;16.0.1000.6;'
            'tcp;%d;np;\\\\LOCALHOST\\pipe\\MSSQL$%s\\sql\\query;;'
            % (args.instance, args.tcp, args.instance)).encode('ascii')
    server.sendto(b'\x05' + len(text).to_bytes(2, 'little') + text, client)
