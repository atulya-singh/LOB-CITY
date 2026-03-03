import socket
import struct

# Configuration mapping your C++ UdpPublisher
MULTICAST_GROUP = '239.255.0.1'
PORT = 3000

# The struct format string tells Python how to unpack the C++ bytes:
# c = char (1 byte)
# Q = unsigned long long (8 bytes)
# q = long long (8 bytes)
# I = unsigned int (4 bytes)
# Total: 33 bytes (Matches your BboMessage exactly!)
BBO_FORMAT = '<cQqIqI' 

def main():
    # 1. Create the UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    # 2. Bind to the port
    sock.bind(('', PORT))

    # 3. Tell the operating system to join the Multicast Group
    mreq = struct.pack("4sl", socket.inet_aton(MULTICAST_GROUP), socket.INADDR_ANY)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

    print(f"--- Listening for Live Market Data on {MULTICAST_GROUP}:{PORT} ---")

    try:
        while True:
            # Receive the raw bytes from C++
            data, addr = sock.recvfrom(1024)
            
            # Ensure it's the right size before unpacking
            if len(data) == struct.calcsize(BBO_FORMAT):
                # Unpack the binary data back into Python variables
                msg_type, timestamp, bid_px, bid_qty, ask_px, ask_qty = struct.unpack(BBO_FORMAT, data)
                
                # Convert from byte string to regular string
                msg_type = msg_type.decode('ascii')
                
                if msg_type == 'B':
                    print(f"[{timestamp}] BBO UPDATE | BID: {bid_qty} @ {bid_px} | ASK: {ask_qty} @ {ask_px}")
                    
    except KeyboardInterrupt:
        print("\nListener stopped.")

if __name__ == "__main__":
    main()