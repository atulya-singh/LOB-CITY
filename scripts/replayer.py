import socket
import time

# Configuration
HOST = '127.0.0.1' # Localhost
PORT = 5050
NUM_ORDERS = 100000

def generate_fix_message(order_id):
    # Alternating Buy (1) and Sell (2) 
    side = '1' if order_id % 2 == 0 else '2'
    # Shifting prices slightly around 150.0
    price = 150.0 + (order_id % 10) 
    
    msg = (f"8=FIX.4.2\x01"
           f"35=D\x01"
           f"11={order_id}\x01"
           f"54={side}\x01"
           f"38=100\x01"
           f"44={price:.2f}\x01"
           f"40=2\x01"
           f"10=000\x01")  # ← terminating field
    return msg.encode('ascii')

def main():
    print(f"--- Market Replayer Starting ---")
    print(f"Generating {NUM_ORDERS} FIX messages in memory...")
    
    # Pre-generate all bytes so we only measure network/C++ speed, not Python string formatting speed
    messages = [generate_fix_message(i) for i in range(1, NUM_ORDERS + 1)]
    
    print(f"Connecting to C++ Matching Engine at {HOST}:{PORT}...")
    
    # Create the TCP socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.connect((HOST, PORT))
            print("Connected! Blasting orders over TCP...")
            
            start_time = time.time()
            
            # Send all 100,000 messages
            for msg in messages:
                s.sendall(msg)
                
            end_time = time.time()
            
            duration = end_time - start_time
            print("\n--- Load Test Complete ---")
            print(f"Total Time: {duration:.4f} seconds")
            print(f"Throughput: {NUM_ORDERS / duration:,.0f} messages/sec")
            
        except ConnectionRefusedError:
            print("[-] Connection failed. Is the C++ lob_server running?")

if __name__ == "__main__":
    main()