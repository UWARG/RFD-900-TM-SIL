#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <ctime>
#include <cstdint>
#include <cstring>
#include <random>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <arpa/inet.h>

// Function to simulate an RFD900 packet
std::vector<uint8_t> simulate_rfd900_packet() {
    std::vector<uint8_t> packet;
    // Start delimiter (0x7E)
    packet.push_back(0x7E);
    
    // Payload: 4 bytes timestamp + 3 floats (4 bytes each) = 16 bytes
    uint8_t payloadLength = 16;
    packet.push_back(payloadLength);
    
    std::vector<uint8_t> payload;
    
    // Get current timestamp (4 bytes unsigned int)
    uint32_t timestamp = static_cast<uint32_t>(std::time(nullptr));
    uint32_t timestamp_be = htonl(timestamp); // Convert to big-endian (network byte order)
    uint8_t* ts_ptr = reinterpret_cast<uint8_t*>(&timestamp_be);
    payload.insert(payload.end(), ts_ptr, ts_ptr + sizeof(timestamp_be));
    
    // Set up random generators for latitude, longitude, and altitude
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> distLat(-90.0f, 90.0f);
    std::uniform_real_distribution<float> distLon(-180.0f, 180.0f);
    std::uniform_real_distribution<float> distAlt(0.0f, 10000.0f);
    
    float lat = distLat(gen);
    float lon = distLon(gen);
    float alt = distAlt(gen);
    
    // Helper lambda to convert a float to 4 bytes (big-endian)
    auto appendFloat = [&payload](float value) {
        union {
            float f;
            uint32_t i;
        } u;
        u.f = value;
        uint32_t net = htonl(u.i);
        uint8_t* ptr = reinterpret_cast<uint8_t*>(&net);
        payload.insert(payload.end(), ptr, ptr + sizeof(net));
    };
    
    appendFloat(lat);
    appendFloat(lon);
    appendFloat(alt);
    
    // Compute checksum: sum of all payload bytes modulo 256
    uint8_t checksum = 0;
    for (uint8_t byte : payload) {
        checksum += byte;
    }
    checksum = checksum % 256;
    
    // Append payload and checksum to the packet
    packet.insert(packet.end(), payload.begin(), payload.end());
    packet.push_back(checksum);
    
    return packet;
}

int main() {
    // Set your serial port here (e.g., "/dev/ttyUSB0")
    const char* portName = "/dev/ttyUSB0";
    // Use termios constants for baudrate; here we use B57600
    int baudrate = B57600;

    // Open the UART port
    int fd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        std::cerr << "Error opening port " << portName << std::endl;
        return 1;
    }
    
    // Configure the UART interface using termios
    struct termios tty;
    memset(&tty, 0, sizeof tty);
    if (tcgetattr(fd, &tty) != 0) {
        std::cerr << "Error from tcgetattr" << std::endl;
        close(fd);
        return 1;
    }
    
    cfsetospeed(&tty, baudrate);
    cfsetispeed(&tty, baudrate);
    
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;   // 8-bit characters
    tty.c_iflag &= ~IGNBRK;                         // disable break processing
    tty.c_lflag = 0;                                // no signaling characters, no echo, no canonical processing
    tty.c_oflag = 0;                                // no remapping, no delays
    tty.c_cc[VMIN]  = 0;                            // non-blocking read
    tty.c_cc[VTIME] = 5;                            // 0.5 seconds read timeout
    
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);         // shut off xon/xoff ctrl

    tty.c_cflag |= (CLOCAL | CREAD);                // ignore modem controls, enable reading
    tty.c_cflag &= ~(PARENB | PARODD);              // no parity
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::cerr << "Error from tcsetattr" << std::endl;
        close(fd);
        return 1;
    }
    
    std::cout << "Starting simulated RFD900 output on port " << portName << std::endl;
    
    // Main loop: generate and send a packet every second
    while (true) {
        std::vector<uint8_t> packet = simulate_rfd900_packet();
        ssize_t bytesWritten = write(fd, packet.data(), packet.size());
        if (bytesWritten != static_cast<ssize_t>(packet.size())) {
            std::cerr << "Error writing to port" << std::endl;
        } else {
            std::cout << "Sent packet: ";
            for (uint8_t byte : packet) {
                printf("%02X ", byte);
            }
            std::cout << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    close(fd);
    return 0;
}
