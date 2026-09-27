# CG2TeamServer の接続動作を確認する検証Client。
# Editorを起動せずに、Server側の契約だけを機械的に確かめる。
#   1. MagicDNS/Hostname 解決で接続できること(localhost名で確認)
#   2. Protocol不一致を拒否すること
#   3. Project ID不一致を拒否すること
#   4. 正しいHandshakeを受理し、Revisionを返すこと
#   5. Heartbeatにackを返すこと
#   6. Client間でMessageが中継されること
#   7. Project外へ出るPathを中継しないこと
#   8. 3台同時接続時に1台の変更が残り2台へ配信されること
#   9. 指定Revision以降の履歴を再配信できること
#  10. 切断したClientのLockが解放され、peerLeftが配られること

import json
import socket
import sys
import time

HOST = "localhost"  # 名前解決経路を通す(IP直書きではない)
PORT = 48000
PROTOCOL = 3
PROJECT_ID = "my-game"

failures = []


def check(condition, label):
    if condition:
        print("  OK   " + label)
    else:
        print("  FAIL " + label)
        failures.append(label)


def connect():
    connection = socket.create_connection((HOST, PORT), timeout=5)
    connection.settimeout(5)
    return connection


def send(connection, payload):
    connection.sendall((json.dumps(payload) + "\n").encode("utf-8"))


def receive(connection, wanted_type, timeout=5.0):
    """wanted_type の Message が来るまで読む。来なければ None。"""
    deadline = time.time() + timeout
    buffer = b""
    while time.time() < deadline:
        try:
            # socket自体のtimeoutは短いので、deadlineまでは諦めずに読み続ける。
            chunk = connection.recv(65536)
        except socket.timeout:
            continue
        if not chunk:
            return None
        buffer += chunk
        while b"\n" in buffer:
            line, buffer = buffer.split(b"\n", 1)
            if not line.strip():
                continue
            try:
                message = json.loads(line.decode("utf-8"))
            except json.JSONDecodeError:
                continue
            if wanted_type is None or message.get("type") == wanted_type:
                return message
    return None


def receive_until(connection, terminal_type, timeout=5.0):
    """terminal_typeまでに届いたJSON Messageをまとめて返す。"""
    deadline = time.time() + timeout
    buffer = b""
    messages = []
    while time.time() < deadline:
        try:
            chunk = connection.recv(65536)
        except socket.timeout:
            continue
        if not chunk:
            break
        buffer += chunk
        while b"\n" in buffer:
            line, buffer = buffer.split(b"\n", 1)
            if not line.strip():
                continue
            try:
                message = json.loads(line.decode("utf-8"))
            except json.JSONDecodeError:
                continue
            messages.append(message)
            if message.get("type") == terminal_type:
                return messages
    return messages


def handshake(connection, user_id, user_name, protocol=PROTOCOL, project_id=PROJECT_ID):
    send(connection, {
        "type": "handshake",
        "protocol": protocol,
        "engineVersion": "0.9.5+161",
        "projectFormat": 1,
        "scriptApi": 13,
        "channel": "Dev",
        "projectId": project_id,
        "userId": user_id,
        "userName": user_name,
    })


print("1. Hostname解決で接続")
try:
    probe = connect()
    check(True, "localhost(名前解決)経由で接続できる")
    probe.close()
except OSError as error:
    check(False, "localhost経由で接続できる: " + str(error))
    sys.exit(1)

print("2. Protocol不一致を拒否")
connection = connect()
handshake(connection, "user-bad-protocol", "BadProtocol", protocol=99)
reply = receive(connection, "handshakeNg")
check(reply is not None, "拒否Messageが返る")
if reply:
    check(str(PROTOCOL) in reply.get("reason", ""),
          "理由にServer protocolが入る: " + reply.get("reason", ""))
connection.close()

print("3. Project ID不一致を拒否")
connection = connect()
handshake(connection, "user-bad-project", "BadProject", project_id="other-game")
reply = receive(connection, "handshakeNg")
check(reply is not None, "拒否Messageが返る")
if reply:
    check("Project ID" in reply.get("reason", ""),
          "理由にProject IDが入る: " + reply.get("reason", ""))
connection.close()

print("4. 正しいHandshakeを受理")
client_a = connect()
handshake(client_a, "user-a", "EditorA")
reply = receive(client_a, "handshakeOk")
check(reply is not None, "受理Messageが返る")
if reply:
    check(reply.get("protocol") == PROTOCOL, "protocolが返る")
    check("revision" in reply, "Server Revisionが返る")

print("5. Heartbeatにackを返す")
send(client_a, {"type": "heartbeat", "userId": "user-a", "sentAt": 1234567})
reply = receive(client_a, "heartbeatAck")
check(reply is not None, "heartbeatAckが返る")
if reply:
    check(reply.get("sentAt") == 1234567, "sentAtをそのまま返す(Latency計測用)")

print("6. Client間でMessageを中継")
client_b = connect()
handshake(client_b, "user-b", "EditorB")
receive(client_b, "handshakeOk")
time.sleep(0.3)
send(client_a, {
    "type": "commit", "userId": "user-a", "userName": "EditorA",
    "scenePath": "Assets/Scenes/Main.scene", "objectUuid": "obj-1",
    "operation": "update", "revision": 42, "fileSize": 128,
})
relayed = receive(client_b, "commit")
check(relayed is not None, "AのcommitがBへ中継される")
if relayed:
    committed_revision = relayed.get("revision", 0)
    check(committed_revision > 0 and committed_revision != 42,
          "ServerがAuthoritative Revisionを採番する")
# Serverは確定Commitを送信元にも返す。後続の3台同時配信検証で
# 古いCommitを誤って受け取らないよう、A側の確認分をここで消費する。
receive(client_a, "commit")

print("7. Project外へ出るPathを中継しない")
send(client_a, {
    "type": "commit", "userId": "user-a", "userName": "EditorA",
    "scenePath": "../../Windows/System32/evil.dll", "objectUuid": "obj-2",
    "operation": "update", "revision": 43, "fileSize": 16,
})
time.sleep(0.5)
leaked = receive(client_b, "commit", timeout=1.0)
check(leaked is None, "../ を含むPathは中継されない")

print("8. 3台同時接続で変更を配信")
client_c = connect()
handshake(client_c, "user-c", "EditorC")
reply = receive(client_c, "handshakeOk")
check(reply is not None, "3台目のHandshakeが受理される")
send(client_b, {
    "type": "commit", "userId": "user-b", "userName": "EditorB",
    "scenePath": "Assets/Scenes/Main.scene", "objectUuid": "obj-three-clients",
    "operation": "update", "revision": 50, "fileSize": 128,
})
relayed_to_a = receive(client_a, "commit")
relayed_to_c = receive(client_c, "commit")
check(relayed_to_a is not None, "Bのcommitが接続済みAへ届く")
check(relayed_to_c is not None, "Bのcommitが3台目Cへ届く")

print("9. 指定Revision以降の履歴を再取得")
send(client_c, {"type": "historyRequest", "userId": "user-c", "afterRevision": 0})
history = receive_until(client_c, "historyEnd")
history_types = [message.get("type") for message in history]
check("historyBegin" in history_types, "履歴開始Messageが返る")
check(any(message.get("type") == "commit" and message.get("targetUserId") == "user-c"
          for message in history), "保存済みCommitが要求Clientだけへ返る")
check("historyEnd" in history_types, "履歴完了Messageが返る")
client_c.close()

print("10. Lockと切断時解放")
send(client_a, {
    "type": "lockRequest", "userId": "user-a", "userName": "EditorA",
    "objectUuid": "obj-lock-1", "componentUuid": "",
})
relayed = receive(client_b, "lockRequest")
check(relayed is not None, "LockがBへ中継される")
client_a.close()
print("  (Aを切断。Heartbeat Timeout + Grace を待機)")
peer_left = receive(client_b, "peerLeft", timeout=40.0)
check(peer_left is not None, "Aの離脱がBへ通知される(Lock解放)")
if peer_left:
    check(peer_left.get("userId") == "user-a", "離脱したUser IDが入る")
client_b.close()

print()
if failures:
    print("FAILED: " + str(len(failures)) + " 件")
    for failure in failures:
        print("  - " + failure)
    sys.exit(1)

print("ALL PASS")
sys.exit(0)
