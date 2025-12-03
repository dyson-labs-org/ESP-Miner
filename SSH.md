# ESP-Miner Network Connectivity Research

This document outlines the technical options for adding SSH capabilities, USB device-to-device communication, and W5500 Ethernet integration to the ESP-Miner project.

## 1. SSH Client and Server Options for ESP32

### Recommended: LibSSH-ESP32

The most current and feature-complete option is **LibSSH-ESP32** by ewpa, last updated September 2025:

- **Capabilities**: Both SSH client and server functionality
- **Framework**: Works with ESP-IDF (built and tested against ESP-IDF as Arduino component)
- **Platform Support**: ESP32, ESP32-C3, ESP32-S2, and ESP32-S3
- **Based on**: libssh-0.11.3 (stable-0.11 branch)
- **Examples**: Includes SSH server, SSH client, SCP client, and OTA flashing examples
- **Important Note**: For stability under concurrency, disable `CONFIG_MBEDTLS_HARDWARE_SHA` in sdkconfig

### Alternative Options

- **esp-idf-ssh-client** (nopnop2002): Client-only, uses libssh2, ESP-IDF v5 compatible
- **wolfSSH**: Commercial option with ESP-IDF support, server examples available
- **ch405labs_esp_libssh2**: Works with ESP-IDF 5.2 (not 5.3 due to mbedtls changes)

## 2. USB-C Device-to-Device Communication (Master/Slave)

### Hardware Capabilities

The ESP32-S3 (and S2) support USB 2.0 OTG with both Host and Device functions, but **cannot operate as both simultaneously**. You can switch between modes programmatically.

### Architecture for Master/Slave Setup

- **Master Device**: Run in USB Host mode using ESP-IDF USB Host Stack
- **Slave Device**: Run in USB Device mode using TinyUSB stack with CDC-ACM

### Communication Options

#### Option 1: CDC-ACM (Serial over USB)

- Master uses CDC-ACM Host Driver
- Slave implements CDC-ACM device
- Communication via serial-like interface
- Examples: `examples/peripherals/usb/host/cdc/cdc_acm_vcp`

#### Option 2: USB Network (CDC-NCM/RNDIS) - RECOMMENDED

- Implements Ethernet-over-USB
- Slave runs CDC-NCM device (TinyUSB component supports this)
- Creates network interface over USB cable
- **Example available**: `peripherals/usb/device/tusb_ncm` demonstrates Wi-Fi data transmission to host via USB using Network Control Model
- Protocols available: NCM (Network Control Model), ECM (Ethernet Control Model), RNDIS (Remote NDIS - Microsoft)
- **Advantage**: Your existing HTTP-based swarm functionality would work unchanged over USB

### Hardware Connections

- ESP32-S3: GPIO19 (D-), GPIO20 (D+)
- Standard USB-C cable for data

## 3. W5500 Ethernet Integration and Swarm Functionality

### W5500 Daughterboard Integration

The W5500 is **officially supported** in ESP-IDF as an SPI Ethernet module.

#### Hardware Setup

- SPI connections: MOSI, MISO, CLK, CS
- Interrupt pin (required for ESP-IDF, unlike Arduino)
- Optional reset pin
- **Clock Speed**: W5500 supports up to 80 MHz, but users report stable operation at 20-40 MHz

#### Software

- Official examples: `examples/ethernet/basic` in ESP-IDF
- Community drivers available: MouNir9944/W5500_Ethernet_Driver_for_ESP32_ESP_IDF
- Configuration via ESP-IDF menuconfig

### Existing Swarm Functionality Analysis

Based on `main/http_server/axe-os/src/app/components/swarm/swarm.component.ts:142-164`, the current swarm implementation:

1. **Network Discovery**: Scans local /24 subnet by calculating IP range from current device's IP and netmask
2. **Device Polling**: Makes HTTP requests to `http://{IP}/api/system/info` and `http://{IP}/api/system/asic` on each discovered device
3. **Data Aggregation**: Collects hashrate, power, temperature, shares, and other metrics
4. **Automatic Refresh**: Polls devices periodically (default 30 seconds)
5. **Manual Management**: Users can manually add/remove devices by IP

### Proxying and Multi-Connection Strategy

#### Scenario 1: Both devices on same network (W5500)

- Current swarm functionality works unchanged
- Both devices accessible via Ethernet network
- Web UI can discover and manage both automatically

#### Scenario 2: One device via USB, one via Ethernet

Three approaches:

##### A. USB Network Bridge (RECOMMENDED)

- Use CDC-NCM on USB slave device
- Configure master to bridge between Ethernet (W5500) and USB network interfaces
- USB device gets IP on same subnet as Ethernet network
- Swarm functionality works unchanged - both devices accessible via HTTP

##### B. SSH Tunneling

- USB device runs SSH server
- Master device creates SSH tunnel to forward HTTP traffic
- Master acts as proxy, forwarding swarm HTTP requests over SSH tunnel
- More complex but provides encryption

##### C. Custom Protocol Bridge

- Master device implements custom proxy
- Receives HTTP requests on Ethernet interface
- Forwards over USB (CDC-ACM or custom protocol)
- Requires significant development effort

## Recommendation

Use **CDC-NCM (USB Networking)** approach because:

1. Creates proper network interface over USB
2. No changes needed to existing swarm code
3. Standard IP networking stack works unchanged
4. Can use existing HTTP API infrastructure

## Sources

### SSH Libraries

- [LibSSH-ESP32 GitHub](https://github.com/ewpa/LibSSH-ESP32)
- [LibSSH-ESP32 PlatformIO](https://registry.platformio.org/libraries/ewpa/LibSSH-ESP32)
- [esp-idf-ssh-client](https://github.com/nopnop2002/esp-idf-ssh-client)
- [wolfSSH ESP32 Examples](https://github.com/wolfSSL/wolfssh-examples/blob/main/Espressif/ESP32/ESP32-SSH-Server/README.md)

### USB Documentation

- [ESP-IDF USB Host Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html)
- [ESP-IDF USB Device Stack](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_device.html)
- [CDC-ACM Host Example](https://github.com/espressif/esp-idf/blob/master/examples/peripherals/usb/host/cdc/cdc_acm_vcp/README.md)

### W5500 Ethernet

- [ESP32 W5500 Integration Guide](https://mischianti.org/esp32-ethernet-w5500-with-plain-http-and-ssl-https/)
- [W5500 ESP32 Driver](https://github.com/MouNir9944/W5500_Ethernet_Driver_for_ESP32_ESP_IDF)
- [ESP-IDF W5500 Issue Discussion](https://github.com/espressif/esp-idf/issues/10963)

### USB Networking

- [TinyUSB Application Guide](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/tinyusb_guide.html)
- [Ethernet over USB Wikipedia](https://en.wikipedia.org/wiki/Ethernet_over_USB)
