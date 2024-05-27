import socket
import threading

# List to keep track of active TCP client sockets
tcp_clients = []
lock = threading.Lock()

def handle_client(client_socket, client_address):
    print(f"Client {client_address} connected.")
    with lock:
        tcp_clients.append(client_socket)
    try:
        while True:
            message = client_socket.recv(1024)
            if not message:
                break
            print(f"Message from {client_address}: {message.decode('utf-8')}")
    except Exception as e:
        print(f"Error handling data from {client_address}: {e}")
    finally:
        with lock:
            tcp_clients.remove(client_socket)
        print(f"Client {client_address} disconnected.")
        client_socket.close()

def handle_udp_client(server_socket):
    print("UDP server started, listening on port 9998...")
    try:
        while True:
            message, client_address = server_socket.recvfrom(1024)
            if message:
                decoded_message = message.decode('utf-8')
                print(f"UDP message from {client_address}: {decoded_message}")
                # Forwarding message to all TCP clients
                forward_message_to_tcp_clients(decoded_message)
    except Exception as e:
        print(f"Error with UDP server: {e}")

def forward_message_to_tcp_clients(message):
    with lock:
        for client in tcp_clients:
            try:
                client.sendall(message.encode('utf-8'))
            except Exception as e:
                print(f"Error sending message to TCP client: {e}")
                tcp_clients.remove(client)

def start_servers():
    tcp_server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    udp_server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    tcp_server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp_server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    tcp_server_socket.bind(('0.0.0.0', 9999))
    udp_server_socket.bind(('0.0.0.0', 9998))

    tcp_server_socket.listen(5)
    print("TCP server started, listening on port 9999...")

    # Start TCP server in a new thread
    threading.Thread(target=accept_tcp_connections, args=(tcp_server_socket,)).start()
    # Start UDP server in a new thread
    threading.Thread(target=handle_udp_client, args=(udp_server_socket,)).start()

def accept_tcp_connections(server_socket):
    try:
        while True:
            client_socket, client_address = server_socket.accept()
            client_thread = threading.Thread(target=handle_client, args=(client_socket, client_address))
            client_thread.start()
    except KeyboardInterrupt:
        print("TCP server is shutting down...")
    finally:
        server_socket.close()

if __name__ == '__main__':
    start_servers()
