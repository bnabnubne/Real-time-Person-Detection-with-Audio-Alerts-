import asyncio, json, socket
import websockets

UDP_IP   = "127.0.0.1"
UDP_PORT = 9001

WS_HOST  = "0.0.0.0"
WS_PORT  = 8765

clients = set()

def make_udp_socket():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((UDP_IP, UDP_PORT))
    s.setblocking(False)
    return s

async def udp_reader(sock):
    loop = asyncio.get_running_loop()
    while True:
        try:
            data, _ = await loop.sock_recvfrom(sock, 65535)
            # data là bytes JSON
            msg = data.decode("utf-8", errors="ignore").strip()
            # validate JSON nhẹ
            try:
                obj = json.loads(msg)
                payload = json.dumps(obj)
            except:
                continue

            # broadcast tới mọi client
            dead = []
            for ws in clients:
                try:
                    await ws.send(payload)
                except:
                    dead.append(ws)
            for ws in dead:
                clients.discard(ws)

        except BlockingIOError:
            await asyncio.sleep(0.001)

async def ws_handler(ws):
    clients.add(ws)
    try:
        # giữ kết nối, UI không cần gửi gì
        async for _ in ws:
            pass
    finally:
        clients.discard(ws)

async def main():
    sock = make_udp_socket()
    print(f"[BRIDGE] UDP {UDP_IP}:{UDP_PORT} -> WS {WS_HOST}:{WS_PORT}")

    async with websockets.serve(ws_handler, WS_HOST, WS_PORT):
        await udp_reader(sock)

if __name__ == "__main__":
    asyncio.run(main())