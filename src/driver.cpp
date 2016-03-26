//=======================================================================
// Copyright (c) 2015-2016 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>

namespace {

const std::size_t UNIX_PATH_MAX = 108;
const std::size_t buffer_size = 4096;

// Configuration (this should be in a configuration file)
const char* server_socket_path = "/tmp/asgard_socket";
const char* client_socket_path = "/tmp/asgard_system_socket";
const char* sys_thermal = "/sys/class/thermal/thermal_zone0/temp";
const std::size_t delay_ms = 5000;

//Buffer
char write_buffer[buffer_size];
char receive_buffer[buffer_size];

// The socket file descriptor
int socket_fd;

// The socket addresses
struct sockaddr_un client_address;
struct sockaddr_un server_address;

// The remote IDs
int source_id = -1;
int sensor_id = -1;

void stop(){
    std::cout << "asgard:system: stop the driver" << std::endl;

    // Unregister the sensor, if necessary
    if(sensor_id >= 0){
        auto nbytes = snprintf(write_buffer, buffer_size, "UNREG_SENSOR %d %d", source_id, sensor_id);
        sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr *) &server_address, sizeof(struct sockaddr_un));
    }

    // Unregister the source, if necessary
    if(source_id >= 0){
        auto nbytes = snprintf(write_buffer, buffer_size, "UNREG_SOURCE %d", source_id);
        sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr *) &server_address, sizeof(struct sockaddr_un));
    }

    // Unlink the client socket
    unlink(client_socket_path);

    // Close the socket
    close(socket_fd);
}

void terminate(int){
    stop();

    std::exit(0);
}

double read_system_temperature(){
    std::ifstream is(sys_thermal);
    std::string line;
    std::getline(is, line);
    int value = std::atoi(line.c_str());
    return value / (double)1000;
}

} //End of anonymous namespace

int main(){
    // Open the socket
    socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if(socket_fd < 0){
        std::cerr << "asgard:system: socket() failed" << std::endl;
        return 1;
    }

    // Init the client address
    memset(&client_address, 0, sizeof(struct sockaddr_un));
    client_address.sun_family = AF_UNIX;
    snprintf(client_address.sun_path, UNIX_PATH_MAX, client_socket_path);

    // Unlink the client socket
    unlink(client_socket_path);

    // Bind to client socket
    if(bind(socket_fd, (const struct sockaddr *) &client_address, sizeof(struct sockaddr_un)) < 0){
        std::cerr << "asgard:system: bind() failed" << std::endl;
        return 1;
    }

    //Register signals for "proper" shutdown
    signal(SIGTERM, terminate);
    signal(SIGINT, terminate);

    // Init the server address
    memset(&server_address, 0, sizeof(struct sockaddr_un));
    server_address.sun_family = AF_UNIX;
    snprintf(server_address.sun_path, UNIX_PATH_MAX, server_socket_path);

    socklen_t address_length = sizeof(struct sockaddr_un);

    // Register the source
    auto nbytes = snprintf(write_buffer, buffer_size, "REG_SOURCE system");
    sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr *) &server_address, sizeof(struct sockaddr_un));

    auto bytes_received = recvfrom(socket_fd, receive_buffer, buffer_size, 0, (struct sockaddr *) &(server_address), &address_length);
    receive_buffer[bytes_received] = '\0';

    source_id = atoi(receive_buffer);

    std::cout << "asgard:system: remote source: " << source_id << std::endl;

    // Register the sensor
    nbytes = snprintf(write_buffer, buffer_size, "REG_SENSOR %d %s %s", source_id, "TEMPERATURE", "cpu");
    sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr *) &server_address, sizeof(struct sockaddr_un));

    bytes_received = recvfrom(socket_fd, receive_buffer, buffer_size, 0, (struct sockaddr *) &(server_address), &address_length);
    receive_buffer[bytes_received] = '\0';

    sensor_id = atoi(receive_buffer);

    std::cout << "asgard:system: remote sensor: " << sensor_id << std::endl;

    while(true){
        double value = read_system_temperature();

        // Send the data
        nbytes = snprintf(write_buffer, buffer_size, "DATA %d %d %.2f", source_id, sensor_id, value);
        sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr *) &server_address, sizeof(struct sockaddr_un));

        // Wait some time before messages
        usleep(delay_ms * 1000);
    }

    stop();

    return 0;
}
