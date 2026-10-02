"""Vetor de teste do O5LOGON (verificador 12c), por uma implementacao independente.

    python3 tools/ora_auth_vector.py        # precisa do `openssl` no PATH

A Oracle nao publica vetores oficiais do logon. Este script refaz a conta com
outras pecas -- hashlib do Python para SHA-512 e PBKDF2, a linha de comando do
OpenSSL para o AES -- e imprime os valores que tests/unit/test_orawire.cpp
confere. Se o C++ e este script concordam, as primitivas e a ordem das
operacoes estao certas; que o SERVIDOR aceita o resultado quem prova e' o
spike_oraconnect.
"""
import hashlib
import subprocess


def aes_cbc(key: bytes, data: bytes, decrypt: bool = False) -> bytes:
    cipher = {16: "aes-128-cbc", 24: "aes-192-cbc", 32: "aes-256-cbc"}[len(key)]
    args = ["openssl", "enc", "-" + cipher, "-nopad", "-K", key.hex(), "-iv", "00" * 16]
    if decrypt:
        args.append("-d")
    return subprocess.run(args, input=data, capture_output=True, check=True).stdout


def pad(data: bytes) -> bytes:
    n = 16 - len(data) % 16
    return data + bytes([n]) * n


password = b"OtterTest1"
verifier = bytes(range(0x00, 0x10))
server_half = bytes(range(0x10, 0x30))
client_half = bytes(range(0x40, 0x60))
combo_salt = bytes(range(0x60, 0x70))
password_salt = bytes(range(0xA0, 0xB0))
speedy_salt = bytes(range(0xB0, 0xC0))
verifier_rounds, combo_rounds = 4096, 3

password_key = hashlib.pbkdf2_hmac("sha512", password, verifier + b"AUTH_PBKDF2_SPEEDY_KEY",
                                   verifier_rounds, 64)
password_hash = hashlib.sha512(password_key + verifier).digest()[:32]

server_key = aes_cbc(password_hash, server_half)          # o que o servidor mandaria
session_key = aes_cbc(password_hash, client_half)
mixed = (client_half + server_half).hex().upper().encode()
combo_key = hashlib.pbkdf2_hmac("sha512", mixed, combo_salt, combo_rounds, 32)
speedy = aes_cbc(combo_key, pad(speedy_salt + password_key))[:80]
encrypted_password = aes_cbc(combo_key, pad(password_salt + password))
proof = aes_cbc(combo_key, bytes(range(0xC0, 0xD0)) + b"SERVER_TO_CLIENT")

for name, value in [("verifier_data", verifier), ("server_key", server_key),
                    ("combo_salt", combo_salt), ("session_key", session_key),
                    ("speedy_key", speedy), ("password", encrypted_password),
                    ("combo_key", combo_key), ("server_proof", proof)]:
    print(f"{name:14} {value.hex().upper()}")
