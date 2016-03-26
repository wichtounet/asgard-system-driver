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

struct driver_connector {
    //Buffer
    char write_buffer[buffer_size];
    char receive_buffer[buffer_size];

    // The socket file descriptor
    int socket_fd;

    // The socket addresses
    struct sockaddr_un client_address;
    struct sockaddr_un server_address;
};

driver_connector driver;

// The remote IDs
int source_id = -1;
int sensor_id = -1;

void unreg_sensor(driver_connector& driver, int source_id, int sensor_id){
    // Unregister the sensor, if necessary
    if(sensor_id >= 0){
        auto nbytes = snprintf(driver.write_buffer, buffer_size, "UNREG_SENSOR %d %d", source_id, sensor_id);
        sendto(driver.socket_fd, driver.write_buffer, nbytes, 0, (struct sockaddr *) &driver.server_address, sizeof(struct sockaddr_un));
    }
}

void unreg_source(driver_connector& driver, int source_id){
    // Unregister the source, if necessary
    if(source_id >= 0){
        auto nbytes = snprintf(driver.write_buffer, buffer_size, "UNREG_SOURCE %d", source_id);
        sendto(driver.socket_fd, driver.write_buffer, nbytes, 0, (struct sockaddr *) &driver.server_address, sizeof(struct sockaddr_un));
    }
}

void stop(){
    std::cout << "asgard:system: stop the driver" << std::endl;

    unreg_sensor(driver, source_id, sensor_id);
    unreg_source(driver, source_id);

    // Unlink the client socket
    unlink(client_socket_path);

    // Close the socket
    close(driver.socket_fd);
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

bool open_driver_connection(driver_connector& driver, const char* client_socket_path, const char* server_socket_path){
    // Open the socket
    driver.socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if(driver.socket_fd < 0){
        std::cerr << "asgard:driversystem: socket() failed" << std::endl;
        return false;
    }

    // Init the client address
    memset(&driver.client_address, 0, sizeof(struct sockaddr_un));
    driver.client_address.sun_family = AF_UNIX;
    snprintf(driver.client_address.sun_path, UNIX_PATH_MAX, client_socket_path);

    // Unlink the client socket
    unlink(client_socket_path);

    // Bind to client socket
    if(bind(driver.socket_fd, (const struct sockaddr *) &driver.client_address, sizeof(struct sockaddr_un)) < 0){
        std::cerr << "asgard:driver: bind() failed" << std::endl;
        return false;
    }

    // Init the server address
    memset(&driver.server_address, 0, sizeof(struct sockaddr_un));
    driver.server_address.sun_family = AF_UNIX;
    snprintf(driver.server_address.sun_path, UNIX_PATH_MAX, server_socket_path);

    return true;
}

int register_source(driver_connector& driver, const std::string& source_name){
    socklen_t address_length = sizeof(struct sockaddr_un);

    // Register the source
    auto nbytes = snprintf(driver.write_buffer, buffer_size, "REG_SOURCE %s", source_name.c_str());
    sendto(driver.socket_fd, driver.write_buffer, nbytes, 0, (struct sockaddr *) &driver.server_address, sizeof(struct sockaddr_un));

    auto bytes_received = recvfrom(driver.socket_fd, driver.receive_buffer, buffer_size, 0, (struct sockaddr *) &(driver.server_address), &address_length);
    driver.receive_buffer[bytes_received] = '\0';

    auto source_id = atoi(driver.receive_buffer);

    std::cout << "asgard:driver: remote source: " << source_id << std::endl;

    return source_id;
}

int register_sensor(driver_connector& driver, int source_id, const std::string& type, const std::string& name){
    socklen_t address_length = sizeof(struct sockaddr_un);

    // Register the sensor
    auto nbytes = snprintf(driver.write_buffer, buffer_size, "REG_SENSOR %d %s %s", source_id, type.c_str(), name.c_str());
    sendto(driver.socket_fd, driver.write_buffer, nbytes, 0, (struct sockaddr *) &driver.server_address, sizeof(struct sockaddr_un));

    auto bytes_received = recvfrom(driver.socket_fd, driver.receive_buffer, buffer_size, 0, (struct sockaddr *) &(driver.server_address), &address_length);
    driver.receive_buffer[bytes_received] = '\0';

    auto sensor_id = atoi(driver.receive_buffer);

    std::cout << "asgard:driver: remote sensor(" << name << "):" << sensor_id << std::endl;

    return sensor_id;
}

void send_data(driver_connector& driver, int source_id, int sensor_id, double value){
    // Send the data
    auto nbytes = snprintf(driver.write_buffer, buffer_size, "DATA %d %d %.2f", source_id, sensor_id, value);
    sendto(driver.socket_fd, driver.write_buffer, nbytes, 0, (struct sockaddr *) &driver.server_address, sizeof(struct sockaddr_un));
}

} //End of anonymous namespace

int main(){
    // Open the connection
    if(!open_driver_connection(driver, client_socket_path, server_socket_path)){
        return 1;
    }

    //Register signals for "proper" shutdown
    signal(SIGTERM, terminate);
    signal(SIGINT, terminate);

    source_id = register_source(driver, "system");
    sensor_id = register_sensor(driver, source_id, "TEMPERATURE", "cpu");

    while(true){
        double value = read_system_temperature();

        send_data(driver, source_id, sensor_id, value);

        // Wait some time before messages
        usleep(delay_ms * 1000);
    }

    stop();

    return 0;
}
